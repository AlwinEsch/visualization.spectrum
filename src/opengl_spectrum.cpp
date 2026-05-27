/*
 *  Copyright (C) 1998-2000 Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 2005-2026 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

/*
 *  Wed May 24 10:49:37 CDT 2000
 *  Fixes to threading/context creation for the nVidia X4 drivers by
 *  Christian Zander <phoenix@minion.de>
 */

/*
 *  Ported to XBMC by d4rk
 *  Also added 'm_hSpeed' to animate transition between bar heights
 *
 *  Ported to GLES 2.0 by Gimli
 */

#include "opengl_spectrum.h"

#include "fft.h"

#include <algorithm>

#if defined(HAS_GL)

const char* vertex_shader_src =
    R"(#version 150

in vec3 position;
in vec3 color;
in float id;

uniform int m_pos;
uniform mat4 mvp;
uniform sampler2D m_bars;

out vec4 vertexColor;

void main()
{
    vec2 uv = vec2
    (
        mod (id, 32.0) + 0.5,
        mod (float ((int (id) / 32) + m_pos), 32.0) + 0.5
    );

    float height = texture(m_bars, uv / 32.0).r * 1.6;

    gl_Position = mvp * vec4 (position.x, position.y * height, position.z, 1.0);
    vertexColor = vec4 (color * (0.2 + 0.8 * height), 1.0);
}
)";

const char* fragment_shader_src =
    R"(#version 150

precision mediump float;
in vec4 vertexColor;

out vec4 outputColor;

void main() {
    outputColor = vertexColor;
}
)";

#else

const char* vertex_shader_src =
    R"(#version 300 es

in vec3 position;
in vec3 color;
in float id;

uniform int m_pos;
uniform mat4 mvp;
uniform sampler2D m_bars;

out vec4 vertexColor;

void main()
{
    vec2 uv = vec2
    (
        mod (id, 32.0) + 0.5,
        mod (float ((int (id) / 32) + m_pos), 32.0) + 0.5
    );

    float height = texture (m_bars, uv / 32.0).r * 1.6;

    gl_Position = mvp * vec4 (position.x, position.y * height, position.z, 1.0);
    vertexColor = vec4 (color * (0.2 + 0.8 * height), 1.0);
}
)";

const char* fragment_shader_src =
    R"(#version 300 es

precision mediump float;
in vec4 vertexColor;

out vec4 outputColor;

void main() {
    outputColor = vertexColor;
}
)";

#endif

struct vertex
{
  GLfloat position[3];
  GLfloat color[3];

  /* If -1 the Y coordinate will be 0, otherwise it'll be used to index the
     * texture */
  GLint id;
};

struct triangle
{
  vertex position[3];
};

CVisualizationSpectrum::CVisualizationSpectrum()
  : m_pcm(new float[AUDIO_BUFFER]()),
    m_projection(glm::frustum(-1.1f, 1.0f, -1.5f, 1.0f, 2.0f, 10.0f))
{
}

CVisualizationSpectrum::~CVisualizationSpectrum()
{
  delete[] m_pcm;
}

bool CVisualizationSpectrum::Init()
{
  for (int i = 0; i <= NUM_BANDS; i++)
    m_logscale[i] = powf(256, (float)i / NUM_BANDS) - 0.5f;

  for (int y = 0; y < NUM_BANDS; y++)
  {
    float yf = (float)y / (NUM_BANDS - 1);

    for (int x = 0; x < NUM_BANDS; x++)
    {
      float xf = (float)x / (NUM_BANDS - 1);

      m_colors[x][y][0] = (1 - xf) * (1 - yf);
      m_colors[x][y][1] = xf;
      m_colors[x][y][2] = yf;
    }
  }

  if (!init_shaders())
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to initialize shaders");
    return false;
  }

  glGenVertexArrays(1, &m_vao);

  std::vector<triangle> triangle_buffer;

  /* We need upload the geometry data only once */
  add_bars(&triangle_buffer, (float*)m_colors);
  update_vbo(triangle_buffer.data(), triangle_buffer.size());

  m_triangle_count = triangle_buffer.size();

  glGenTextures(1, &m_values_tex);
  glBindTexture(GL_TEXTURE_2D, m_values_tex);

  //glClearColor(0, 0, 0, 1);

  m_initialized = true;

  return true;
}

void CVisualizationSpectrum::DeInit()
{
  //glDisable(GL_TEXTURE);

  if (m_values_tex)
    glDeleteTextures(1, &m_values_tex);

  if (m_vao)
    glDeleteVertexArrays(1, &m_vao);

  if (m_program)
    glDeleteProgram(m_program);

  while (!m_fftqueue.empty())
  {
    float* freq = m_fftqueue.front().second;
    m_fftqueue.pop();
    delete[] freq;
  }
}

bool CVisualizationSpectrum::AudioStart(int channels, int samplesPerSec, int bitsPerSample)
{
  m_channels = channels;
  m_samplesPerSec = samplesPerSec;
  m_bitsPerSample = bitsPerSample;
  m_currentTime = std::chrono::steady_clock::now();
  return true;
}

void CVisualizationSpectrum::AudioData(const float* pAudioData, size_t iAudioDataLength)
{
  const size_t samples = iAudioDataLength / m_channels;
  const size_t num_frames = samples / AUDIO_BUFFER;
  for (size_t i = 0; i < num_frames; i += 4)
  {
    write_to_buffer(pAudioData + i * AUDIO_BUFFER * m_channels, AUDIO_BUFFER * m_channels,
                  m_channels);
    float* freq = new float[1024];
    calc_freq(m_pcm, freq);
    m_fftqueue.push(std::make_pair(m_currentTime, freq));
    m_currentTime += std::chrono::milliseconds((int)(1000.0 / m_samplesPerSec * AUDIO_BUFFER));
  }
}

void CVisualizationSpectrum::Render()
{
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  while (!m_fftqueue.empty() && m_fftqueue.front().first < now)
  {
    float* freq = m_fftqueue.front().second;
    m_fftqueue.pop();
    render_freq(freq);
    delete[] freq;
  }

  glClearColor(0, 0, 0, 1);

  //glEnable(GL_TEXTURE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glUseProgram(m_program);

  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, glm::vec3(0.0f, -0.5f, -5.0f));

  model = glm::rotate(model, glm::radians(38.0f), glm::vec3(1.0f, 0.0f, 0.0f));

  model = glm::rotate(model, glm::radians(m_angle + 180.0f), glm::vec3(0.0f, 1.0f, 0.0f));

  glm::mat4 mvp = m_projection * model;

  glUniformMatrix4fv(m_mvp_pointer, 1, GL_FALSE, glm::value_ptr(mvp));
  glUniform1i(m_m_pos_pointer, m_pos);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_values_tex);

  /* Upload our array as an OpenGL texture */
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, NUM_BANDS, NUM_BANDS, 0, GL_RED, GL_FLOAT, m_bars);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

  glBindVertexArray(m_vao);

  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_triangle_count * 3));

  glBindVertexArray(0);
  glUseProgram(0);

  //glDisable(GL_TEXTURE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from Kodi)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS CVisualizationSpectrum::SetSetting(const std::string& settingName,
                                                const kodi::addon::CSettingValue& settingValue)
{
  if (settingName.empty() || settingValue.empty())
    return ADDON_STATUS_UNKNOWN;

  return ADDON_STATUS_OK;
}

void CVisualizationSpectrum::write_to_buffer(const float* input, size_t length, size_t channels)
{
  size_t frames = length / channels;

  if (frames >= AUDIO_BUFFER)
  {
    size_t offset = frames - AUDIO_BUFFER;

    mix(m_pcm, input + offset, AUDIO_BUFFER, channels);
  }
  else
  {
    size_t keep = AUDIO_BUFFER - frames;
    memmove(m_pcm, m_pcm + frames, keep * sizeof(float));

    mix(m_pcm + keep, input, frames, channels);
  }
}

void CVisualizationSpectrum::mix(float* destination,
                                 const float* source,
                                 size_t frames,
                                 size_t channels)
{
  size_t length = frames * channels;
  for (size_t i = 0; i < length; i += channels)
  {
    float v = 0.0f;
    for (size_t j = 0; j < channels; j++)
    {
      v += source[i + j];
    }

    destination[(i / 2)] = v / (float)channels;
  }
}

float CVisualizationSpectrum::blackman_window(float in, size_t i, size_t length)
{
  const float alpha = 0.16f;
  const float a0 = 0.5f * (1.0f - alpha);
  const float a1 = 0.5f;
  const float a2 = 0.5f * alpha;

  const float x = static_cast<float>(i) / static_cast<float>(length);
  return in * (a0 - a1 * cos(2.0f * M_PI * x) + a2 * cos(4.0f * M_PI * x));
}

void CVisualizationSpectrum::render_freq(const float* freq)
{
  make_log_graph(freq, m_bars[m_pos]);
  m_pos = (m_pos + 1) % NUM_BANDS;

  m_angle += m_anglespeed;
  if (m_angle > 45 || m_angle < -45)
    m_anglespeed = -m_anglespeed;
}

/* stolen from the skins plugin */
/* convert linear frequency graph to logarithmic one */
void CVisualizationSpectrum::make_log_graph(const float* freq, float* graph)
{
  for (int i = 0; i < NUM_BANDS; i++)
  {
    /* sum up values in freq array between m_logscale[i] and m_logscale[i + 1],
           including fractional parts */
    int a = static_cast<int>(std::ceil(m_logscale[i]));
    int b = static_cast<int>(std::floor(m_logscale[i + 1]));
    float sum = 0;

    if (b < a)
      sum += freq[b] * (m_logscale[i + 1] - m_logscale[i]);
    else
    {
      if (a > 0)
        sum += freq[a - 1] * (a - m_logscale[i]);
      for (; a < b; a++)
        sum += freq[a];
      if (b < 256)
        sum += freq[b] * (m_logscale[i + 1] - b);
    }

    /* fudge factor to make the graph have the same overall height as a
           12-band one no matter how many bands there are */
    sum *= (float)NUM_BANDS / 12;

    /* convert to dB */
    float val = 20 * log10f(sum);

    /* scale (-DB_RANGE, 0.0) to (0.0, 1.0) */
    val = 1 + val / DB_RANGE;

    graph[i] = std::clamp(val, 0.0f, 1.0f);
  }
}


bool CVisualizationSpectrum::init_shaders()
{
  GLuint program = 0;
  GLuint vertex = 0;
  GLuint fragment = 0;

  create_shader(GL_VERTEX_SHADER, vertex_shader_src, &vertex);
  create_shader(GL_FRAGMENT_SHADER, fragment_shader_src, &fragment);

  GLuint mvp_pointer = 0;
  GLuint m_pos_pointer = 0;

  if (vertex != 0 && fragment != 0)
  {
    /* link the vertex and fragment shaders together */
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    int status = GL_FALSE;

    glGetProgramiv(program, GL_LINK_STATUS, &status);

    if (status)
    {
      mvp_pointer = glGetUniformLocation(program, "mvp");
      m_pos_pointer = glGetUniformLocation(program, "m_pos");

      /* the individual shaders can be detached and destroyed */
      glDetachShader(program, vertex);
      glDetachShader(program, fragment);
    }
    else
    {
      int log_len = 0;
      glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);

      char* buffer = new char[log_len + 1];
      glGetProgramInfoLog(program, log_len, NULL, buffer);

      kodi::Log(ADDON_LOG_ERROR, "Linking failure in program: %s\n", buffer);

      delete[] buffer;

      glDeleteProgram(program);
      program = 0;
    }
  }

  if (vertex != 0)
    glDeleteShader(vertex);

  if (fragment != 0)
    glDeleteShader(fragment);

  if (program != 0)
  {
    m_program = program;
    m_mvp_pointer = mvp_pointer;
    m_m_pos_pointer = m_pos_pointer;
  }

  return program != 0;
}

GLuint CVisualizationSpectrum::create_shader(int shader_type,
                                             const char* source,
                                             GLuint* shader_out)
{
  GLuint shader = glCreateShader(shader_type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);

  int status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE)
  {
    int log_len;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);

    char* buffer = new char[log_len + 1];
    glGetShaderInfoLog(shader, log_len, NULL, buffer);

    kodi::Log(ADDON_LOG_ERROR, "%s shader compilation error:%s",
              (shader_type == GL_VERTEX_SHADER ? "Vertex" : "Fragment"), buffer);

    delete[] buffer;

    glDeleteShader(shader);
    shader = 0;
  }

  if (shader_out != NULL)
    *shader_out = shader;

  return shader != 0;
}

void CVisualizationSpectrum::update_vbo(const triangle* triangle_buffer, size_t triangle_count)
{
  GLuint position_pointer = glGetAttribLocation(m_program, "position");
  GLuint color_pointer = glGetAttribLocation(m_program, "color");
  GLuint id_pointer = glGetAttribLocation(m_program, "id");

  GLuint vbo;

  glGenBuffers(1, &vbo);

  glBindVertexArray(m_vao);

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(struct triangle) * triangle_count, triangle_buffer,
               GL_STATIC_DRAW);

  /* enable and set the position attribute */
  glEnableVertexAttribArray(position_pointer);
  glVertexAttribPointer(position_pointer, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                        BUFFER_OFFSET(offsetof(struct vertex, position)));

  /* enable and set the color attribute */
  glEnableVertexAttribArray(color_pointer);
  glVertexAttribPointer(color_pointer, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                        BUFFER_OFFSET(offsetof(struct vertex, color)));

  /* enable and set the id attribute */
  glEnableVertexAttribArray(id_pointer);
  glVertexAttribPointer(id_pointer, 1, GL_INT, GL_FALSE, sizeof(struct vertex),
                        BUFFER_OFFSET(offsetof(struct vertex, id)));

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  /* the VBO is referenced by the VAO */
  glDeleteBuffers(1, &vbo);
}

void CVisualizationSpectrum::add_rectangle(std::vector<triangle>* triangle_buffer,
                                           int id,
                                           float x1,
                                           float z1,
                                           float x2,
                                           float z2,
                                           float r,
                                           float g,
                                           float b)
{
  /* All triangle vertices should be aligned counter-clockwise */

  /* height multiplier */
  float h_mu = 1.0f;

  /* Top */
  triangle_buffer->push_back({
      vertex{{x2, h_mu, z2}, {r, g, b}, id},
      vertex{{x2, h_mu, z1}, {r, g, b}, id},
      vertex{{x1, h_mu, z1}, {r, g, b}, id},
  });

  triangle_buffer->push_back({
      vertex{{x1, h_mu, z2}, {r, g, b}, id},
      vertex{{x2, h_mu, z2}, {r, g, b}, id},
      vertex{{x1, h_mu, z1}, {r, g, b}, id},
  });

  /* Right */
  triangle_buffer->push_back({
      vertex{{x1, h_mu, z2}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
      vertex{{x1, h_mu, z1}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
      vertex{{x1, 0.0f, z1}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
  });

  triangle_buffer->push_back({
      vertex{{x1, 0.0f, z2}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
      vertex{{x1, h_mu, z2}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
      vertex{{x1, 0.0f, z1}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
  });

  /* Left */
  triangle_buffer->push_back({
      vertex{{x2, 0.0f, z2}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
      vertex{{x2, 0.0f, z1}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
      vertex{{x2, h_mu, z1}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
  });

  triangle_buffer->push_back({
      vertex{{x2, h_mu, z2}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
      vertex{{x2, 0.0f, z2}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
      vertex{{x2, h_mu, z1}, {0.65f * r, 0.65f * g, 0.65f * b}, id},
  });

  /* Front */
  triangle_buffer->push_back({
      vertex{{x2, h_mu, z1}, {0.8f * r, 0.8f * g, 0.8f * b}, id},
      vertex{{x2, 0.0f, z1}, {0.8f * r, 0.8f * g, 0.8f * b}, id},
      vertex{{x1, 0.0f, z1}, {0.8f * r, 0.8f * g, 0.8f * b}, id},
  });

  triangle_buffer->push_back({
      vertex{{x1, h_mu, z1}, {0.8f * r, 0.8f * g, 0.8f * b}, id},
      vertex{{x2, h_mu, z1}, {0.8f * r, 0.8f * g, 0.8f * b}, id},
      vertex{{x1, 0.0f, z1}, {0.8f * r, 0.8f * g, 0.8f * b}, id},
  });
}

void CVisualizationSpectrum::add_bars(std::vector<triangle>* triangle_buffer, const float* m_colors)
{
  for (int i = 0; i < NUM_BANDS; i++)
  {
    for (int j = 0; j < NUM_BANDS; j++)
    {
      float x = 1.6f - BAR_SPACING * j;
      float z = -1.6f + (NUM_BANDS - i) * BAR_SPACING;

      int id = (i % NUM_BANDS) * NUM_BANDS + j;

      float r = m_colors[id * 3 + 0];
      float g = m_colors[id * 3 + 1];
      float b = m_colors[id * 3 + 2];

      add_rectangle(triangle_buffer, id, x, z, x + BAR_WIDTH, z + BAR_WIDTH, r, g, b);
    }
  }
}

ADDONCREATOR(CVisualizationSpectrum)

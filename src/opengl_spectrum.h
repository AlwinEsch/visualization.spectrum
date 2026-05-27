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

#define __STDC_LIMIT_MACROS

//#include "kissfft/kiss_fft.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <kodi/addon-instance/Visualization.h>
#include <kodi/gui/gl/GL.h>
#include <kodi/gui/gl/Shader.h>
#include <math.h>
#include <queue>
#include <stdint.h>
#include <string.h>
#include <vector>

struct triangle;

#ifndef M_PI
#define M_PI 3.141592654f
#endif

#define AUDIO_BUFFER (512)
#define NUM_BANDS 32
#define DB_RANGE 40

#define BAR_SPACING (3.2f / NUM_BANDS)
#define BAR_WIDTH (0.8f * BAR_SPACING)

class ATTR_DLL_LOCAL CVisualizationSpectrum : public kodi::addon::CAddonBase,
                                              public kodi::addon::CInstanceVisualization
{
public:
  CVisualizationSpectrum();
  ~CVisualizationSpectrum() override;

  bool Init() override;
  void DeInit() override;
  bool AudioStart(int channels, int samplesPerSec, int bitsPerSample) override;
  void AudioData(const float* audioData, size_t audioDataLength) override;
  int AudioGetSyncDelay() override { return 0; };
  void Render() override;
  ADDON_STATUS SetSetting(const std::string& settingName,
                          const kodi::addon::CSettingValue& settingValue) override;

private:
  void write_to_buffer(const float* input, size_t length, size_t channels);
  void mix(float* destination, const float* source, size_t frames, size_t channels);
  float blackman_window(float in, size_t i, size_t length);

  bool init_shaders();
  GLuint create_shader(int shader_type, const char* source, GLuint* shader_out);
  void update_vbo(const triangle* triangle_buffer, size_t triangle_count);
  void add_rectangle(std::vector<triangle>* triangle_buffer,
                     int id,
                     float x1,
                     float z1,
                     float x2,
                     float z2,
                     float r,
                     float g,
                     float b);
  void add_bars(std::vector<triangle>* triangle_buffer, const float* m_colors);
  void render_freq(const float* freq);
  void make_log_graph(const float* freq, float* graph);

  unsigned int m_vao{0};
  unsigned int m_program{0};
  unsigned int m_values_tex{0};

  unsigned int m_mvp_pointer{0};
  unsigned int m_m_pos_pointer{0};

  glm::mat4 m_projection;

  size_t m_triangle_count{0};

  bool m_initialized{false};

  int m_channels;
  int m_samplesPerSec;
  int m_bitsPerSample;

  float* m_pcm;
  std::chrono::steady_clock::time_point m_currentTime;
  std::queue<std::pair<std::chrono::steady_clock::time_point, float*>> m_fftqueue;
  std::queue<float*> m_fftqueueInactive;

  int m_pos{0};
  float m_angle{25};
  float m_anglespeed{0.05f};
  float m_logscale[NUM_BANDS + 1]{{0}};
  float m_colors[NUM_BANDS][NUM_BANDS][3]{{0}};
  float m_bars[NUM_BANDS][NUM_BANDS]{{0}};
};

cmake_minimum_required(VERSION 3.18)
project(visualization.spectrum)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(glm REQUIRED)

if(NOT WIN32 AND (APP_RENDER_SYSTEM STREQUAL "gl" OR NOT APP_RENDER_SYSTEM))
  find_package(OpenGl REQUIRED)
  set(DEPLIBS ${OPENGL_LIBRARIES})
  set(INCLUDES ${OPENGL_INCLUDE_DIR})
  add_definitions(${OPENGL_DEFINITIONS})
else()
  find_package(OpenGLES REQUIRED)
  set(DEPLIBS ${OPENGLES_LIBRARIES})
  set(INCLUDES ${OPENGLES_INCLUDE_DIR})
  add_definitions(${OPENGLES_DEFINITIONS})
endif()


set(SPECTRUM_SOURCES src/fft.cc
                     src/opengl_spectrum.cpp)

set(SPECTRUM_HEADERS src/fft.h
                     src/opengl_spectrum.h)

include_directories(${INCLUDES}
                    ${GLM_INCLUDE_DIR}
                    ${KODI_INCLUDE_DIR}/..) # Hack way with "/..", need bigger Kodi cmake rework to match right include ways (becomes done in future)

build_addon(visualization.spectrum SPECTRUM DEPLIBS)

include(CPack)

cmake_minimum_required(VERSION 3.20)
project(ArcanaSurvivorsVita VERSION 0.3.0 LANGUAGES CXX)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "VITASDK is not set. Install VitaSDK 2026.08+ and export VITASDK.")
endif()
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(PROJECT_ROOT "${ROOT}/..")

add_executable(arcana_vita
  main.cpp
  ${PROJECT_ROOT}/src/game.cpp
  ${PROJECT_ROOT}/src/data.cpp
  ${PROJECT_ROOT}/src/profile.cpp
  ${PROJECT_ROOT}/src/native/render_queue.cpp
)

target_include_directories(arcana_vita PRIVATE ${PROJECT_ROOT}/include)
target_compile_features(arcana_vita PRIVATE cxx_std_20)
target_compile_options(arcana_vita PRIVATE -O3 -ffast-math -fomit-frame-pointer -Wall -Wextra)
target_link_options(arcana_vita PRIVATE -Wl,-q)

target_link_libraries(arcana_vita PRIVATE
  vita2d
  freetype png jpeg z m
  SceAppMgr_stub SceCommonDialog_stub SceCtrl_stub SceDisplay_stub
  SceGxm_stub SceKernelDmacMgr_stub ScePower_stub SceProcessmgr_stub SceSysmodule_stub
)

vita_create_self(eboot.bin arcana_vita SAFE)
vita_create_vpk(arcana-survivors.vpk ARCA00002 eboot.bin
  VERSION 00.30
  NAME "Arcana vita2d probe"
  FILE ${PROJECT_ROOT}/assets/native_atlas.png assets/native_atlas.png
)

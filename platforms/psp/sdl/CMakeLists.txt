# Arcana Survivors — PSP (EBOOT.PBP), PSPSDK + SDL2. Build inside pspdev/pspdev:
#   tools/docker/psp-build.sh      (from the repo root)
cmake_minimum_required(VERSION 3.20)
project(ArcanaSurvivorsPsp VERSION 0.9.1 LANGUAGES C CXX)

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
find_package(SDL2 REQUIRED)
# SDL2_image/SDL2_ttf CMake configs in pspdev fail on their own dependencies: link them by name.

add_executable(arcana_psp
  main.cpp
  ${ROOT}/platforms/sdl/batch_renderer.cpp
  ${ROOT}/platforms/sdl/frontend.cpp
  ${ROOT}/platforms/sdl/animator.cpp
  ${ROOT}/platforms/sdl/world_renderer.cpp
  ${ROOT}/platforms/sdl/audio.cpp
  ${ROOT}/src/game.cpp
  ${ROOT}/src/data.cpp
  ${ROOT}/src/profile.cpp
  ${ROOT}/src/native/render_queue.cpp
)
target_include_directories(arcana_psp PRIVATE ${ROOT}/include ${ROOT}/platforms/sdl)
target_compile_features(arcana_psp PRIVATE cxx_std_20)
# The Allegrex FPU is single precision only: float simulation and float literals everywhere.
target_compile_definitions(arcana_psp PRIVATE ARCANA_REAL_FLOAT)
target_compile_options(arcana_psp PRIVATE -O2 -G0 -fsingle-precision-constant -Wall
  -Wno-missing-field-initializers -Wno-misleading-indentation -Wno-unused-parameter)
target_link_libraries(arcana_psp PRIVATE
  SDL2_ttf SDL2_image freetype harfbuzz png jpeg z bz2 SDL2::SDL2main SDL2::SDL2-static
)

create_pbp_file(
  TARGET arcana_psp
  TITLE "Arcana Survivors"
  ICON_PATH ${CMAKE_CURRENT_LIST_DIR}/icon0.png
  BACKGROUND_PATH ${CMAKE_CURRENT_LIST_DIR}/pic1.png
  VERSION 00.90
  BUILD_PRX
)

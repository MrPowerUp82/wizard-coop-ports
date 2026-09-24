# Arcana Survivors — PS Vita (.vpk), VitaSDK + SDL2 (GXM). Build inside vitasdk/vitasdk:
#   tools/docker/vita-build.sh      (from the repo root)
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "VITASDK is not set.")
endif()
set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
project(ArcanaSurvivorsVitaSDL VERSION 0.9.1 LANGUAGES C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
find_package(SDL2 REQUIRED)
# The vitasdk SDL2 package pulls vitaGL -> vitashark after our libraries; its runtime-shader
# extension lib and pthread must come at the very end of the link line.
set(CMAKE_CXX_STANDARD_LIBRARIES "-lSceShaccCgExt -ltaihen_stub -lSceShaccCg_stub -Wl,--whole-archive -lpthread -Wl,--no-whole-archive ${CMAKE_CXX_STANDARD_LIBRARIES}")
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)

add_executable(arcana_vita_sdl
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
target_include_directories(arcana_vita_sdl PRIVATE ${ROOT}/include ${ROOT}/platforms/sdl)
target_compile_features(arcana_vita_sdl PRIVATE cxx_std_20)
target_compile_options(arcana_vita_sdl PRIVATE -O2 -fno-rtti -Wall -Wextra -Wno-missing-field-initializers -Wno-misleading-indentation)
# vita-elf-create appends ~4 KiB of SCE module info right after .text; the stock script starts .data
# at the next 64 KiB boundary, so a build whose code ends just below one fails with
# "segment 1 overlaps". Link with a copy of the stock script that always leaves 8 KiB of room.
execute_process(COMMAND sh -c "'${CMAKE_LINKER}' --verbose | sed -n '/^=====/,/^=====/p' | sed '1d;$d' | sed 's/\. = ALIGN(0x10000);/. = ALIGN(. + 0x2000, 0x10000);/' > '${CMAKE_BINARY_DIR}/vita_padded.ld'"
                RESULT_VARIABLE ARCANA_LD_RESULT)
if(NOT ARCANA_LD_RESULT EQUAL 0)
  message(FATAL_ERROR "could not derive the padded Vita linker script")
endif()
target_link_options(arcana_vita_sdl PRIVATE -Wl,-q -Wl,-T,${CMAKE_BINARY_DIR}/vita_padded.ld)
target_link_libraries(arcana_vita_sdl PRIVATE
  SDL2_ttf::SDL2_ttf-static SDL2_image::SDL2_image-static SDL2::SDL2-static
  freetype png bz2 z
  SceCtrl_stub ScePower_stub SceProcessmgr_stub
)

vita_create_self(eboot.bin arcana_vita_sdl UNSAFE)
vita_create_vpk(arcana-survivors-native.vpk ARCA00001 eboot.bin
  VERSION 00.90
  NAME "Arcana Survivors"
  FILE ${ROOT}/assets/native_atlas_128.png assets/native_atlas_128.png
  FILE ${ROOT}/assets/terrain_tiles.png assets/terrain_tiles.png
  FILE ${ROOT}/assets/fonts/DejaVuSans-Bold.ttf assets/fonts/DejaVuSans-Bold.ttf
  FILE ${CMAKE_CURRENT_LIST_DIR}/sce_sys sce_sys
)

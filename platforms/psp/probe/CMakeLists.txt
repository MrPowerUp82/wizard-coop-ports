# Arcana Survivors — PSP CPU probe (EBOOT.PBP). Build inside pspdev/pspdev:
#   tools/docker/psp-probe-build.sh      (from the repo root)
cmake_minimum_required(VERSION 3.20)
project(ArcanaPspProbe LANGUAGES C CXX)

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
add_executable(arcana_psp_probe
  main.cpp
  ${ROOT}/src/game.cpp
  ${ROOT}/src/data.cpp
  ${ROOT}/src/native/render_queue.cpp
)
target_include_directories(arcana_psp_probe PRIVATE ${ROOT}/include ${ROOT}/tools/probe)
target_compile_features(arcana_psp_probe PRIVATE cxx_std_20)
# Single-precision simulation: the Allegrex FPU has no double support (see include/arcana/real.hpp).
target_compile_definitions(arcana_psp_probe PRIVATE ARCANA_REAL_FLOAT)
target_compile_options(arcana_psp_probe PRIVATE -O2 -G0 -fsingle-precision-constant -Wdouble-promotion -Wall -Wno-missing-field-initializers -Wno-misleading-indentation -Wno-unused-parameter)
target_link_libraries(arcana_psp_probe PRIVATE pspdebug pspdisplay pspge pspctrl psppower)

create_pbp_file(
  TARGET arcana_psp_probe
  TITLE "Arcana Survivors CPU Probe"
  VERSION 01.00
  BUILD_PRX
)

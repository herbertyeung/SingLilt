# Pinned desktop library discovery and imported runtime targets.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

if(EXISTS "${CMAKE_BINARY_DIR}/_deps/Qt/6.8.3/msvc2022_64")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/_deps/Qt/6.8.3/msvc2022_64")
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network Concurrent)
set(FLUIDSYNTH_ROOT "${CMAKE_BINARY_DIR}/_deps/FluidSynth/fluidsynth-v2.6.1-win10-x64-cpp11" CACHE PATH "FluidSynth runtime")
set(SALAMANDER_SF2 "${CMAKE_BINARY_DIR}/_deps/SoundFonts/SalamanderGrandPiano-SF2-V3+20200602/SalamanderGrandPiano-V3+20200602.sf2" CACHE FILEPATH "Salamander SF2")
if(NOT EXISTS "${FLUIDSYNTH_ROOT}/lib/libfluidsynth-3.lib" OR NOT EXISTS "${SALAMANDER_SF2}")
    message(FATAL_ERROR "Run pwsh -File scripts/setup-soundfonts.ps1 to fetch the verified runtime and piano.")
endif()
add_library(FluidSynthRuntime SHARED IMPORTED)
set_target_properties(FluidSynthRuntime PROPERTIES
    IMPORTED_IMPLIB "${FLUIDSYNTH_ROOT}/lib/libfluidsynth-3.lib"
    IMPORTED_LOCATION "${FLUIDSYNTH_ROOT}/bin/libfluidsynth-3.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${FLUIDSYNTH_ROOT}/include")

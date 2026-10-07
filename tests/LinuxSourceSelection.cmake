# Exercise actual source selection without needing Linux development libraries.
# Copyright (c) 2026 Herbert Yeung
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.30)

if(NOT DEFINED SINGLILT_SOURCE_ROOT)
    get_filename_component(SINGLILT_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
set(CMAKE_SYSTEM_NAME Linux)
include("${SINGLILT_SOURCE_ROOT}/cmake/Sources.cmake")
set(windows_sources src/audio/MicrophoneCapture.cpp src/audio/OriginalAudioPlayer.cpp
    src/audio/MidiInstrument.cpp src/recognition/WindowsOcr.cpp)
set(linux_sources src/audio/linux/MicrophoneCapture.cpp src/audio/linux/OriginalAudioPlayer.cpp
    src/audio/linux/MidiInstrument.cpp src/recognition/linux/TextOcr.cpp src/recognition/linux/AudioDecoder.cpp)
foreach(source IN LISTS linux_sources)
    if(NOT source IN_LIST APP_FILES OR NOT EXISTS "${SINGLILT_SOURCE_ROOT}/${source}")
        message(FATAL_ERROR "Linux source selection: FAIL missing ${source}")
    endif()
endforeach()
foreach(source IN LISTS windows_sources)
    if(source IN_LIST APP_FILES)
        message(FATAL_ERROR "Linux source selection: FAIL selected Windows backend ${source}")
    endif()
endforeach()
set(CMAKE_SYSTEM_NAME Windows)
include("${SINGLILT_SOURCE_ROOT}/cmake/Sources.cmake")
foreach(source IN LISTS windows_sources)
    if(NOT source IN_LIST APP_FILES)
        message(FATAL_ERROR "Windows source selection: FAIL missing ${source}")
    endif()
endforeach()
foreach(source IN LISTS linux_sources)
    if(source IN_LIST APP_FILES)
        message(FATAL_ERROR "Windows source selection: FAIL selected Linux backend ${source}")
    endif()
endforeach()
message(STATUS "Platform source selection: PASS Linux=5 Windows=4; mutually exclusive")

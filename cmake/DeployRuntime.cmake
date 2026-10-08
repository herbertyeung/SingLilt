# Runtime libraries, sound banks, and optional tool deployment.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/DeployLinux.cmake)
    return()
endif()

add_custom_command(TARGET SingLilt POST_BUILD
    COMMAND Qt6::windeployqt "--$<LOWER_CASE:$<CONFIG>>"
        --no-translations --no-opengl-sw --no-system-d3d-compiler
        "$<TARGET_FILE:SingLilt>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/language" "$<TARGET_FILE_DIR:SingLilt>/language"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:SingLilt>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:SingLilt>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/README.md" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${CMAKE_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:SingLilt>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SingLilt>/assets/soundfonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SALAMANDER_SF2}" "$<TARGET_FILE_DIR:SingLilt>/assets/soundfonts/Salamander.sf2"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FLUIDSYNTH_ROOT}/bin/libfluidsynth-3.dll" "${FLUIDSYNTH_ROOT}/bin/SDL3.dll" "${FLUIDSYNTH_ROOT}/bin/sndfile.dll" "$<TARGET_FILE_DIR:SingLilt>"
    COMMENT "Deploy application runtime"
    VERBATIM)

set(GENERALUSER_SF2 "${CMAKE_BINARY_DIR}/_deps/SoundFonts/GeneralUser-GS/GeneralUser-GS.sf2")
if(EXISTS "${GENERALUSER_SF2}")
    add_custom_command(TARGET SingLilt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GENERALUSER_SF2}"
            "$<TARGET_FILE_DIR:SingLilt>/assets/soundfonts/GeneralUser-GS.sf2")
endif()

set(WHISPER_ROOT "${CMAKE_BINARY_DIR}/_deps/Whisper")
if(EXISTS "${WHISPER_ROOT}/runtime/Release/whisper-cli.exe")
    add_custom_command(TARGET SingLilt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SingLilt>/tools/whisper"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SingLilt>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WHISPER_ROOT}/runtime/Release/whisper-cli.exe"
            "${WHISPER_ROOT}/runtime/Release/whisper.dll"
            "${WHISPER_ROOT}/runtime/Release/ggml.dll"
            "${WHISPER_ROOT}/runtime/Release/ggml-base.dll"
            "${WHISPER_ROOT}/runtime/Release/ggml-cpu.dll"
            "$<TARGET_FILE_DIR:SingLilt>/tools/whisper"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WHISPER_ROOT}/models/ggml-base.bin"
            "$<TARGET_FILE_DIR:SingLilt>/models/ggml-base.bin")
endif()

set(SEPARATION_ROOT "${CMAKE_BINARY_DIR}/_deps/Separation/runtime")
if(EXISTS "${SEPARATION_ROOT}/python/python.exe")
    add_custom_command(TARGET SingLilt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${SEPARATION_ROOT}"
            "$<TARGET_FILE_DIR:SingLilt>/tools/separation"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/scripts/separate_vocals.py"
            "$<TARGET_FILE_DIR:SingLilt>/tools/separation/separate_vocals.py")
endif()

set(NATIVE_STAFF_ROOT "${CMAKE_BINARY_DIR}/_deps/CrispEmbed/runtime")
if(EXISTS "${NATIVE_STAFF_ROOT}/crispembed.exe")
    add_custom_command(TARGET SingLilt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${NATIVE_STAFF_ROOT}"
            "$<TARGET_FILE_DIR:SingLilt>/tools/omr-native")
endif()

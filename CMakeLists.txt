# Application and core-test targets for the single build directory.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.30)
project(SingLilt VERSION 0.26.0 LANGUAGES CXX)

# All generated material, including downloaded dependencies, stays in build/.
file(REAL_PATH "${CMAKE_SOURCE_DIR}/build" allowed_build)
file(REAL_PATH "${CMAKE_BINARY_DIR}" actual_build)
if(NOT actual_build STREQUAL allowed_build)
    message(FATAL_ERROR "Use the single build directory: build/")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_SUPPRESS_REGENERATION ON)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Configurations" FORCE)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")
set(CMAKE_VS_GLOBALS "UseMultiToolTask=true")

option(SINGLILT_BUILD_APP "Build the desktop application" ON)
include(cmake/Sources.cmake)
enable_testing()

add_executable(SingLiltCoreTests tests/CoreTests.cpp tests/SingingTests.h tests/SingingTests.cpp
    tests/NumberedPerformanceTests.h tests/NumberedPerformanceTests.cpp ${CORE_FILES})
target_include_directories(SingLiltCoreTests PRIVATE src)
set_target_properties(SingLiltCoreTests PROPERTIES FOLDER "Tests")
if(MSVC)
    target_compile_options(SingLiltCoreTests PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME SingLiltCore COMMAND SingLiltCoreTests)
set_tests_properties(SingLiltCore PROPERTIES LABELS "core;ci")
add_test(NAME SingLiltPlatformSources COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/tests/LinuxSourceSelection.cmake")
set_tests_properties(SingLiltPlatformSources PROPERTIES LABELS "core;ci")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES ${CORE_FILES})
source_group("Tests" FILES tests/CoreTests.cpp tests/SingingTests.h tests/SingingTests.cpp
    tests/NumberedPerformanceTests.h tests/NumberedPerformanceTests.cpp)

if(NOT SINGLILT_BUILD_APP)
    return()
endif()
if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Desktop support requires Windows or Linux. Use -DSINGLILT_BUILD_APP=OFF for core tests.")
endif()
if(WIN32)
    enable_language(RC)
endif()
include(cmake/Dependencies.cmake)

add_executable(SingLiltNumberedRecognitionTests tests/NumberedRecognitionTests.cpp
    ${NUMBERED_RECOGNITION_FILES} ${CORE_FILES})
target_include_directories(SingLiltNumberedRecognitionTests PRIVATE src)
target_link_libraries(SingLiltNumberedRecognitionTests PRIVATE Qt6::Gui Qt6::Network)
set_target_properties(SingLiltNumberedRecognitionTests PROPERTIES FOLDER "Tests")
if(MSVC)
    target_compile_options(SingLiltNumberedRecognitionTests PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME SingLiltNumberedRecognition COMMAND SingLiltNumberedRecognitionTests "$<TARGET_FILE:SingLilt>")
set_tests_properties(SingLiltNumberedRecognition PROPERTIES LABELS "ci" TIMEOUT 90
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(SingLilt WIN32 ${APP_FILES} ${CORE_FILES})
add_dependencies(SingLiltNumberedRecognitionTests SingLilt)
configure_file(resources/branding/Version.h.in "${CMAKE_BINARY_DIR}/generated/Version.h" @ONLY)
if(WIN32)
    target_sources(SingLilt PRIVATE resources/branding/SingLilt.rc resources/branding/singlilt.ico)
endif()
target_sources(SingLilt PRIVATE resources/branding/Version.h.in)
set_source_files_properties(resources/branding/Version.h.in PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(SingLilt PRIVATE ${RESOURCE_FILES})
set_source_files_properties(${RESOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
qt_add_resources(SingLilt ui_resources
    PREFIX "/" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources" FILES ${RESOURCE_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/resources" PREFIX "Resources" FILES ${RESOURCE_FILES})
file(GLOB_RECURSE LANGUAGE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/language/*.json")
target_sources(SingLilt PRIVATE ${LANGUAGE_FILES})
set_source_files_properties(${LANGUAGE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
source_group(TREE "${CMAKE_SOURCE_DIR}/language" PREFIX "Languages" FILES ${LANGUAGE_FILES})
target_include_directories(SingLilt PRIVATE src resources/branding "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(SingLilt PRIVATE Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::CorePrivate FluidSynthRuntime)
target_compile_definitions(SingLilt PRIVATE QT_NO_KEYWORDS SINGLILT_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_link_libraries(SingLilt PRIVATE winmm windowsapp mfplat mfreadwrite mfuuid ole32)
    target_compile_definitions(SingLilt PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_link_libraries(SingLilt PRIVATE Qt6::Multimedia ALSA::ALSA PkgConfig::Tesseract)
endif()
set_target_properties(SingLilt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:SingLilt>")
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT SingLilt)

set_target_properties(SingLilt PROPERTIES FOLDER "App")
if(MSVC)
    target_compile_options(SingLilt PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES ${APP_FILES})
include(cmake/DeployRuntime.cmake)
add_executable(SingLiltAudioDecoderTests tests/AudioDecoderProcessTests.cpp
    src/recognition/linux/AudioDecoder.h src/recognition/linux/AudioDecoder.cpp
    src/i18n/LanguageManager.h src/i18n/LanguageManager.cpp)
target_include_directories(SingLiltAudioDecoderTests PRIVATE src)
target_link_libraries(SingLiltAudioDecoderTests PRIVATE Qt6::Core)
set_target_properties(SingLiltAudioDecoderTests PROPERTIES FOLDER "Tests")
if(MSVC)
    target_compile_options(SingLiltAudioDecoderTests PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME SingLiltAudioDecoderProcess COMMAND SingLiltAudioDecoderTests)
set_tests_properties(SingLiltAudioDecoderProcess PROPERTIES LABELS "ci" TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
if(WIN32)
    include(cmake/Tests.cmake)
else()
    include(cmake/LinuxTests.cmake)
endif()

# Headless desktop checks independent of physical Linux audio devices.
# Copyright (c) 2026 Herbert Yeung
# SPDX-License-Identifier: MIT

find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
add_executable(SingLiltLanguageTests tests/LanguageTests.cpp src/i18n/LanguageManager.cpp src/i18n/LanguageManager.h)
target_include_directories(SingLiltLanguageTests PRIVATE src)
target_link_libraries(SingLiltLanguageTests PRIVATE Qt6::Core)
add_executable(SingLiltMigrationTests tests/MigrationTests.cpp src/settings/LegacyMigration.cpp src/settings/LegacyMigration.h)
target_include_directories(SingLiltMigrationTests PRIVATE src)
target_link_libraries(SingLiltMigrationTests PRIVATE Qt6::Core)
add_executable(SingLiltLinuxMediaTests tests/LinuxMediaTests.cpp
    src/recognition/linux/AudioDecoder.h src/recognition/linux/AudioDecoder.cpp
    src/recognition/linux/TextOcr.cpp src/audio/linux/OriginalAudioPlayer.cpp
    src/audio/linux/MicrophoneCapture.cpp src/audio/linux/MidiInstrument.cpp
    src/audio/SoundFontInstrument.cpp src/i18n/LanguageManager.cpp)
target_include_directories(SingLiltLinuxMediaTests PRIVATE src)
target_link_libraries(SingLiltLinuxMediaTests PRIVATE Qt6::Widgets Qt6::Multimedia ALSA::ALSA
    PkgConfig::Tesseract FluidSynthRuntime)
foreach(target SingLiltLanguageTests SingLiltMigrationTests SingLiltLinuxMediaTests)
    set_target_properties(${target} PROPERTIES FOLDER "Tests")
endforeach()

add_test(NAME SingLiltLanguages COMMAND SingLiltLanguageTests)
add_test(NAME SingLiltMigration COMMAND SingLiltMigrationTests)
add_test(NAME SingLiltLinuxMedia COMMAND SingLiltLinuxMediaTests)
add_test(NAME SingLiltVersion COMMAND SingLilt --version --language en_US)
set_tests_properties(SingLiltVersion PROPERTIES PASS_REGULAR_EXPRESSION "SingLilt ${PROJECT_VERSION}")
add_test(NAME SingLiltCli COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/CliTests.py"
    "$<TARGET_FILE:SingLilt>" "${CMAKE_BINARY_DIR}/cli-tests/$<CONFIG>" "${PROJECT_VERSION}")
add_test(NAME SingLiltExternalLanguage COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/ExternalLanguageTests.py" "$<TARGET_FILE:SingLilt>"
    "${CMAKE_BINARY_DIR}/external-language-tests/$<CONFIG>")
add_test(NAME SingLiltNativeStaffProcess COMMAND SingLilt --native-staff-process-check
    --report "${CMAKE_BINARY_DIR}/native-staff-process/$<CONFIG>/report.json")
foreach(diagnostic project-package musicxml staff-renderer)
    add_test(NAME "SingLiltLinux-${diagnostic}" COMMAND SingLilt "--${diagnostic}-check"
        --audio-backend sampled --diagnostic-no-audio --language en_US
        --report "${CMAKE_BINARY_DIR}/linux-diagnostics/$<CONFIG>/${diagnostic}.json")
endforeach()
# The catalog command is headless and does not initialize an audio endpoint.
add_test(NAME SingLiltCatalog COMMAND SingLilt --catalog-check --language en_US)
get_property(linux_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${linux_tests} PROPERTIES LABELS "ci" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

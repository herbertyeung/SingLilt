# Desktop regression registration and CI test labels.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

if(WIN32)
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh REQUIRED)
endif()
option(SINGLILT_TEST_AUDIO_OUTPUT "Exercise real audio output in desktop diagnostics" ON)
if(WIN32)
    set(diagnostic_backend system)
else()
    set(diagnostic_backend sampled)
endif()
set(diagnostic_audio_arguments)
set(localization_audio_arguments)
if(NOT SINGLILT_TEST_AUDIO_OUTPUT)
    list(APPEND diagnostic_audio_arguments --diagnostic-no-audio)
    list(APPEND localization_audio_arguments -NoAudio)
endif()
add_test(NAME SingLiltVersion COMMAND SingLilt --version --language en_US)
add_test(NAME SingLiltThemes COMMAND SingLilt --theme-check --audio-backend ${diagnostic_backend} --language en_US
    --report "${CMAKE_BINARY_DIR}/theme-tests/$<CONFIG>/report.json")
set_tests_properties(SingLiltThemes PROPERTIES LABELS "ci" TIMEOUT 120 RUN_SERIAL TRUE)
if(WIN32)
add_test(NAME SingLiltLocalization COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
    "${CMAKE_SOURCE_DIR}/scripts/verify-localization.ps1" -Executable "$<TARGET_FILE:SingLilt>"
    -OutputDirectory "${CMAKE_BINARY_DIR}/localization-tests/$<CONFIG>" ${localization_audio_arguments})
set_tests_properties(SingLiltLocalization PROPERTIES LABELS "ci" TIMEOUT 90 RUN_SERIAL TRUE)
endif()
set_tests_properties(SingLiltVersion PROPERTIES TIMEOUT 15 PASS_REGULAR_EXPRESSION "SingLilt ${PROJECT_VERSION}")
add_test(NAME SingLiltProjectPackage COMMAND SingLilt --project-package-check --audio-backend ${diagnostic_backend}
    ${diagnostic_audio_arguments} --report "${CMAKE_BINARY_DIR}/project-package/ctest/$<CONFIG>/report.json")
set_tests_properties(SingLiltProjectPackage PROPERTIES TIMEOUT 45)
set(SINGLILT_PRIVATE_FIXTURES "${CMAKE_SOURCE_DIR}/build/private-fixtures" CACHE PATH "Private recognition regression data")
if(WIN32 AND EXISTS "${SINGLILT_PRIVATE_FIXTURES}/buxia.png")
    add_test(NAME SingLiltLowerOctave
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
            "${CMAKE_SOURCE_DIR}/scripts/verify-low-octave.ps1"
            -Executable "$<TARGET_FILE:SingLilt>"
            -FixtureDirectory "${SINGLILT_PRIVATE_FIXTURES}"
            -OutputDirectory "${CMAKE_BINARY_DIR}/low-octave-fix/$<CONFIG>")
    set_tests_properties(SingLiltLowerOctave PROPERTIES
        TIMEOUT 45
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_test(NAME SingLiltVersePlayback
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
            "${CMAKE_SOURCE_DIR}/scripts/verify-verses.ps1"
            -Executable "$<TARGET_FILE:SingLilt>"
            -FixtureDirectory "${SINGLILT_PRIVATE_FIXTURES}"
            -OutputDirectory "${CMAKE_BINARY_DIR}/ab-verse-change/$<CONFIG>")
    set_tests_properties(SingLiltVersePlayback PROPERTIES
        TIMEOUT 60
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
add_test(NAME SingLiltCatalog COMMAND SingLilt --catalog-check --language en_US)
set_tests_properties(SingLiltCatalog PROPERTIES TIMEOUT 15
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
add_test(NAME SingLiltProductWorkspace COMMAND SingLilt --product-workspace-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/productization-tests/$<CONFIG>/workspace/report.json")
add_test(NAME SingLiltSettingsProduct COMMAND SingLilt --settings-product-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/productization-tests/$<CONFIG>/settings/report.json")
add_test(NAME SingLiltOptions COMMAND SingLilt --options-check --options-range-only --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/options/ctest/$<CONFIG>/report.json")
set_tests_properties(SingLiltOptions PROPERTIES TIMEOUT 60)
add_test(NAME SingLiltWaveExport COMMAND SingLilt --wave-export-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/productization-tests/$<CONFIG>/export/report.json")
add_test(NAME SingLiltHistoryRecovery COMMAND SingLilt --history-recovery-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/productization-tests/$<CONFIG>/history/report.json")
set_tests_properties(SingLiltProductWorkspace SingLiltSettingsProduct SingLiltHistoryRecovery PROPERTIES TIMEOUT 45)
set_tests_properties(SingLiltWaveExport PROPERTIES TIMEOUT 120)
add_test(NAME SingLiltStaffRenderer COMMAND SingLilt --staff-renderer-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/staff-tests/$<CONFIG>/renderer/report.json")
add_test(NAME SingLiltMusicXml COMMAND SingLilt --musicxml-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/staff-tests/$<CONFIG>/musicxml/report.json")
add_test(NAME SingLiltStaffRecognition COMMAND SingLilt --staff-recognition-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/staff-tests/$<CONFIG>/recognition/report.json")
add_test(NAME SingLiltStaffPlayback COMMAND SingLilt --staff-playback-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/staff-tests/$<CONFIG>/playback/report.json")
add_test(NAME SingLiltStaffWorkflow COMMAND SingLilt --staff-workflow-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/staff-tests/$<CONFIG>/workflow/report.json")
set_tests_properties(SingLiltStaffRenderer SingLiltMusicXml SingLiltStaffRecognition SingLiltStaffWorkflow PROPERTIES TIMEOUT 90)
set_tests_properties(SingLiltStaffPlayback PROPERTIES TIMEOUT 120)
add_test(NAME SingLiltStaffFidelity COMMAND SingLilt --staff-fidelity-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/fidelity-tests/$<CONFIG>/report.json")
set_tests_properties(SingLiltStaffFidelity PROPERTIES TIMEOUT 90)
add_test(NAME SingLiltOriginalStaffView COMMAND SingLilt --original-staff-view-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/original-staff-tests/$<CONFIG>/view/report.json")
set_tests_properties(SingLiltOriginalStaffView PROPERTIES TIMEOUT 90)
add_test(NAME SingLiltOriginalImageWorkflow COMMAND SingLilt --original-image-workflow-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/original-staff-tests/$<CONFIG>/workflow/report.json")
set_tests_properties(SingLiltOriginalImageWorkflow PROPERTIES TIMEOUT 90)
add_test(NAME SingLiltStaffAnchorCorrection COMMAND SingLilt --staff-anchor-correction-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/original-staff-tests/$<CONFIG>/anchor/report.json")
set_tests_properties(SingLiltStaffAnchorCorrection PROPERTIES TIMEOUT 90)
add_test(NAME SingLiltNativeStaffCore COMMAND SingLilt --native-staff-core-check
    --report "${CMAKE_BINARY_DIR}/crisp-omr-evaluation/$<CONFIG>/core.json")
set_tests_properties(SingLiltNativeStaffCore PROPERTIES TIMEOUT 30)
add_test(NAME SingLiltNativeStaffProcess COMMAND SingLilt --native-staff-process-check
    --report "${CMAKE_BINARY_DIR}/native-staff-process/$<CONFIG>/report.json")
set_tests_properties(SingLiltNativeStaffProcess PROPERTIES LABELS "ci" TIMEOUT 60)

add_test(NAME SingLiltStaffNoteEditing COMMAND SingLilt --staff-note-editing-check
    --report "${CMAKE_BINARY_DIR}/staff-note-editing/$<CONFIG>/core.json")
set_tests_properties(SingLiltStaffNoteEditing PROPERTIES TIMEOUT 30)

add_test(NAME SingLiltStaffNoteCanvas COMMAND SingLilt --staff-note-canvas-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/staff-note-editing/$<CONFIG>/canvas.json")
set_tests_properties(SingLiltStaffNoteCanvas PROPERTIES TIMEOUT 40)

add_test(NAME SingLiltStaffPositionMapping COMMAND SingLilt --staff-position-mapping-check
    --report "${CMAKE_BINARY_DIR}/staff-smart-editing/$<CONFIG>/position.json")
add_test(NAME SingLiltStaffEditRecovery COMMAND SingLilt --staff-edit-recovery-check
    --report "${CMAKE_BINARY_DIR}/staff-smart-editing/$<CONFIG>/recovery.json")
add_test(NAME SingLiltStaffSmartNote COMMAND SingLilt --staff-smart-note-check --audio-backend ${diagnostic_backend}
    --report "${CMAKE_BINARY_DIR}/staff-smart-editing/$<CONFIG>/smart.json")
set_tests_properties(SingLiltStaffPositionMapping SingLiltStaffEditRecovery PROPERTIES TIMEOUT 30)
set_tests_properties(SingLiltStaffSmartNote PROPERTIES TIMEOUT 60)

set_tests_properties(SingLiltVersion SingLiltCatalog SingLiltProjectPackage SingLiltMusicXml
    SingLiltStaffRenderer SingLiltNativeStaffCore SingLiltStaffNoteEditing
    SingLiltStaffPositionMapping SingLiltStaffEditRecovery PROPERTIES LABELS "ci")
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
if(WIN32)
add_test(NAME SingLiltRuntime COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/RuntimeTests.py" "$<TARGET_FILE:SingLilt>" "${PROJECT_VERSION}"
    "${CMAKE_BINARY_DIR}/runtime-tests/$<CONFIG>/report.json")
set_tests_properties(SingLiltRuntime PROPERTIES LABELS "ci" TIMEOUT 30)
endif()
add_test(NAME SingLiltCli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/CliTests.py" "$<TARGET_FILE:SingLilt>" "${CMAKE_BINARY_DIR}/cli-tests/$<CONFIG>"
    "${PROJECT_VERSION}")
set_tests_properties(SingLiltCli PROPERTIES LABELS "ci" TIMEOUT 90)

add_executable(SingLiltLanguageTests tests/LanguageTests.cpp src/i18n/LanguageManager.cpp src/i18n/LanguageManager.h src/platform/RuntimePaths.cpp)
target_include_directories(SingLiltLanguageTests PRIVATE src)
target_link_libraries(SingLiltLanguageTests PRIVATE Qt6::Core)
set_target_properties(SingLiltLanguageTests PROPERTIES FOLDER "Tests")
if(MSVC)
    target_compile_options(SingLiltLanguageTests PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME SingLiltLanguages COMMAND SingLiltLanguageTests)
set_tests_properties(SingLiltLanguages PROPERTIES LABELS "ci" TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

add_test(NAME SingLiltExternalLanguage COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/ExternalLanguageTests.py" "$<TARGET_FILE:SingLilt>"
    "${CMAKE_BINARY_DIR}/external-language-tests/$<CONFIG>")
set_tests_properties(SingLiltExternalLanguage PROPERTIES LABELS "ci" TIMEOUT 90 RUN_SERIAL TRUE)

add_executable(SingLiltMigrationTests tests/MigrationTests.cpp src/settings/LegacyMigration.cpp src/settings/LegacyMigration.h)
target_include_directories(SingLiltMigrationTests PRIVATE src)
target_link_libraries(SingLiltMigrationTests PRIVATE Qt6::Core)
set_target_properties(SingLiltMigrationTests PROPERTIES FOLDER "Tests")
if(MSVC)
    target_compile_options(SingLiltMigrationTests PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()
add_test(NAME SingLiltMigration COMMAND SingLiltMigrationTests)
set_tests_properties(SingLiltMigration PROPERTIES LABELS "ci" TIMEOUT 30
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

add_executable(SingLiltRuntimePathsTests tests/RuntimePathsTests.cpp src/platform/RuntimePaths.cpp)
target_include_directories(SingLiltRuntimePathsTests PRIVATE src)
target_link_libraries(SingLiltRuntimePathsTests PRIVATE Qt6::Core)
add_test(NAME SingLiltRuntimePaths COMMAND SingLiltRuntimePathsTests)

if(APPLE)
 add_executable(SingLiltMacAudioTests tests/MacAudioTests.cpp src/audio/QtAudioDecode.cpp src/audio/QtOriginalAudioPlayer.cpp src/i18n/LanguageManager.cpp src/platform/RuntimePaths.cpp)
 target_include_directories(SingLiltMacAudioTests PRIVATE src)
 target_link_libraries(SingLiltMacAudioTests PRIVATE Qt6::Widgets Qt6::Multimedia)
 add_test(NAME SingLiltMacAudio COMMAND SingLiltMacAudioTests)
 set_tests_properties(SingLiltMacAudio PROPERTIES TIMEOUT 45)
endif()

if(APPLE)
 add_executable(SingLiltMacOcrTests tests/MacOcrTests.cpp src/recognition/PlatformOcr.cpp src/recognition/MacOcr.mm src/i18n/LanguageManager.cpp src/platform/RuntimePaths.cpp)
 target_include_directories(SingLiltMacOcrTests PRIVATE src)
 target_link_libraries(SingLiltMacOcrTests PRIVATE Qt6::Widgets "-framework Vision" "-framework Foundation" "-framework CoreGraphics")
 add_test(NAME SingLiltMacOcr COMMAND SingLiltMacOcrTests)
 set_tests_properties(SingLiltMacOcr SingLiltMacAudio SingLiltRuntimePaths PROPERTIES LABELS "native" TIMEOUT 45)
endif()

if(APPLE)
 add_executable(SingLiltProcessTests tests/ProcessCancellationTests.cpp)
 target_include_directories(SingLiltProcessTests PRIVATE src)
 target_link_libraries(SingLiltProcessTests PRIVATE Qt6::Core)
 add_test(NAME SingLiltProcessCancellation COMMAND SingLiltProcessTests)
 set_tests_properties(SingLiltProcessCancellation PROPERTIES LABELS "native" TIMEOUT 15)
endif()

add_executable(SingLiltCaptureClockTests tests/CaptureClockTests.cpp)
target_include_directories(SingLiltCaptureClockTests PRIVATE src)
add_test(NAME SingLiltCaptureClock COMMAND SingLiltCaptureClockTests)
set_tests_properties(SingLiltCaptureClock PROPERTIES LABELS "native" TIMEOUT 15)

if(APPLE)
 add_executable(SingLiltMacMicrophoneLifecycleTests tests/MacMicrophoneLifecycleTests.cpp src/audio/QtMicrophoneCapture.cpp src/i18n/LanguageManager.cpp src/platform/RuntimePaths.cpp)
 target_include_directories(SingLiltMacMicrophoneLifecycleTests PRIVATE src)
 target_link_libraries(SingLiltMacMicrophoneLifecycleTests PRIVATE Qt6::Multimedia)
 add_test(NAME SingLiltMacMicrophoneLifecycle COMMAND SingLiltMacMicrophoneLifecycleTests)
 set_tests_properties(SingLiltMacMicrophoneLifecycle PROPERTIES LABELS "native" TIMEOUT 15)
endif()

if(APPLE)
 add_executable(SingLiltFileOpenTests tests/FileOpenHandlerTests.cpp)
 target_include_directories(SingLiltFileOpenTests PRIVATE src)
 target_link_libraries(SingLiltFileOpenTests PRIVATE Qt6::Widgets)
 add_test(NAME SingLiltFileOpen COMMAND SingLiltFileOpenTests)
 set_tests_properties(SingLiltFileOpen PROPERTIES LABELS "native" TIMEOUT 15)
endif()

if(APPLE)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/macos-audio-cancellation")
    add_test(NAME SingLiltAudioCancellation COMMAND SingLilt --audio-cancellation-check
        --audio-backend sampled --diagnostic-no-audio --language en_US
        --report "${CMAKE_BINARY_DIR}/macos-audio-cancellation/report.json")
    set_tests_properties(SingLiltAudioCancellation PROPERTIES LABELS "ci" TIMEOUT 90)
endif()

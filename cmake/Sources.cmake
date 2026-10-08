# Source and resource ownership for application and core targets.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

set(CORE_FILES
    src/audio/PitchDetector.h src/audio/PitchDetector.cpp
    src/domain/Accompaniment.h src/domain/Accompaniment.cpp
    src/domain/AccompanimentTimeline.h src/domain/AccompanimentTimeline.cpp
    src/domain/AudioSource.h src/domain/AudioSource.cpp
    src/domain/EarTraining.h src/domain/EarTraining.cpp
    src/domain/NumberedPerformance.h src/domain/NumberedPerformance.cpp
    src/domain/Score.h src/domain/Score.cpp
    src/domain/SingingAssessment.h src/domain/SingingAssessment.cpp
    src/domain/SingingLesson.h
    src/domain/StaffPerformance.h src/domain/StaffPerformance.cpp
    src/domain/Timeline.h src/domain/Timeline.cpp
)

set(APP_FILES
    src/platform/RuntimePaths.h
    src/diagnostics/ThemeCheck.h src/diagnostics/ThemeCheck.cpp
    src/ui/ThemeManager.h src/ui/ThemeManager.cpp
    src/diagnostics/NativeStaffProcessCheck.h src/diagnostics/NativeStaffProcessCheck.cpp
    src/settings/LegacyMigration.h src/settings/LegacyMigration.cpp
    src/application/StaffAnchorCorrection.h src/application/StaffAnchorCorrection.cpp
    src/application/StaffNoteEditing.h src/application/StaffNoteEditing.cpp
    src/application/StaffPositionMapping.h src/application/StaffPositionMapping.cpp
    src/application/StaffScoreCorrection.h src/application/StaffScoreCorrection.cpp
    src/application/StaffTimingMapping.h src/application/StaffTimingMapping.cpp
    src/audio/InstrumentOutput.h src/audio/InstrumentOutput.cpp
    src/audio/MicrophoneCapture.h src/audio/MicrophoneCapture.cpp
    src/audio/MidiInstrument.h src/audio/MidiInstrument.cpp
    src/audio/OriginalAudioPlayer.h src/audio/OriginalAudioPlayer.cpp
    src/audio/PlaybackEngine.h src/audio/PlaybackEngine.cpp
    src/audio/SoundFontInstrument.h src/audio/SoundFontInstrument.cpp
    src/audio/WaveRenderer.h src/audio/WaveRenderer.cpp
    src/cli/CommandLine.h src/cli/CommandLine.cpp
    src/cli/JsonReport.h src/cli/JsonReport.cpp
    src/cli/ProjectCommands.h src/cli/ProjectCommands.cpp
    src/diagnostics/AccompanimentCheck.h src/diagnostics/AccompanimentCheck.cpp
    src/diagnostics/AccompanimentPreviewCheck.h src/diagnostics/AccompanimentPreviewCheck.cpp
    src/diagnostics/AsyncRecognitionCheck.h src/diagnostics/AsyncRecognitionCheck.cpp
    src/diagnostics/AudioImportCheck.h src/diagnostics/AudioImportCheck.cpp
    src/diagnostics/AudioCancellationCheck.h src/diagnostics/AudioCancellationCheck.cpp
    src/diagnostics/BekernDecoderCheck.h src/diagnostics/BekernDecoderCheck.cpp
    src/diagnostics/ClassroomCheck.h src/diagnostics/ClassroomCheck.cpp
    src/diagnostics/CrispStaffAnchorCheck.h src/diagnostics/CrispStaffAnchorCheck.cpp
    src/diagnostics/DiagnosticCommands.h src/diagnostics/DiagnosticCommands.cpp
    src/diagnostics/HistoryRecoveryCheck.h src/diagnostics/HistoryRecoveryCheck.cpp
    src/diagnostics/LocalStaffImageCheck.h src/diagnostics/LocalStaffImageCheck.cpp
    src/diagnostics/LocalizationCheck.h src/diagnostics/LocalizationCheck.cpp
    src/diagnostics/LanguagePackCheck.h src/diagnostics/LanguagePackCheck.cpp
    src/diagnostics/MenuIconsCheck.h src/diagnostics/MenuIconsCheck.cpp
    src/diagnostics/MultiPageWorkflowCheck.h src/diagnostics/MultiPageWorkflowCheck.cpp
    src/diagnostics/MusicXmlCheck.h src/diagnostics/MusicXmlCheck.cpp
    src/diagnostics/NotePreviewCheck.h src/diagnostics/NotePreviewCheck.cpp
    src/diagnostics/OptionsCheck.h src/diagnostics/OptionsCheck.cpp
    src/diagnostics/OriginalImageWorkflowCheck.h src/diagnostics/OriginalImageWorkflowCheck.cpp
    src/diagnostics/OriginalStaffViewCheck.h src/diagnostics/OriginalStaffViewCheck.cpp
    src/diagnostics/ProductWorkspaceCheck.h src/diagnostics/ProductWorkspaceCheck.cpp
    src/diagnostics/ProjectCheck.h src/diagnostics/ProjectCheck.cpp
    src/diagnostics/SettingsProductCheck.h src/diagnostics/SettingsProductCheck.cpp
    src/diagnostics/SmokeRunner.h src/diagnostics/SmokeRunner.cpp
    src/diagnostics/StaffAnchorCorrectionCheck.h src/diagnostics/StaffAnchorCorrectionCheck.cpp
    src/diagnostics/StaffEditRecoveryCheck.h src/diagnostics/StaffEditRecoveryCheck.cpp
    src/diagnostics/StaffFidelityCheck.h src/diagnostics/StaffFidelityCheck.cpp
    src/diagnostics/StaffNoteCanvasCheck.h src/diagnostics/StaffNoteCanvasCheck.cpp
    src/diagnostics/StaffNoteEditingCheck.h src/diagnostics/StaffNoteEditingCheck.cpp
    src/diagnostics/StaffPlaybackCheck.h src/diagnostics/StaffPlaybackCheck.cpp
    src/diagnostics/StaffPositionMappingCheck.h src/diagnostics/StaffPositionMappingCheck.cpp
    src/diagnostics/StaffRecognitionCheck.h src/diagnostics/StaffRecognitionCheck.cpp
    src/diagnostics/StaffRendererCheck.h src/diagnostics/StaffRendererCheck.cpp
    src/diagnostics/StaffSmartNoteCheck.h src/diagnostics/StaffSmartNoteCheck.cpp
    src/diagnostics/StaffWorkflowCheck.h src/diagnostics/StaffWorkflowCheck.cpp
    src/diagnostics/WaveExportCheck.h src/diagnostics/WaveExportCheck.cpp
    src/diagnostics/WholeSongCheck.h src/diagnostics/WholeSongCheck.cpp
    src/i18n/LanguageManager.h src/i18n/LanguageManager.cpp
    src/main.cpp
    src/practice/PracticeSession.h src/practice/PracticeSession.cpp
    src/recognition/AudioTranscriber.h src/recognition/AudioTranscriber.cpp
    src/recognition/AudioTranscriptionTask.h src/recognition/AudioTranscriptionTask.cpp
    src/recognition/BekernDecoder.h src/recognition/BekernDecoder.cpp
    src/recognition/CloudRecognitionTask.h src/recognition/CloudRecognitionTask.cpp
    src/recognition/CloudRecognizer.h src/recognition/CloudRecognizer.cpp
    src/recognition/CrispStaffRecognizer.h src/recognition/CrispStaffRecognizer.cpp
    src/recognition/CrispStaffSourceAnchors.h src/recognition/CrispStaffSourceAnchors.cpp
    src/recognition/LocalRecognizer.h src/recognition/LocalRecognizer.cpp
    src/recognition/LocalStaffRecognitionTask.h src/recognition/LocalStaffRecognitionTask.cpp
    src/recognition/LocalStaffRecognizer.h src/recognition/LocalStaffRecognizer.cpp
    src/recognition/StaffPageInput.h
    src/recognition/StaffPageSplitter.h src/recognition/StaffPageSplitter.cpp
    src/recognition/StaffProcessJob.h
    src/recognition/StaffTempoRecognizer.h src/recognition/StaffTempoRecognizer.cpp
    src/recognition/VocalSeparator.h src/recognition/VocalSeparator.cpp
    src/recognition/WindowsOcr.h src/recognition/WindowsOcr.cpp
    src/settings/AppSettings.h src/settings/AppSettings.cpp
    src/settings/CapabilityStatus.h src/settings/CapabilityStatus.cpp
    src/storage/LessonStore.h src/storage/LessonStore.cpp
    src/storage/MusicXmlImporter.h src/storage/MusicXmlImporter.cpp
    src/storage/PracticeHistory.h src/storage/PracticeHistory.cpp
    src/storage/ProjectPackage.h src/storage/ProjectPackage.cpp
    src/storage/ProjectStore.h src/storage/ProjectStore.cpp
    src/storage/StaffEditRecovery.h src/storage/StaffEditRecovery.cpp
    src/storage/StaffPagesStore.h src/storage/StaffPagesStore.cpp
    src/storage/StaffPerformanceStore.cpp
    src/ui/AccompanimentPanel.h src/ui/AccompanimentPanel.cpp
    src/ui/ClassroomDialog.h src/ui/ClassroomDialog.cpp
    src/ui/ClassroomEarTraining.cpp
    src/ui/ClassroomLayout.cpp
    src/ui/ClassroomOptions.cpp
    src/ui/InstrumentNames.h
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/PreviewAudioSession.h src/ui/PreviewAudioSession.cpp
    src/ui/MainWindowAccompaniment.cpp
    src/ui/MainWindowAudio.cpp
    src/ui/MainWindowCloud.cpp
    src/ui/MainWindowEditing.cpp
    src/ui/MainWindowExport.cpp
    src/ui/MainWindowLayout.cpp
    src/ui/MainWindowLessons.cpp
    src/ui/MainWindowLocalStaff.cpp
    src/ui/MainWindowMenus.cpp
    src/ui/MainWindowPages.cpp
    src/ui/MainWindowProject.cpp
    src/ui/MainWindowStaff.cpp
    src/ui/MenuIcons.h src/ui/MenuIcons.cpp
    src/ui/NotationRenderer.h src/ui/NotationRenderer.cpp
    src/ui/OptionsDialog.h src/ui/OptionsDialog.cpp
    src/ui/PitchCurve.h src/ui/PitchCurve.cpp
    src/ui/PracticeScore.h src/ui/PracticeScore.cpp
    src/ui/RecognitionPreviewDialog.h src/ui/RecognitionPreviewDialog.cpp
    src/ui/ScoreView.h src/ui/ScoreView.cpp
    src/ui/StaffCorrectionDialog.h src/ui/StaffCorrectionDialog.cpp
    src/ui/StaffNoteDialog.h src/ui/StaffNoteDialog.cpp
    src/ui/StaffNoteEditor.h src/ui/StaffNoteEditor.cpp
    src/ui/StaffPageImportDialog.h src/ui/StaffPageImportDialog.cpp
    src/ui/StaffRenderer.h src/ui/StaffRenderer.cpp
)

set(RESOURCE_FILES
    resources/fonts/Bravura.otf
    resources/branding/singlilt-icon.png
    resources/styles/app.qss)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(REMOVE_ITEM APP_FILES src/audio/MicrophoneCapture.cpp src/audio/OriginalAudioPlayer.cpp
        src/audio/MidiInstrument.cpp src/recognition/WindowsOcr.cpp)
    list(APPEND APP_FILES
        src/audio/linux/MicrophoneCapture.cpp
        src/audio/linux/OriginalAudioPlayer.cpp
        src/audio/linux/MidiInstrument.cpp
        src/audio/linux/MidiRouting.h
        src/recognition/linux/TextOcr.cpp
        src/recognition/linux/AudioDecoder.h src/recognition/linux/AudioDecoder.cpp
        src/platform/LinuxRuntime.h)
endif()

// Diagnostic dispatch and isolated test preferences.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "DiagnosticCommands.h"
#include "LanguagePackCheck.h"
#include "NativeStaffProcessCheck.h"
#include "ThemeCheck.h"
#include "cli/CommandLine.h"
#include "cli/JsonReport.h"
#include "diagnostics/AccompanimentCheck.h"
#include "diagnostics/AccompanimentPreviewCheck.h"
#include "diagnostics/AsyncRecognitionCheck.h"
#include "diagnostics/AudioCancellationCheck.h"
#include "diagnostics/AudioImportCheck.h"
#include "diagnostics/BekernDecoderCheck.h"
#include "diagnostics/ClassroomCheck.h"
#include "diagnostics/CrispStaffAnchorCheck.h"
#include "diagnostics/HistoryRecoveryCheck.h"
#include "diagnostics/LocalStaffImageCheck.h"
#include "diagnostics/LocalizationCheck.h"
#include "diagnostics/MenuIconsCheck.h"
#include "diagnostics/MultiPageWorkflowCheck.h"
#include "diagnostics/MusicXmlCheck.h"
#include "diagnostics/NotePreviewCheck.h"
#include "diagnostics/OptionsCheck.h"
#include "diagnostics/OriginalImageWorkflowCheck.h"
#include "diagnostics/OriginalStaffViewCheck.h"
#include "diagnostics/ProductWorkspaceCheck.h"
#include "diagnostics/ProjectCheck.h"
#include "diagnostics/SettingsProductCheck.h"
#include "diagnostics/SmokeRunner.h"
#include "diagnostics/StaffAnchorCorrectionCheck.h"
#include "diagnostics/StaffEditRecoveryCheck.h"
#include "diagnostics/StaffFidelityCheck.h"
#include "diagnostics/StaffNoteCanvasCheck.h"
#include "diagnostics/StaffNoteEditingCheck.h"
#include "diagnostics/StaffPlaybackCheck.h"
#include "diagnostics/StaffPositionMappingCheck.h"
#include "diagnostics/StaffRecognitionCheck.h"
#include "diagnostics/StaffRendererCheck.h"
#include "diagnostics/StaffSmartNoteCheck.h"
#include "diagnostics/StaffWorkflowCheck.h"
#include "diagnostics/WaveExportCheck.h"
#include "diagnostics/WholeSongCheck.h"
#include "i18n/LanguageManager.h"
#include "recognition/LocalStaffRecognizer.h"
#include "settings/AppSettings.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTextStream>
#include <atomic>

namespace singlilt
{
bool configureDiagnosticSettings(const QStringList &rawArguments)
{
    const auto hasOption = [&rawArguments](const QString &name)
    { return hasCommandLineOption(rawArguments, name); };
    if (hasOption("--theme-check") || hasOption("--language-pack-check") || hasOption("--smoke") ||
        hasOption("--ui-localization-check") || hasOption("--async-recognition-check") ||
        hasOption("--accompaniment-check") || hasOption("--accompaniment-preview-check") ||
        hasOption("--audio-cancellation-check") || hasOption("--whole-song-check") ||
        hasOption("--audio-task-check") || hasOption("--classroom-check") || hasOption("--note-preview-check") ||
        hasOption("--instrument-check") || hasOption("--options-check") || hasOption("--options-read-check") ||
        hasOption("--project-package-check") || hasOption("--product-workspace-check") ||
        hasOption("--settings-product-check") || hasOption("--wave-export-check") ||
        hasOption("--history-recovery-check") || hasOption("--menu-icons-check") ||
        hasOption("--staff-renderer-check") || hasOption("--musicxml-check") ||
        hasOption("--staff-recognition-check") || hasOption("--staff-playback-check") ||
        hasOption("--staff-workflow-check") || hasOption("--local-staff-image-check") ||
        hasOption("--multi-page-workflow-check") || hasOption("--staff-fidelity-check") ||
        hasOption("--original-staff-view-check") || hasOption("--original-image-workflow-check") ||
        hasOption("--staff-anchor-correction-check") || hasOption("--staff-note-canvas-check") ||
        hasOption("--staff-smart-note-check"))
    {
        // Diagnostic windows must not read or modify the user's application preferences.
        QString reportPath;
        for (int i = 1; i < rawArguments.size(); ++i)
        {
            if (rawArguments[i] == "--report" && i + 1 < rawArguments.size())
                reportPath = rawArguments[i + 1];
            else if (rawArguments[i].startsWith("--report="))
                reportPath = rawArguments[i].mid(9);
        }
        if (reportPath.isEmpty())
            return false;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QFileInfo(reportPath).absolutePath() + "/settings");
        if (qEnvironmentVariableIsEmpty("JIANPU_STAFF_RECOVERY_DIRECTORY"))
            qputenv("JIANPU_STAFF_RECOVERY_DIRECTORY",
                    (QFileInfo(reportPath).absolutePath() + "/recovery").toUtf8());
    }
    return true;
}

std::optional<int> runCoreDiagnostics(const QCommandLineParser &args, LanguageManager &languages)
{
    if (args.isSet("native-staff-process-check"))
    {
        const auto report = checkNativeStaffProcess();
        if (args.isSet("report"))
        {
            QDir().mkpath(QFileInfo(args.value("report")).absolutePath());
            writeJsonReport(args.value("report"), report);
        }
        QTextStream(stdout) << "NATIVE_STAFF_PROCESS passed=" << report.value("passed").toBool() << Qt::endl;
        return report.value("passed").toBool() ? 0 : 3;
    }
    if (args.isSet("options-read-check"))
    {
        const auto settings = loadAppSettings();
        writeJsonReport(args.value("report"), {{"model", settings.vision.model},
                                               {"timeout", settings.vision.timeoutSeconds},
                                               {"tolerance", settings.centsTolerance},
                                               {"latency", settings.latencyMilliseconds},
                                               {"keyPersisted", QSettings().contains("vision/apiKey") ||
                                                                    QSettings().contains("vision/key")}});
        return 0;
    }
    if (args.isSet("catalog-check"))
    {
        const auto errors = languages.validateCatalogs();
        if (args.isSet("report"))
            writeJsonReport(args.value("report"), {{"language", languages.language()},
                                                   {"errors", QJsonArray::fromStringList(errors)},
                                                   {"passed", errors.isEmpty()}});
        return errors.isEmpty() ? 0 : 2;
    }
    if (args.isSet("staff-position-mapping-check") || args.isSet("staff-edit-recovery-check"))
    {
        const auto report =
            args.isSet("staff-position-mapping-check") ? checkStaffPositionMapping() : checkStaffEditRecovery();
        if (args.isSet("report"))
        {
            QDir().mkpath(QFileInfo(args.value("report")).absolutePath());
            writeJsonReport(args.value("report"), report);
        }
        QTextStream(stdout) << "STAFF_SMART_CORE passed=" << report.value("passed").toBool() << Qt::endl;
        return report.value("passed").toBool() ? 0 : 3;
    }
    if (args.isSet("staff-note-editing-check"))
    {
        const auto report = checkStaffNoteEditing();
        if (args.isSet("report"))
        {
            QDir().mkpath(QFileInfo(args.value("report")).absolutePath());
            writeJsonReport(args.value("report"), report);
        }
        QTextStream(stdout) << "STAFF_NOTE_EDITING passed=" << report.value("passed").toBool() << Qt::endl;
        return report.value("passed").toBool() ? 0 : 3;
    }
    if (args.isSet("native-staff-core-check"))
    {
        const auto decoder = checkBekernDecoder();
        const auto anchors = crispStaffAnchorDiagnosticReport();
        LocalStaffRecognitionOptions missing;
        missing.engineExecutable = QDir::tempPath() + "/missing-singlilt-native-engine.exe";
        const QImage image(200, 200, QImage::Format_RGB32);
        const auto failure = recognizeLocalStaff(image, "test.png", missing);
        const std::atomic_bool cancelled{true};
        const auto cancellation = recognizeLocalStaff(image, "test.png", missing, &cancelled);
        const bool passed = decoder.value("passed").toBool() && anchors.value("passed").toBool() &&
                            !failure.valid() && !failure.error.isEmpty() && cancellation.cancelled &&
                            !cancellation.project;
        const QJsonObject report{{"passed", passed},
                                 {"decoder", decoder},
                                 {"anchors", anchors},
                                 {"missingEngineRejected", !failure.valid() && !failure.error.isEmpty()},
                                 {"cancellationIsTerminal", cancellation.cancelled && !cancellation.project}};
        if (args.isSet("report"))
        {
            QDir().mkpath(QFileInfo(args.value("report")).absolutePath());
            writeJsonReport(args.value("report"), report);
        }
        QTextStream(stdout) << "NATIVE_STAFF_CORE passed=" << passed << Qt::endl;
        return passed ? 0 : 2;
    }
    return std::nullopt;
}

void runWindowDiagnostics(MainWindow &window, LanguageManager &languages, ThemeManager &themes,
                          const QCommandLineParser &args, QApplication &app)
{
    if (args.isSet("theme-check"))
        runThemeCheck(window, languages, themes, args, app);
    else if (args.isSet("language-pack-check"))
        runLanguagePackCheck(window, languages, args, app);
    else if (args.isSet("staff-smart-note-check"))
        runStaffSmartNoteCheck(window, args, app);
    else if (args.isSet("staff-note-canvas-check"))
        runStaffNoteCanvasCheck(window, args, app);
    else if (args.isSet("staff-anchor-correction-check"))
        runStaffAnchorCorrectionCheck(window, args, app);
    else if (args.isSet("original-staff-view-check"))
        runOriginalStaffViewCheck(args, app);
    else if (args.isSet("original-image-workflow-check"))
        runOriginalImageWorkflowCheck(window, args, app);
    else if (args.isSet("staff-fidelity-check"))
        runStaffFidelityCheck(window, args, app);
    else if (args.isSet("multi-page-workflow-check"))
        runMultiPageWorkflowCheck(window, args, app);
    else if (args.isSet("local-staff-image-check"))
        runLocalStaffImageCheck(window, args, app);
    else if (args.isSet("staff-workflow-check"))
        runStaffWorkflowCheck(window, languages, args, app);
    else if (args.isSet("staff-playback-check"))
        runStaffPlaybackCheck(args, app);
    else if (args.isSet("staff-renderer-check"))
        runStaffRendererCheck(args, app);
    else if (args.isSet("musicxml-check"))
        runMusicXmlCheck(args, app);
    else if (args.isSet("staff-recognition-check"))
        runStaffRecognitionCheck(window, args, app);
    else if (args.isSet("menu-icons-check"))
        runMenuIconsCheck(window, languages, args, app);
    else if (args.isSet("product-workspace-check"))
        runProductWorkspaceCheck(window, languages, args, app);
    else if (args.isSet("settings-product-check"))
        runSettingsProductCheck(args, app);
    else if (args.isSet("wave-export-check"))
        runWaveExportCheck(window, args, app);
    else if (args.isSet("history-recovery-check"))
        runHistoryRecoveryCheck(window, languages, args, app);
    else if (args.isSet("project-package-check"))
        runProjectCheck(window, languages, args, app);
    else if (args.isSet("options-check"))
        runOptionsCheck(window, args, app);
    else if (args.isSet("note-preview-check") || args.isSet("instrument-check"))
        runNotePreviewCheck(window, languages, args, app);
    else if (args.isSet("classroom-check"))
        runClassroomCheck(window, languages, args, app);
    else if (args.isSet("audio-cancellation-check"))
        runAudioCancellationCheck(window, args, app);
    else if (args.isSet("audio-task-check"))
        runAudioImportCheck(window, args, app);
    else if (args.isSet("whole-song-check"))
        runWholeSongCheck(window, args, app);
    else if (args.isSet("accompaniment-preview-check"))
        runAccompanimentPreviewCheck(window, args, app);
    else if (args.isSet("accompaniment-check"))
        runAccompanimentCheck(window, languages, args, app);
    else if (args.isSet("async-recognition-check"))
        runAsyncRecognitionCheck(window, args, app);
    else if (args.isSet("ui-localization-check"))
        runLocalizationCheck(window, languages, args, app);
    else if (args.isSet("smoke"))
        runGuiSmoke(window, args, app);
}
} // namespace singlilt

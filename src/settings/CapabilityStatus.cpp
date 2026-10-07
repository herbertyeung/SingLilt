// Local runtime, model, and sound-bank readiness checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "CapabilityStatus.h"
#include "AppSettings.h"
#include "i18n/LanguageManager.h"
#include "platform/RuntimePaths.h"
#include "recognition/AudioTranscriber.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_LINUX
#include "platform/LinuxRuntime.h"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace singlilt
{
namespace
{
QString applicationDirectory(const QString &directory)
{
    return directory.isEmpty() ? QCoreApplication::applicationDirPath() : directory;
}

void checkFile(CapabilityFileStatus &status, const QString &path)
{
    const QFileInfo file(path);
    status.checkedFiles.append(QDir::toNativeSeparators(file.absoluteFilePath()));
    if (path.isEmpty() || !file.isFile() || !file.isReadable() || file.size() <= 0)
    {
        status.filesPresent = false;
        status.missingFiles.append(path.isEmpty() ? trText("ui.capabilities.no_system_gm")
                                                  : QDir::toNativeSeparators(file.absoluteFilePath()));
    }
}

QString systemGmPath()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH]{};
    const auto count = GetSystemDirectoryW(path, MAX_PATH);
    if (count > 0 && count < MAX_PATH)
        return QDir::fromNativeSeparators(QString::fromWCharArray(path)) + "/drivers/gm.dls";
#elif defined(Q_OS_LINUX)
    return linuxGmSoundFontPath();
#endif
    return {};
}

CapabilityFileStatus whisperStatus(const QString &directory, const QString &executable, const QString &model)
{
    CapabilityFileStatus status;
    checkFile(status, executable.isEmpty() ? directory + "/tools/whisper/whisper-cli" + NativeExecutableSuffix
                                           : executable);
    checkFile(status, model.isEmpty() ? directory + "/models/ggml-base.bin" : model);
    return status;
}

CapabilityFileStatus separationStatus(const QString &directory, QString python, const QString &script,
                                      const QString &modelDirectory)
{
    CapabilityFileStatus status;
    if (python.isEmpty())
        python = qEnvironmentVariable("JIANPU_SEPARATOR_PYTHON");
    if (python.isEmpty())
        python = directory + SeparatorPythonPath;
    const auto models = modelDirectory.isEmpty() ? directory + "/tools/separation/models" : modelDirectory;
    checkFile(status, python);
    checkFile(status, script.isEmpty() ? directory + "/tools/separation/separate_vocals.py" : script);
    checkFile(status, models + "/955717e8-8726e21a.th");
    const QString metadataPath = models + "/model.json";
    checkFile(status, metadataPath);
    QFile metadata(metadataPath);
    if (metadata.open(QIODevice::ReadOnly))
    {
        const auto object =
            metadata.size() <= 65536 ? QJsonDocument::fromJson(metadata.readAll()).object() : QJsonObject{};
        if (object.value("schema").toInt() != 1 || object.value("name").toString() != "htdemucs" ||
            object.value("signature").toString() != "955717e8" ||
            object.value("file").toString() != "955717e8-8726e21a.th" ||
            object.value("version").toString() != "demucs-infer-4.2.2" ||
            object.value("sha256").toString() !=
                "8726e21a993978c7ba086d3872e7608d7d5bfca646ca4aca459ffda844faa8b4")
        {
            status.filesPresent = false;
            status.issues.append(trText("messages.capabilities.separation_metadata"));
        }
    }
    else if (!status.missingFiles.contains(QDir::toNativeSeparators(QFileInfo(metadataPath).absoluteFilePath())))
    {
        status.filesPresent = false;
        status.missingFiles.append(QDir::toNativeSeparators(QFileInfo(metadataPath).absoluteFilePath()));
    }
    return status;
}
} // namespace

CapabilityStatus inspectCapabilities(const AppSettings &settings, const QString &appDirectory)
{
    const QString directory = applicationDirectory(appDirectory);
    CapabilityStatus status;
    const QString piano = qEnvironmentVariable("JIANPU_SOUNDFONT");
    QString pianoPath = piano.isEmpty() ? directory + "/assets/soundfonts/Salamander.sf2" : piano;
#ifdef Q_OS_LINUX
    if (piano.isEmpty() && !QFileInfo(pianoPath).isFile())
        pianoPath = linuxGmSoundFontPath();
#endif
    checkFile(status.piano, pianoPath);
    QString gm = settings.gmSoundFontPath;
    if (gm.isEmpty())
        gm = qEnvironmentVariable("JIANPU_GM_SOUNDFONT");
    if (gm.isEmpty())
    {
        const QString bundled = directory + "/assets/soundfonts/GeneralUser-GS.sf2";
        gm = QFileInfo(bundled).isFile() ? bundled : systemGmPath();
    }
    checkFile(status.gm, gm);
    status.whisper = whisperStatus(directory, {}, settings.whisperModel);
    status.separation = separationStatus(directory, {}, {}, {});
    return status;
}

QString capabilityStatusText(const CapabilityFileStatus &status)
{
    if (status.filesPresent)
        return trText("ui.capabilities.files_present");
    QStringList details = status.issues;
    if (!status.missingFiles.isEmpty())
        details.append(trText("ui.capabilities.missing_files").arg(status.missingFiles.join('\n')));
    return details.join('\n');
}

QString audioImportCapabilityError(const AudioTranscriptionOptions &options, const QString &appDirectory)
{
    const QString directory = applicationDirectory(appDirectory);
    QStringList errors;
    if (options.separateVocals)
    {
        const auto status = separationStatus(directory, options.separatorPython, options.separatorScript,
                                             options.separatorModelDirectory);
        if (!status.filesPresent)
            errors.append(trText("messages.capabilities.unavailable")
                              .arg(trText("ui.capabilities.separation"), capabilityStatusText(status)));
    }
    if (options.recognizeLyrics && options.lyricsText.trimmed().isEmpty())
    {
        const auto status = whisperStatus(directory, options.whisperExecutable, options.whisperModel);
        if (!status.filesPresent)
            errors.append(trText("messages.capabilities.unavailable")
                              .arg(trText("ui.capabilities.whisper"), capabilityStatusText(status)));
    }
    return errors.join('\n');
}
} // namespace singlilt

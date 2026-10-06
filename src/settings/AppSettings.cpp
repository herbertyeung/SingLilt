// Application preferences, validation, and persistence.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AppSettings.h"
#include "CapabilityStatus.h"
#include "i18n/LanguageManager.h"
#include "storage/LessonStore.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
void require(bool valid, const char *key)
{
    if (!valid)
        throw std::runtime_error(trText(key).toStdString());
}
void requireField(bool valid, const char *field, const char *key)
{
    if (!valid)
        throw SettingsValidationError(field, trText(key));
}
bool range(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}
void existingFile(const QString &path, const char *field, const char *key)
{
    requireField(path.isEmpty() || (QFileInfo(path).isFile() && QFileInfo(path).isReadable()), field, key);
}
} // namespace

SettingsValidationError::SettingsValidationError(const char *field, const QString &message)
    : std::runtime_error(message.toStdString()), field_(field)
{
}

AppSettings::AppSettings() : language(LanguageManager::configuredDefaultLanguage()) {}

QString themeModeName(ThemeMode mode)
{
    switch (mode)
    {
    case ThemeMode::Light:
        return QStringLiteral("light");
    case ThemeMode::Dark:
        return QStringLiteral("dark");
    default:
        return QStringLiteral("system");
    }
}

ThemeMode themeModeFromName(const QString &name)
{
    if (name == "light")
        return ThemeMode::Light;
    if (name == "dark")
        return ThemeMode::Dark;
    return ThemeMode::System;
}

const char *SettingsValidationError::field() const
{
    return field_;
}

QString defaultLessonDirectory()
{
    return QCoreApplication::applicationDirPath() + "/assets/lessons";
}

AppSettings loadAppSettings()
{
    QSettings store;
    AppSettings s;
    const auto defaultLanguage = s.language;
    s.language = store.value("ui/language", s.language).toString();
    if (!LanguageManager::isInstalledLanguage(s.language))
        s.language = defaultLanguage;
    s.themeMode = themeModeFromName(store.value("ui/theme", "system").toString());
    s.startupClassroom = store.value("ui/startupClassroom", false).toBool();
    s.showMarkers = store.value("ui/showMarkers", true).toBool();
    s.lessonDirectory = store.value("practice/lessonDirectory", defaultLessonDirectory()).toString();
    s.audioBackend = store.value("audio/backend", 0).toInt();
    s.gmSoundFontPath = store.value("audio/gmSoundFontPath").toString();
    s.melodyVolume = store.value("audio/defaultMelodyVolume", 0.9).toDouble();
    s.accompanimentVolume = store.value("audio/defaultAccompanimentVolume", 0.55).toDouble();
    s.originalVolume = store.value("audio/originalVolume", 0.9).toDouble();
    s.originalSpeed = store.value("audio/originalSpeed", 1.0).toDouble();
    s.velocity = store.value("audio/defaultVelocity", 88).toInt();
    s.accentBeats = store.value("audio/defaultAccent", false).toBool();
    s.metronome = store.value("audio/metronome", false).toBool();
    s.programA = store.value("audio/defaultProgramA", 0).toInt();
    s.programB = store.value("audio/defaultProgramB", 4).toInt();
    s.microphoneId = store.value("practice/microphoneId").toString();
    s.centsTolerance = store.value("practice/centsTolerance", 50.0).toDouble();
    s.latencyMilliseconds = store.value("practice/latencyMilliseconds", 0).toInt();
    s.difficulty = store.value("practice/difficulty", 0).toInt();
    s.practiceSpeed = store.value("practice/speed", 1.0).toDouble();
    s.guide = store.value("practice/guide", 0).toInt();
    s.vision.endpoint = store.value("vision/endpoint", s.vision.endpoint).toString();
    s.vision.model = store.value("vision/model", s.vision.model).toString();
    s.vision.timeoutSeconds = store.value("vision/timeoutSeconds", s.vision.timeoutSeconds).toInt();
    s.vision.apiKey = qEnvironmentVariable("OPENAI_API_KEY");
    s.whisperModel = store.value("audio/whisperModel").toString();
    s.lyricLanguage = store.value("audio/lyricLanguage", "auto").toString();
    s.separateVocals = store.value("audio/separateVocals", true).toBool();
    s.recognizeLyrics = store.value("audio/recognizeLyrics", true).toBool();
    s.enhanceVoice = s.separateVocals && store.value("audio/enhanceVoice", false).toBool();
    return s;
}

void validateAppSettings(const AppSettings &s, const AppSettings *previous)
{
    requireField(LanguageManager::isInstalledLanguage(s.language), "optionsLanguage", "messages.options.invalid");
    requireField(s.themeMode == ThemeMode::System || s.themeMode == ThemeMode::Light ||
                     s.themeMode == ThemeMode::Dark,
                 "optionsTheme", "messages.options.invalid");
    requireField(s.audioBackend == 0 || s.audioBackend == 1, "optionsAudioBackend", "messages.options.invalid");
    requireField(range(s.melodyVolume, 0, 1), "optionsMelodyVolume", "messages.options.invalid");
    requireField(range(s.accompanimentVolume, 0, 1), "optionsAccompanimentVolume", "messages.options.invalid");
    requireField(range(s.originalVolume, 0, 1), "optionsOriginalVolume", "messages.options.invalid");
    requireField(range(s.originalSpeed, 0.25, 2), "optionsOriginalSpeed", "messages.options.invalid");
    requireField(range(s.velocity, 1, 127), "optionsVelocity", "messages.options.invalid");
    requireField(range(s.programA, 0, 127), "optionsProgramA", "messages.options.invalid");
    requireField(range(s.programB, 0, 127), "optionsProgramB", "messages.options.invalid");
    requireField(range(s.centsTolerance, 10, 100), "optionsTolerance", "messages.options.invalid");
    requireField(range(s.latencyMilliseconds, 0, 500), "optionsLatency", "messages.options.invalid");
    requireField(range(s.difficulty, 0, 2), "optionsDifficulty", "messages.options.invalid");
    requireField(range(s.practiceSpeed, 0.5, 1.5), "optionsPracticeSpeed", "messages.options.invalid");
    requireField(range(s.guide, 0, 2), "optionsGuide", "messages.options.invalid");
    if (!previous || s.vision.endpoint != previous->vision.endpoint || s.vision.model != previous->vision.model ||
        s.vision.timeoutSeconds != previous->vision.timeoutSeconds || s.vision.apiKey != previous->vision.apiKey)
    {
        const QUrl url(s.vision.endpoint.trimmed());
        requireField(url.isValid() && !url.host().isEmpty() && (url.scheme() == "http" || url.scheme() == "https"),
                     "optionsVisionEndpoint", "messages.options.ai_invalid");
        requireField(!s.vision.model.trimmed().isEmpty() && s.vision.model.size() <= 256, "optionsVisionModel",
                     "messages.options.ai_invalid");
        requireField(s.vision.timeoutSeconds >= VisionConfig::MinTimeoutSeconds &&
                         s.vision.timeoutSeconds <= VisionConfig::MaxTimeoutSeconds,
                     "visionTimeout", "messages.options.ai_invalid");
    }
    requireField(s.lyricLanguage == "auto" || s.lyricLanguage == "zh" || s.lyricLanguage == "en",
                 "optionsLyricLanguage", "messages.options.invalid");
    if (!previous || s.gmSoundFontPath != previous->gmSoundFontPath)
    {
        existingFile(s.gmSoundFontPath, "optionsGmPath", "messages.options.soundfont_invalid");
        requireField(s.gmSoundFontPath.isEmpty() ||
                         QFileInfo(s.gmSoundFontPath).suffix().compare("sf2", Qt::CaseInsensitive) == 0,
                     "optionsGmPath", "messages.options.soundfont_invalid");
    }
    if (!previous || s.whisperModel != previous->whisperModel)
        existingFile(s.whisperModel, "optionsWhisperModel", "messages.options.model_invalid");
    if (!previous || s.lessonDirectory != previous->lessonDirectory ||
        (s.startupClassroom && !previous->startupClassroom))
    {
        try
        {
            loadSingingLessons(s.lessonDirectory.isEmpty() ? defaultLessonDirectory() : s.lessonDirectory,
                               s.language);
        }
        catch (const std::exception &error)
        {
            throw SettingsValidationError("optionsCourseDirectory", QString::fromUtf8(error.what()));
        }
    }
    if (previous)
    {
        const bool sampledEnabled = s.audioBackend == 0 && previous->audioBackend != 0;
        const bool lyricsEnabled = s.recognizeLyrics && !previous->recognizeLyrics;
        const bool separationEnabled = s.separateVocals && !previous->separateVocals;
        if (sampledEnabled || lyricsEnabled || separationEnabled)
        {
            const auto status = inspectCapabilities(s);
            if (sampledEnabled && (!status.piano.filesPresent || !status.gm.filesPresent))
                throw SettingsValidationError("optionsGmPath", trText("messages.capabilities.audio_missing"));
            if (lyricsEnabled && !status.whisper.filesPresent)
                throw SettingsValidationError("optionsWhisperModel", capabilityStatusText(status.whisper));
            if (separationEnabled && !status.separation.filesPresent)
                throw SettingsValidationError("optionsSeparateVocals", capabilityStatusText(status.separation));
        }
    }
}

void saveAppSettings(const AppSettings &s, const AppSettings *previous)
{
    validateAppSettings(s, previous);
    QSettings store;
    store.setValue("ui/language", s.language);
    store.setValue("ui/theme", themeModeName(s.themeMode));
    store.setValue("ui/startupClassroom", s.startupClassroom);
    store.setValue("ui/showMarkers", s.showMarkers);
    store.setValue("practice/lessonDirectory", s.lessonDirectory);
    store.setValue("audio/backend", s.audioBackend);
    store.setValue("audio/gmSoundFontPath", s.gmSoundFontPath);
    store.setValue("audio/defaultMelodyVolume", s.melodyVolume);
    store.setValue("audio/defaultAccompanimentVolume", s.accompanimentVolume);
    store.setValue("audio/originalVolume", s.originalVolume);
    store.setValue("audio/originalSpeed", s.originalSpeed);
    store.setValue("audio/defaultVelocity", s.velocity);
    store.setValue("audio/defaultAccent", s.accentBeats);
    store.setValue("audio/metronome", s.metronome);
    store.setValue("audio/defaultProgramA", s.programA);
    store.setValue("audio/defaultProgramB", s.programB);
    store.setValue("practice/microphoneId", s.microphoneId);
    store.setValue("practice/centsTolerance", s.centsTolerance);
    store.setValue("practice/latencyMilliseconds", s.latencyMilliseconds);
    store.setValue("practice/difficulty", s.difficulty);
    store.setValue("practice/speed", s.practiceSpeed);
    store.setValue("practice/guide", s.guide);
    store.setValue("vision/endpoint", s.vision.endpoint.trimmed());
    store.setValue("vision/model", s.vision.model.trimmed());
    store.setValue("vision/timeoutSeconds", s.vision.timeoutSeconds);
    store.setValue("audio/whisperModel", s.whisperModel);
    store.setValue("audio/lyricLanguage", s.lyricLanguage);
    store.setValue("audio/separateVocals", s.separateVocals);
    store.setValue("audio/recognizeLyrics", s.recognizeLyrics);
    store.setValue("audio/enhanceVoice", s.separateVocals && s.enhanceVoice);
    // The API key stays in the application snapshot, never in preferences or scores.
    store.sync();
    require(store.status() == QSettings::NoError, "messages.options.save_failed");
}
} // namespace singlilt

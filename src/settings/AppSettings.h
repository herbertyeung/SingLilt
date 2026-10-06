// Application preferences, validation, and persistence.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/Accompaniment.h"
#include "domain/Score.h"
#include "recognition/CloudRecognizer.h"
#include <stdexcept>

namespace singlilt
{
enum class ThemeMode
{
    System,
    Light,
    Dark
};

QString themeModeName(ThemeMode mode);
ThemeMode themeModeFromName(const QString &name);

struct AppSettings
{
    AppSettings();
    QString language;
    ThemeMode themeMode = ThemeMode::System;
    bool startupClassroom = false;
    bool showMarkers = true;
    QString lessonDirectory;
    int audioBackend = 0;
    QString gmSoundFontPath;
    double melodyVolume = 0.9;
    double accompanimentVolume = 0.55;
    double originalVolume = 0.9;
    double originalSpeed = 1.0;
    int velocity = 88;
    bool accentBeats = false;
    bool metronome = false;
    int programA = 0, programB = 4;
    QString microphoneId;
    double centsTolerance = 50.0;
    int latencyMilliseconds = 0;
    int difficulty = 0;
    double practiceSpeed = 1.0;
    int guide = 0;
    VisionConfig vision;
    QString whisperModel;
    QString lyricLanguage = "auto";
    bool separateVocals = true;
    bool recognizeLyrics = true;
    bool enhanceVoice = false;
};

struct OptionsContext
{
    bool classroom = false;
    bool fullStaffPerformance = false;
    int primaryStaffProgram = 0;
    int otherStaffProgram = 0;
    Score score;
    PracticeMix mix;
    int transpose = 0;
    double speed = 1.0;
    int playbackSource = 0;
    double originalSpeed = 1.0;
    double originalVolume = 0.9;
    int accompanimentPattern = 0;
};

class SettingsValidationError final : public std::runtime_error
{
  public:
    SettingsValidationError(const char *field, const QString &message);
    const char *field() const;

  private:
    const char *field_;
};

AppSettings loadAppSettings();
void validateAppSettings(const AppSettings &settings, const AppSettings *previous = nullptr);
void saveAppSettings(const AppSettings &settings, const AppSettings *previous = nullptr);
QString defaultLessonDirectory();
} // namespace singlilt

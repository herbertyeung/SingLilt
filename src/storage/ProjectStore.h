// Project data, JSON conversion, and legacy-format loading.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "domain/Accompaniment.h"
#include "domain/AudioSource.h"
#include "domain/Score.h"
#include "domain/StaffPerformance.h"
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <memory>
#include <optional>
class QTemporaryDir;

namespace singlilt
{
enum class NotationStyle
{
    Numbered,
    Staff
};
struct ProjectPracticeSettings
{
    int transpose = 0;
    double speed = 1.0;
    bool metronome = true;
    double originalSpeed = 1.0;
    double originalVolume = .9;
    int playbackSource = 0;
    bool loopEnabled = false;
    std::int64_t loopStart = 0, loopEnd = 0;
};
struct StaffPage
{
    QString label;
    QImage sourceImage;
    QImage renderedImage;
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
};
struct Project
{
    Score score;
    QImage image;
    QStringList warnings;
    std::optional<AccompanimentArrangement> accompaniment;
    PracticeMix practiceMix;
    std::optional<AudioSourceInfo> audioSource;
    bool generatedNotation = false;
    std::optional<ProjectPracticeSettings> practiceSettings;
    QJsonObject processing;
    QMap<QString, QString> resourceNames;
    // Copies used by previews and workers keep extracted media alive.
    std::shared_ptr<QTemporaryDir> mediaDirectory;
    NotationStyle notationStyle = NotationStyle::Numbered;
    bool staffBassClef = false;
    int staffKeyFifths = 0;
    bool staffMinor = false;
    std::optional<StaffPerformance> staffPerformance;
    std::vector<StaffPage> staffPages;
    bool staffImagePlayback = false;
};
QJsonObject scoreToJson(const Score &score);
QJsonObject staffPerformanceToJson(const StaffPerformance &performance);
StaffPerformance staffPerformanceFromJson(const QJsonObject &json, const Score &score);
Score scoreFromJson(const QJsonObject &json);
QJsonObject projectToJson(const Project &project);
Project projectFromJson(const QJsonObject &json, const QImage &image, const std::vector<StaffPage> &pages = {});
void saveProject(const QString &path, const Project &project);
Project loadProject(const QString &path);
Project makePracticeScore();
} // namespace singlilt

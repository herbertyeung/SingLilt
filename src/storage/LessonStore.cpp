// Bundled lesson loading and course validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "LessonStore.h"
#include "ProjectStore.h"
#include "domain/Timeline.h"
#include "i18n/LanguageManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
[[noreturn]] void invalid(const QString &path, const QString &reason)
{
    throw std::runtime_error(trText("messages.lesson.invalid").arg(path, reason).toStdString());
}

QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        invalid(path, trText("messages.lesson.read"));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        invalid(path, error.errorString());
    return document.object();
}

std::string localized(const QJsonObject &object, const char *key, const QString &locale, const QString &path)
{
    const auto entry = object.value(key);
    QString text;
    if (entry.isString())
        text = entry.toString();
    else if (entry.isObject())
    {
        const auto translations = entry.toObject();
        for (auto it = translations.begin(); it != translations.end(); ++it)
            if (!it.value().isString())
                invalid(path, QString::fromLatin1(key));
        text = translations.value(locale).toString();
        if (text.isEmpty())
            text = translations.value("en_US").toString();
        if (text.isEmpty())
            text = translations.value("zh_CN").toString();
    }
    if (text.trimmed().isEmpty() || text.size() > 8000)
        invalid(path, QString::fromLatin1(key));
    return text.toStdString();
}

double option(const QJsonObject &practice, const char *key, double fallback, double minimum, double maximum,
              const QString &path)
{
    if (!practice.contains(key))
        return fallback;
    const auto entry = practice.value(key);
    const double number = entry.toDouble(-1.0);
    if (!entry.isDouble() || !std::isfinite(number) || number < minimum || number > maximum)
        invalid(path, QString::fromLatin1(key));
    return number;
}
} // namespace

std::vector<SingingLesson> loadSingingLessons(const QString &directory, const QString &locale)
{
    const QDir folder(directory);
    const QString root = QFileInfo(directory).canonicalFilePath();
    const QString indexPath = folder.filePath("index.json");
    const auto index = readObject(indexPath);
    const auto files = index.value("lessons").toArray();
    if (root.isEmpty() || index.value("schema").toDouble() != 1.0 || !index.value("lessons").isArray() ||
        files.isEmpty() || files.size() > 128)
        invalid(indexPath, trText("messages.lesson.index"));
    std::vector<SingingLesson> lessons;
    QSet<int> ids;
    QSet<QString> paths;
    static const QStringList exerciseNames{"direction", "same",        "degree",     "interval",
                                           "rhythm",    "single_echo", "phrase_echo"};
    for (const auto &entry : files)
    {
        const QString relative = entry.toString();
        const QString path = folder.filePath(relative);
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (!entry.isString() || relative.isEmpty() || QDir::isAbsolutePath(relative) ||
            !canonical.startsWith(root + '/', Qt::CaseInsensitive) || paths.contains(canonical))
            invalid(path, trText("messages.lesson.path"));
        paths.insert(canonical);
        const auto object = readObject(canonical);
        const double id = object.value("id").toDouble(-1.0);
        if (object.value("schema").toDouble() != 1.0 || !object.value("id").isDouble() || id < 1.0 ||
            id > 9999.0 || std::floor(id) != id || ids.contains(static_cast<int>(id)) ||
            !object.value("score").isObject() || !object.value("practice").isObject())
            invalid(path, trText("messages.lesson.fields"));
        SingingLesson lesson;
        lesson.id = static_cast<int>(id);
        ids.insert(lesson.id);
        lesson.title = localized(object, "title", locale, path);
        lesson.goal = localized(object, "goal", locale, path);
        lesson.steps = localized(object, "steps", locale, path);
        lesson.selfCheck = localized(object, "selfCheck", locale, path);
        const auto practice = object.value("practice").toObject();
        lesson.centsTolerance = option(practice, "centsTolerance", 50.0, 10.0, 100.0, path);
        lesson.timingToleranceSeconds = option(practice, "timingToleranceMs", 180.0, 50.0, 500.0, path) / 1000.0;
        const auto exercises = practice.value("earExercises").toArray();
        if (!practice.value("earExercises").isArray() || exercises.isEmpty() || exercises.size() > 7)
            invalid(path, trText("messages.lesson.exercises"));
        for (const auto &exercise : exercises)
        {
            const int type = static_cast<int>(exerciseNames.indexOf(exercise.toString()));
            if (type < 0 || !exercise.isString() ||
                std::find(lesson.earExercises.begin(), lesson.earExercises.end(),
                          static_cast<EarExercise>(type)) != lesson.earExercises.end())
                invalid(path, trText("messages.lesson.exercises"));
            lesson.earExercises.push_back(static_cast<EarExercise>(type));
        }
        auto scoreJson = object.value("score").toObject();
        if (!scoreJson.value("notes").isArray() || scoreJson.value("notes").toArray().size() > 512)
            invalid(path, trText("messages.lesson.score"));
        auto notes = scoreJson.value("notes").toArray();
        WrittenMeasure measure;
        measure.beatsPerBar = scoreJson.value("beatsPerBar").toInt(4);
        measure.beatUnit = scoreJson.value("beatUnit").toInt(4);
        const int barTicks = ticksPerBar(measure);
        if (barTicks <= 0)
            invalid(path, trText("messages.storage.invalid_meter"));
        int sourceTick = 0;
        for (int i = 0; i < notes.size(); ++i)
        {
            if (!notes[i].isObject())
                invalid(path, trText("messages.lesson.score"));
            auto note = notes[i].toObject();
            if (!note.contains("bbox"))
                note.insert("bbox", QJsonArray{1, 1, 1, 1});
            if (!note.contains("measure"))
                note.insert("measure", sourceTick / barTicks);
            if (!note.contains("confidence"))
                note.insert("confidence", 1.0);
            sourceTick += std::clamp(note.value("durationTicks").toInt(), 0, MaximumNoteDurationTicks);
            notes[i] = note;
        }
        scoreJson.insert("notes", notes);
        try
        {
            lesson.score = scoreFromJson(scoreJson);
            lesson.score.title = lesson.title;
            for (std::size_t i = 0; i < lesson.score.notes.size(); ++i)
                lesson.score.notes[i].id = static_cast<int>(i);
            const auto timeline = buildTimeline(lesson.score);
            if (!timeline.valid() || timeline.durationSeconds() > 110.0 || timeline.events.empty() ||
                std::none_of(timeline.events.begin(), timeline.events.end(),
                             [](const auto &event) { return event.midiPitch >= 0; }))
                invalid(path, trText("messages.lesson.score"));
        }
        catch (const std::exception &error)
        {
            invalid(path, QString::fromUtf8(error.what()));
        }
        lessons.push_back(std::move(lesson));
    }
    return lessons;
}
} // namespace singlilt

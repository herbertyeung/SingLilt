// Practice-summary persistence and bounded history.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "PracticeHistory.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
void validateAttempt(const QJsonObject &attempt)
{
    const QString kind = attempt.value("kind").toString();
    if ((kind != "singing" && kind != "ear") || !attempt.value("lessonId").isDouble() ||
        attempt.value("lessonId").toDouble() < 1 || attempt.value("lessonId").toDouble() > 9999 ||
        std::floor(attempt.value("lessonId").toDouble()) != attempt.value("lessonId").toDouble() ||
        !QDateTime::fromString(attempt.value("time").toString(), Qt::ISODate).isValid() ||
        !attempt.value("reliable").isBool())
        throw std::runtime_error("Invalid practice history entry");
    if (attempt.contains("attemptId") &&
        (!attempt.value("attemptId").isString() || QUuid(attempt.value("attemptId").toString()).isNull()))
        throw std::runtime_error("Invalid practice attempt identity");
    if (kind == "ear")
    {
        if (!attempt.value("correct").isBool() || !attempt.value("exercise").isDouble() ||
            attempt.value("exercise").toDouble() < 0 || attempt.value("exercise").toDouble() > 6 ||
            std::floor(attempt.value("exercise").toDouble()) != attempt.value("exercise").toDouble() ||
            !attempt.value("replays").isDouble() || attempt.value("replays").toDouble() < 1 ||
            attempt.value("replays").toDouble() > 100 || !attempt.value("difficulty").isDouble() ||
            std::floor(attempt.value("replays").toDouble()) != attempt.value("replays").toDouble() ||
            attempt.value("difficulty").toDouble() < 0 || attempt.value("difficulty").toDouble() > 2 ||
            std::floor(attempt.value("difficulty").toDouble()) != attempt.value("difficulty").toDouble())
            throw std::runtime_error("Invalid ear-training history entry");
    }
    else
    {
        for (const char *key : {"pitch", "rhythm", "coverage"})
        {
            const double value = attempt.value(key).toDouble(-1.0);
            if (!attempt.value(key).isDouble() || !std::isfinite(value) || value < 0.0 || value > 100.0)
                throw std::runtime_error("Invalid singing history entry");
        }
    }
}
} // namespace

PracticeHistory::PracticeHistory(QString path) : path_(std::move(path)) {}
void PracticeHistory::load()
{
    QFile file(path_);
    if (!file.exists())
    {
        attempts_ = {};
        return;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
        throw std::runtime_error("Practice history could not be read");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value("schema").toDouble() != 1.0 || !document.object().value("attempts").isArray())
        throw std::runtime_error("Invalid practice history file");
    const auto attempts = document.object().value("attempts").toArray();
    if (attempts.size() > 500)
        throw std::runtime_error("Practice history exceeds its entry limit");
    for (const auto &attempt : attempts)
    {
        if (!attempt.isObject())
            throw std::runtime_error("Invalid practice history entry");
        validateAttempt(attempt.toObject());
    }
    attempts_ = attempts;
}
void PracticeHistory::append(QJsonObject attempt)
{
    if (!attempt.contains("time"))
        attempt.insert("time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    validateAttempt(attempt);
    if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
        throw std::runtime_error("Practice history directory could not be created");
    QLockFile lock(path_ + ".lock");
    if (!lock.tryLock(100))
        throw std::runtime_error("Practice history is being updated by another window");
    // A second application instance may have completed an attempt since load().
    load();
    const QString attemptId = attempt.value("attemptId").toString();
    if (!attemptId.isEmpty())
        for (const auto &saved : attempts_)
            if (saved.toObject().value("attemptId").toString() == attemptId)
                return;
    auto updated = attempts_;
    updated.append(attempt);
    while (updated.size() > 500)
        updated.removeFirst();
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    const QByteArray bytes = QJsonDocument(QJsonObject{{"schema", 1}, {"attempts", updated}}).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(file.errorString().toStdString());
    attempts_ = updated;
}
} // namespace singlilt

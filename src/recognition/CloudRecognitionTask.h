// Asynchronous vision requests and reviewable candidates.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "CloudRecognizer.h"
#include <QFutureWatcher>
#include <QObject>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace singlilt
{

// State, snapshots and callbacks belong to the UI thread. The worker owns value
// copies of its inputs and shares only the atomic cancellation intent.
class CloudRecognitionTask final : public QObject
{
  public:
    enum class State
    {
        Idle,
        Running,
        Cancelling,
        Ready,
        Failed,
        Cancelled
    };

    explicit CloudRecognitionTask(QObject *parent = nullptr);
    ~CloudRecognitionTask() override;
    bool start(QImage image, QString path, QString sourceLabel, VisionConfig config,
               RecognitionNotation notation = RecognitionNotation::Numbered);
    void cancel();
    void discard();
    bool isRunning() const;
    State state() const;
    const RecognitionResult *result() const;
    const QImage &image() const;
    const QString &sourceLabel() const;
    const QString &errorString() const;

    std::function<void()> stateChanged;

  private:
    void finish();
    void notifyStateChanged();

    QFutureWatcher<RecognitionResult> watcher_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::optional<RecognitionResult> result_;
    QImage image_;
    QString sourceLabel_;
    QString error_;
    State state_ = State::Idle;
};

} // namespace singlilt

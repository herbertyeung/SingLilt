// Asynchronous audio analysis and candidate-review state.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "AudioTranscriber.h"

#include <QFutureWatcher>
#include <QObject>
#include <functional>
#include <optional>

namespace singlilt
{

// UI-thread state and immutable worker snapshots, matching CloudRecognitionTask.
class AudioTranscriptionTask final : public QObject
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

    explicit AudioTranscriptionTask(QObject *parent = nullptr);
    ~AudioTranscriptionTask() override;
    bool start(QString path, AudioTranscriptionOptions options);
    void cancel();
    void discard();
    bool isRunning() const;
    State state() const;
    const AudioTranscriptionResult *result() const;
    const QString &sourcePath() const;
    const AudioTranscriptionOptions &options() const;
    const QString &errorString() const;

    std::function<void()> stateChanged;
    AudioTranscriptionProgress progress;

  private:
    void finish();
    void notifyStateChanged();
    QFutureWatcher<AudioTranscriptionResult> watcher_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::optional<AudioTranscriptionResult> result_;
    QString sourcePath_;
    AudioTranscriptionOptions options_;
    QString error_;
    State state_ = State::Idle;
    std::uint64_t generation_ = 0;
};

} // namespace singlilt

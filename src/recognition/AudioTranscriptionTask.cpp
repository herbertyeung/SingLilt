// Asynchronous audio analysis and candidate-review state.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AudioTranscriptionTask.h"

#include <QException>
#include <QMetaObject>
#include <QtConcurrent>
#include <exception>
#include <utility>

namespace singlilt
{
namespace
{
QString failureMessage(const QUnhandledException &exception)
{
    if (const auto cause = exception.exception())
    {
        try
        {
            std::rethrow_exception(cause);
        }
        catch (const std::exception &error)
        {
            return QString::fromUtf8(error.what());
        }
        catch (...)
        {
        }
    }
    return QStringLiteral("messages.audio_transcription.failed");
}
} // namespace

AudioTranscriptionTask::AudioTranscriptionTask(QObject *parent) : QObject(parent), watcher_(this)
{
    QObject::connect(&watcher_, &QFutureWatcher<AudioTranscriptionResult>::finished, this, [this] { finish(); });
}

AudioTranscriptionTask::~AudioTranscriptionTask()
{
    stateChanged = {};
    progress = {};
    QObject::disconnect(&watcher_, nullptr, this, nullptr);
    if (cancellation_)
        cancellation_->store(true, std::memory_order_relaxed);
    try
    {
        watcher_.waitForFinished();
    }
    catch (...)
    {
        // A closing window discards worker failures and cancellation results.
    }
}

bool AudioTranscriptionTask::start(QString path, AudioTranscriptionOptions options)
{
    if (isRunning() || state_ == State::Ready)
        return false;
    sourcePath_ = std::move(path);
    options_ = std::move(options);
    result_.reset();
    error_.clear();
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++generation_;
    state_ = State::Running;
    try
    {
        watcher_.setFuture(QtConcurrent::run(
            [this, path = sourcePath_, options = options_, cancellation = cancellation_, generation]
            {
                const auto update = [this, generation](int percent, const QString &stage)
                {
                    QMetaObject::invokeMethod(
                        this,
                        [this, generation, percent, stage]
                        {
                            if (generation_ == generation && state_ == State::Running && progress)
                                progress(percent, stage);
                        },
                        Qt::QueuedConnection);
                };
                return transcribeAudio(path, options, cancellation, update);
            }));
    }
    catch (const std::exception &error)
    {
        error_ = QString::fromUtf8(error.what());
        state_ = State::Failed;
        notifyStateChanged();
        return false;
    }
    notifyStateChanged();
    return true;
}

void AudioTranscriptionTask::cancel()
{
    if (state_ != State::Running)
        return;
    cancellation_->store(true, std::memory_order_relaxed);
    state_ = State::Cancelling;
    notifyStateChanged();
}

void AudioTranscriptionTask::discard()
{
    if (isRunning() || state_ == State::Idle)
        return;
    ++generation_;
    state_ = State::Idle;
    result_.reset();
    sourcePath_.clear();
    options_ = {};
    error_.clear();
    cancellation_.reset();
    watcher_.setFuture(QFuture<AudioTranscriptionResult>());
    notifyStateChanged();
}

void AudioTranscriptionTask::finish()
{
    if (!isRunning())
        return;
    try
    {
        if (!cancellation_->load(std::memory_order_relaxed))
        {
            auto result = watcher_.result();
            if (result.cancelled)
            {
                state_ = State::Cancelled;
                error_ = QStringLiteral("messages.audio_transcription.cancelled");
            }
            else if (result.score.notes.empty())
            {
                state_ = State::Failed;
                error_ = QStringLiteral("messages.audio_transcription.no_notes");
            }
            else
            {
                result_ = std::move(result);
                state_ = State::Ready;
            }
        }
    }
    catch (const QUnhandledException &exception)
    {
        error_ = failureMessage(exception);
        state_ = State::Failed;
    }
    catch (const std::exception &error)
    {
        error_ = QString::fromUtf8(error.what());
        state_ = State::Failed;
    }
    catch (...)
    {
        error_ = QStringLiteral("messages.audio_transcription.failed");
        state_ = State::Failed;
    }
    if (cancellation_->load(std::memory_order_relaxed))
    {
        result_.reset();
        error_ = QStringLiteral("messages.audio_transcription.cancelled");
        state_ = State::Cancelled;
    }
    notifyStateChanged();
}

void AudioTranscriptionTask::notifyStateChanged()
{
    const auto callback = stateChanged;
    if (callback)
        callback();
}

bool AudioTranscriptionTask::isRunning() const
{
    return state_ == State::Running || state_ == State::Cancelling;
}
AudioTranscriptionTask::State AudioTranscriptionTask::state() const
{
    return state_;
}
const AudioTranscriptionResult *AudioTranscriptionTask::result() const
{
    return state_ == State::Ready && result_ ? &*result_ : nullptr;
}
const QString &AudioTranscriptionTask::sourcePath() const
{
    return sourcePath_;
}
const AudioTranscriptionOptions &AudioTranscriptionTask::options() const
{
    return options_;
}
const QString &AudioTranscriptionTask::errorString() const
{
    return error_;
}
} // namespace singlilt

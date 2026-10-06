// Asynchronous vision requests and reviewable candidates.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "CloudRecognitionTask.h"

#include <QException>
#include <QtConcurrent>
#include <exception>
#include <utility>

namespace singlilt
{
namespace
{
QString unhandledMessage(const QUnhandledException &exception)
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
    return QStringLiteral("messages.recognition_task.failed");
}
} // namespace

CloudRecognitionTask::CloudRecognitionTask(QObject *parent) : QObject(parent), watcher_(this)
{
    QObject::connect(&watcher_, &QFutureWatcher<RecognitionResult>::finished, this, [this] { finish(); });
}

CloudRecognitionTask::~CloudRecognitionTask()
{
    stateChanged = {};
    QObject::disconnect(&watcher_, nullptr, this, nullptr);
    if (cancellation_)
        cancellation_->store(true, std::memory_order_relaxed);
    try
    {
        watcher_.waitForFinished();
    }
    catch (...)
    {
        // Closing discards worker errors, including the cancellation exception.
    }
}

bool CloudRecognitionTask::start(QImage image, QString path, QString sourceLabel, VisionConfig config,
                                 RecognitionNotation notation)
{
    if (isRunning() || state_ == State::Ready)
        return false;

    image_ = std::move(image);
    sourceLabel_ = std::move(sourceLabel);
    result_.reset();
    error_.clear();
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    state_ = State::Running;
    try
    {
        watcher_.setFuture(QtConcurrent::run(
            [image = image_, path = std::move(path), config = std::move(config), cancellation = cancellation_,
             notation] { return recognizeCloud(image, path, config, cancellation.get(), notation); }));
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

void CloudRecognitionTask::cancel()
{
    if (state_ != State::Running)
        return;
    cancellation_->store(true, std::memory_order_relaxed);
    state_ = State::Cancelling;
    notifyStateChanged();
}

void CloudRecognitionTask::discard()
{
    if (isRunning() || state_ == State::Idle)
        return;
    state_ = State::Idle;
    result_.reset();
    image_ = {};
    sourceLabel_.clear();
    error_.clear();
    cancellation_.reset();
    watcher_.setFuture(QFuture<RecognitionResult>());
    notifyStateChanged();
}

void CloudRecognitionTask::finish()
{
    if (!isRunning())
        return;
    try
    {
        if (!cancellation_->load(std::memory_order_relaxed))
        {
            auto recognized = watcher_.result();
            if (recognized.score.notes.empty())
            {
                error_ = QStringLiteral("messages.recognition_task.empty_result");
                state_ = State::Failed;
            }
            else
            {
                result_ = std::move(recognized);
                state_ = State::Ready;
            }
        }
    }
    catch (const QUnhandledException &exception)
    {
        error_ = unhandledMessage(exception);
        state_ = State::Failed;
    }
    catch (const std::exception &error)
    {
        error_ = QString::fromUtf8(error.what());
        state_ = State::Failed;
    }
    catch (...)
    {
        error_ = QStringLiteral("messages.recognition_task.failed");
        state_ = State::Failed;
    }
    // Cancellation wins even if a successful reply was already queued for delivery.
    if (cancellation_->load(std::memory_order_relaxed))
    {
        result_.reset();
        error_ = QStringLiteral("messages.recognition_task.cancelled");
        state_ = State::Cancelled;
    }
    notifyStateChanged();
}

void CloudRecognitionTask::notifyStateChanged()
{
    const auto callback = stateChanged;
    if (callback)
        callback();
}

bool CloudRecognitionTask::isRunning() const
{
    return state_ == State::Running || state_ == State::Cancelling;
}

CloudRecognitionTask::State CloudRecognitionTask::state() const
{
    return state_;
}

const RecognitionResult *CloudRecognitionTask::result() const
{
    return state_ == State::Ready && result_ ? &*result_ : nullptr;
}

const QImage &CloudRecognitionTask::image() const
{
    return image_;
}

const QString &CloudRecognitionTask::sourceLabel() const
{
    return sourceLabel_;
}

const QString &CloudRecognitionTask::errorString() const
{
    return error_;
}

} // namespace singlilt

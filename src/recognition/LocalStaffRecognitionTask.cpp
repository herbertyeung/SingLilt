// Asynchronous local staff recognition and cancellation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "LocalStaffRecognitionTask.h"

#include <QException>
#include <QFileInfo>
#include <QtConcurrent>
#include <exception>
#include <utility>

namespace singlilt
{
namespace
{
QString localStaffFailure(const QUnhandledException &exception)
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
    return QStringLiteral("messages.local_staff.failed");
}
} // namespace

LocalStaffRecognitionTask::LocalStaffRecognitionTask(QObject *parent) : QObject(parent), watcher_(this)
{
    connect(&watcher_, &QFutureWatcher<LocalStaffRecognitionResult>::finished, this, [this] { finish(); });
}

LocalStaffRecognitionTask::~LocalStaffRecognitionTask()
{
    stateChanged = {};
    disconnect(&watcher_, nullptr, this, nullptr);
    if (cancellation_)
        cancellation_->store(true, std::memory_order_relaxed);
    try
    {
        watcher_.waitForFinished();
    }
    catch (...)
    {
        // Closing discards errors after the worker has stopped its process tree.
    }
}

bool LocalStaffRecognitionTask::start(QImage image, QString path, QString sourceLabel,
                                      LocalStaffRecognitionOptions options)
{
    const auto rectangle = image.rect();
    std::vector<StaffPageInput> pages{{std::move(image), QFileInfo(path).fileName(), 0, rectangle}};
    return startInputs(std::move(pages), std::move(sourceLabel), std::move(options), true);
}

bool LocalStaffRecognitionTask::startPages(std::vector<StaffPageInput> pages, QString sourceLabel,
                                           LocalStaffRecognitionOptions options)
{
    return startInputs(std::move(pages), std::move(sourceLabel), std::move(options), false);
}

bool LocalStaffRecognitionTask::startInputs(std::vector<StaffPageInput> pages, QString sourceLabel,
                                            LocalStaffRecognitionOptions options, bool legacySingle)
{
    if (isRunning() || state_ == State::Ready)
        return false;
    pageInputs_ = std::move(pages);
    image_ = pageInputs_.empty() ? QImage{} : pageInputs_.front().image;
    sourceLabel_ = std::move(sourceLabel);
    error_.clear();
    engineLog_.clear();
    result_.reset();
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    state_ = State::Running;
    try
    {
        watcher_.setFuture(QtConcurrent::run(
            [pages = pageInputs_, options = std::move(options), cancellation = cancellation_, legacySingle]
            {
                if (legacySingle && pages.size() == 1)
                    return recognizeLocalStaff(pages.front().image, pages.front().label, options,
                                               cancellation.get());
                return recognizeLocalStaffPages(pages, options, cancellation.get());
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

void LocalStaffRecognitionTask::cancel()
{
    if (state_ != State::Running)
        return;
    cancellation_->store(true, std::memory_order_relaxed);
    state_ = State::Cancelling;
    notifyStateChanged();
}

void LocalStaffRecognitionTask::discard()
{
    if (isRunning() || state_ == State::Idle)
        return;
    state_ = State::Idle;
    result_.reset();
    image_ = {};
    pageInputs_.clear();
    sourceLabel_.clear();
    error_.clear();
    engineLog_.clear();
    cancellation_.reset();
    watcher_.setFuture(QFuture<LocalStaffRecognitionResult>());
    notifyStateChanged();
}

void LocalStaffRecognitionTask::finish()
{
    if (!isRunning())
        return;
    try
    {
        if (!cancellation_->load(std::memory_order_relaxed))
        {
            auto recognized = watcher_.result();
            engineLog_ = recognized.engineLog;
            if (recognized.cancelled)
            {
                state_ = State::Cancelled;
                error_ = QStringLiteral("messages.local_staff.cancelled");
            }
            else if (!recognized.valid())
            {
                state_ = State::Failed;
                error_ =
                    recognized.error.isEmpty() ? QStringLiteral("messages.local_staff.failed") : recognized.error;
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
        state_ = State::Failed;
        error_ = localStaffFailure(exception);
    }
    catch (const std::exception &error)
    {
        state_ = State::Failed;
        error_ = QString::fromUtf8(error.what());
    }
    catch (...)
    {
        state_ = State::Failed;
        error_ = QStringLiteral("messages.local_staff.failed");
    }
    if (cancellation_->load(std::memory_order_relaxed))
    {
        result_.reset();
        state_ = State::Cancelled;
        error_ = QStringLiteral("messages.local_staff.cancelled");
    }
    notifyStateChanged();
}

void LocalStaffRecognitionTask::notifyStateChanged()
{
    const auto notify = stateChanged;
    if (notify)
        notify();
}

bool LocalStaffRecognitionTask::isRunning() const
{
    return state_ == State::Running || state_ == State::Cancelling;
}
LocalStaffRecognitionTask::State LocalStaffRecognitionTask::state() const
{
    return state_;
}
const LocalStaffRecognitionResult *LocalStaffRecognitionTask::result() const
{
    return state_ == State::Ready && result_ ? &*result_ : nullptr;
}
const QImage &LocalStaffRecognitionTask::image() const
{
    return image_;
}
const std::vector<StaffPageInput> &LocalStaffRecognitionTask::pageInputs() const
{
    return pageInputs_;
}
const QString &LocalStaffRecognitionTask::sourceLabel() const
{
    return sourceLabel_;
}
const QString &LocalStaffRecognitionTask::errorString() const
{
    return error_;
}
const QString &LocalStaffRecognitionTask::engineLog() const
{
    return engineLog_;
}
} // namespace singlilt

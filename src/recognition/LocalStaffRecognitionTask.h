// Asynchronous local staff recognition and cancellation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "LocalStaffRecognizer.h"
#include <QFutureWatcher>
#include <QObject>
#include <functional>
#include <memory>

namespace singlilt
{
// UI-thread state; the worker receives value snapshots and only shares cancellation.
class LocalStaffRecognitionTask final : public QObject
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

    explicit LocalStaffRecognitionTask(QObject *parent = nullptr);
    ~LocalStaffRecognitionTask() override;
    bool start(QImage image, QString path, QString sourceLabel, LocalStaffRecognitionOptions options = {});
    bool startPages(std::vector<StaffPageInput> pages, QString sourceLabel,
                    LocalStaffRecognitionOptions options = {});
    void cancel();
    void discard();
    bool isRunning() const;
    State state() const;
    const LocalStaffRecognitionResult *result() const;
    const QImage &image() const;
    const std::vector<StaffPageInput> &pageInputs() const;
    const QString &sourceLabel() const;
    const QString &errorString() const;
    const QString &engineLog() const;

    std::function<void()> stateChanged;

  private:
    bool startInputs(std::vector<StaffPageInput> pages, QString sourceLabel, LocalStaffRecognitionOptions options,
                     bool legacySingle);
    void finish();
    void notifyStateChanged();
    QFutureWatcher<LocalStaffRecognitionResult> watcher_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::optional<LocalStaffRecognitionResult> result_;
    QImage image_;
    std::vector<StaffPageInput> pageInputs_;
    QString sourceLabel_;
    QString error_;
    QString engineLog_;
    State state_ = State::Idle;
};
} // namespace singlilt

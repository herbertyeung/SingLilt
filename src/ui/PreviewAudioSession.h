// Dialog-owned original-audio preview and transport restoration.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "audio/OriginalAudioPlayer.h"
#include <QObject>
#include <QTimer>
#include <cstdint>
#include <functional>
#include <string>
class QDialog;
namespace singlilt
{
class MainWindow;
class PreviewAudioSession final : public QObject
{
  public:
    PreviewAudioSession(MainWindow &window, std::function<bool()> intent, std::function<void(bool)> setIntent,
                        std::function<void(const QString &)> reportError, QDialog *dialog);
    OriginalAudioPlayer &audio();
    QString errorString() const;
    bool requestPlay(const QString &path, double startSeconds);
    void finish();
    void abandon();

  private:
    bool acceptedTransportUnchanged() const;
    MainWindow &window_;
    OriginalAudioPlayer audio_;
    QTimer stateTimer_;
    std::function<bool()> intent_;
    std::function<void(bool)> setIntent_;
    std::function<void(const QString &)> reportError_;
    std::string fingerprint_;
    QString originalPath_;
    QString projectSourcePath_;
    QString error_;
    qint64 imageIdentity_ = 0;
    std::int64_t pausedTick_ = 0;
    double pausedSeconds_ = 0;
    int selection_ = 0;
    bool active_ = false, wasMelodyPlaying_ = false, wasOriginalPlaying_ = false, previousIntent_ = false;
};
} // namespace singlilt

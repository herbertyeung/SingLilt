// Dialog-owned original-audio preview and transport restoration.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "PreviewAudioSession.h"
#include "ui/MainWindow.h"
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QPushButton>
#include <QSlider>
#include <cmath>
#include <utility>
namespace singlilt
{
PreviewAudioSession::PreviewAudioSession(MainWindow &window, std::function<bool()> intent,
                                         std::function<void(bool)> setIntent,
                                         std::function<void(const QString &)> reportError, QDialog *dialog)
    : QObject(dialog), window_(window), intent_(std::move(intent)), setIntent_(std::move(setIntent)),
      reportError_(std::move(reportError))
{
    stateTimer_.setInterval(50);
    connect(&stateTimer_, &QTimer::timeout, this,
            [this]
            {
                if (!active_)
                    return;
                setProperty("auditionPlaying", audio_.isPlaying());
                setProperty("auditionPositionSeconds", audio_.positionSeconds());
            });
    connect(dialog, &QDialog::finished, this, [this] { finish(); });
    for (const char *name : {"play", "stop"})
        if (auto *button = window.findChild<QPushButton *>(name))
            connect(button, &QPushButton::clicked, this, [this] { abandon(); });
    if (auto *source = window.findChild<QComboBox *>("playbackSource"))
        connect(source, &QComboBox::currentIndexChanged, this, [this] { abandon(); });
    if (auto *volume = window.findChild<QSlider *>("originalVolume"))
        connect(volume, &QSlider::valueChanged, this,
                [this](int value)
                {
                    if (active_ && !audio_.setVolume(value / 100.0))
                        reportError_(audio_.errorString());
                });
    if (auto *speed = window.findChild<QDoubleSpinBox *>("originalSpeed"))
        connect(speed, &QDoubleSpinBox::valueChanged, this,
                [this](double value)
                {
                    if (active_ && !audio_.setSpeed(value))
                        reportError_(audio_.errorString());
                });
}

OriginalAudioPlayer &PreviewAudioSession::audio()
{
    return audio_;
}

QString PreviewAudioSession::errorString() const
{
    return error_;
}

bool PreviewAudioSession::requestPlay(const QString &path, double startSeconds)
{
    error_.clear();
    audio_.cancelOpen();
    setProperty("auditionLoading", false);
    if (!audio_.isOpen() ||
        QFileInfo(audio_.sourcePath()).absoluteFilePath() != QFileInfo(path).absoluteFilePath())
    {
        if (audio_.isOpen())
            audio_.pause();
        stateTimer_.stop();
        setProperty("auditionLoading", true);
        setProperty("auditionPlaying", false);
        const auto fingerprint = audioTimingFingerprint(window_.project().score);
        const auto image = window_.project().image.cacheKey();
        audio_.openAsync(path, this,
                         [this, path, startSeconds, fingerprint, image](bool opened)
                         {
                             setProperty("auditionLoading", false);
                             if (fingerprint != audioTimingFingerprint(window_.project().score) ||
                                 image != window_.project().image.cacheKey())
                             {
                                 audio_.close();
                                 setProperty("auditionLoading", false);
                                 return;
                             }
                             if (!opened || !requestPlay(path, startSeconds))
                                 reportError_(audio_.errorString().isEmpty() ? error_ : audio_.errorString());
                         });
        return true;
    }
    if (!active_ || !acceptedTransportUnchanged())
    {
        wasMelodyPlaying_ = window_.player().isPlaying();
        wasOriginalPlaying_ = window_.originalAudioPlayer().isPlaying();
        previousIntent_ = intent_();
        const auto *source = window_.findChild<QComboBox *>("playbackSource");
        selection_ = source ? source->currentData().toInt() : 0;
        fingerprint_ = audioTimingFingerprint(window_.project().score);
        imageIdentity_ = window_.project().image.cacheKey();
        projectSourcePath_ = window_.project().audioSource
                                 ? QString::fromStdString(window_.project().audioSource->path)
                                 : QString();
        originalPath_ = window_.originalAudioPlayer().sourcePath();
    }
    window_.player().pause();
    if (window_.originalAudioPlayer().isOpen())
        window_.originalAudioPlayer().pause();
    pausedTick_ = window_.player().positionTicks();
    pausedSeconds_ = window_.originalAudioPlayer().positionSeconds();
    setIntent_(false);
    active_ = true;
    const auto *speed = window_.findChild<QDoubleSpinBox *>("originalSpeed");
    const auto *volume = window_.findChild<QSlider *>("originalVolume");
    const bool ok = audio_.setSpeed(speed ? speed->value() : 1.0) &&
                    audio_.setVolume(volume ? volume->value() / 100.0 : .9) && audio_.seek(startSeconds) &&
                    audio_.play();
    setProperty("auditionSourcePath", audio_.sourcePath());
    setProperty("auditionPlaying", ok && audio_.isPlaying());
    setProperty("auditionPositionSeconds", audio_.positionSeconds());
    if (ok)
        stateTimer_.start();
    if (!ok)
    {
        error_ = audio_.errorString();
        finish();
    }
    return ok;
}

void PreviewAudioSession::finish()
{
    stateTimer_.stop();
    audio_.close();
    setProperty("auditionLoading", false);
    setProperty("auditionPlaying", false);
    setProperty("auditionPositionSeconds", 0.0);
    if (active_ && acceptedTransportUnchanged())
    {
        bool restored = true;
        if (wasOriginalPlaying_)
            restored = window_.originalAudioPlayer().play();
        else if (wasMelodyPlaying_)
            restored = window_.player().play();
        setIntent_(restored && previousIntent_);
        if (!restored)
        {
            const QString restoreError =
                wasOriginalPlaying_ ? window_.originalAudioPlayer().errorString() : window_.player().errorString();
            error_ = error_.isEmpty() ? restoreError : error_ + '\n' + restoreError;
            reportError_(error_);
        }
    }
    active_ = false;
}

void PreviewAudioSession::abandon()
{
    // Initial import preloading does not own the main transport.
    if (!active_ && !property("auditionLoading").toBool())
        return;
    stateTimer_.stop();
    audio_.close();
    setProperty("auditionLoading", false);
    setProperty("auditionPlaying", false);
    setProperty("auditionPositionSeconds", 0.0);
    active_ = false;
}

bool PreviewAudioSession::acceptedTransportUnchanged() const
{
    const auto *source = window_.findChild<QComboBox *>("playbackSource");
    return !window_.player().isPlaying() && !window_.originalAudioPlayer().isPlaying() &&
           (!source || source->currentData().toInt() == selection_) &&
           audioTimingFingerprint(window_.project().score) == fingerprint_ &&
           window_.project().image.cacheKey() == imageIdentity_ &&
           (window_.project().audioSource ? QString::fromStdString(window_.project().audioSource->path)
                                          : QString()) == projectSourcePath_ &&
           window_.originalAudioPlayer().sourcePath() == originalPath_ &&
           window_.player().positionTicks() == pausedTick_ &&
           std::abs(window_.originalAudioPlayer().positionSeconds() - pausedSeconds_) < .03;
}
} // namespace singlilt

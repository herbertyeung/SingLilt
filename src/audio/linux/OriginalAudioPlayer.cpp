// UI-thread-owned Linux original audio playback through Qt Multimedia.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "audio/OriginalAudioPlayer.h"
#include "i18n/LanguageManager.h"
#include <QAudioOutput>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <cmath>

namespace singlilt
{
struct OriginalAudioPlayer::Impl
{
    QAudioOutput output;
    QMediaPlayer player;
    QString path;
    QString error;
    double duration = 0.0;
    double rate = 1.0;
    double volume = 0.9;
    QTimer timeout;
    std::unique_ptr<Impl> pending;
    QPointer<QObject> context;
    std::function<void(bool)> completion;

    Impl()
    {
        player.setAudioOutput(&output);
        QObject::connect(&player, &QMediaPlayer::errorOccurred, &player,
                         [this](QMediaPlayer::Error, const QString &message) { error = message; });
    }

    ~Impl()
    {
        QObject::disconnect(&player, nullptr, &player, nullptr);
        QObject::disconnect(&player, nullptr, QCoreApplication::instance(), nullptr);
        timeout.stop();
        player.stop();
    }

    bool ready()
    {
        if (!path.isEmpty() && player.error() == QMediaPlayer::NoError)
            return true;
        if (error.isEmpty())
            error = trText("messages.audio_source.not_open");
        return false;
    }
};

OriginalAudioPlayer::OriginalAudioPlayer() : impl_(std::make_unique<Impl>()) {}
OriginalAudioPlayer::~OriginalAudioPlayer() = default;

bool OriginalAudioPlayer::open(const QString &path)
{
    QEventLoop wait;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &wait, &QEventLoop::quit);
    bool completed = false;
    bool opened = false;
    openAsync(path, &wait,
              [&](bool success)
              {
                  completed = true;
                  opened = success;
                  wait.quit();
              });
    if (!completed)
    {
        deadline.start(16000);
        wait.exec();
    }
    if (!completed)
    {
        impl_->pending.reset();
        impl_->completion = {};
        impl_->error = trText("messages.audio_transcription.decode_failed");
    }
    return completed && opened;
}

void OriginalAudioPlayer::openAsync(const QString &path, QObject *context, std::function<void(bool)> completion)
{
    impl_->pending.reset();
    impl_->completion = {};
    const QPointer<QObject> receiver(context);
    if (!receiver)
        return;
    const QFileInfo file(path);
    if (!file.isFile())
    {
        impl_->error = trText("messages.audio_source.missing_file").arg(path);
        if (completion)
            completion(false);
        return;
    }
    if (isOpen() && impl_->path == file.absoluteFilePath())
    {
        if (completion)
            completion(true);
        return;
    }
    auto candidate = std::make_unique<Impl>();
    if (!candidate->player.isAvailable())
    {
        impl_->error = trText("messages.audio_source.media_backend_missing");
        if (completion)
            completion(false);
        return;
    }
    candidate->volume = impl_->volume;
    candidate->output.setVolume(static_cast<float>(candidate->volume));
    candidate->path = file.absoluteFilePath();
    impl_->context = receiver;
    impl_->completion = std::move(completion);
    impl_->pending = std::move(candidate);
    const QPointer<QMediaPlayer> guard(&impl_->pending->player);
    const auto finish = [this, guard]
    {
        if (!guard || !impl_->pending || &impl_->pending->player != guard.data())
            return;
        auto candidate = std::move(impl_->pending);
        candidate->timeout.stop();
        QObject::disconnect(&candidate->player, nullptr, QCoreApplication::instance(), nullptr);
        QObject::disconnect(&candidate->timeout, nullptr, QCoreApplication::instance(), nullptr);
        const auto context = impl_->context;
        auto completion = std::move(impl_->completion);
        if (!context)
            return;
        candidate->duration = candidate->player.duration() / 1000.0;
        const bool opened = candidate->player.mediaStatus() == QMediaPlayer::LoadedMedia &&
                            candidate->player.error() == QMediaPlayer::NoError && candidate->duration > 0 &&
                            candidate->duration <= 1200;
        if (opened)
        {
            candidate->volume = impl_->volume;
            candidate->output.setVolume(static_cast<float>(candidate->volume));
            impl_ = std::move(candidate);
        }
        else
        {
            impl_->error = candidate->duration > 1200
                               ? trText("messages.audio_transcription.duration_limit")
                               : (candidate->error.isEmpty() ? trText("messages.audio_transcription.decode_failed")
                                                             : candidate->error);
        }
        if (context && completion)
            completion(opened);
    };
    // Complete outside the media backend's signal stack before destroying a rejected candidate.
    const auto schedule = [finish] { QTimer::singleShot(0, QCoreApplication::instance(), finish); };
    auto &pending = *impl_->pending;
    QObject::connect(&pending.player, &QMediaPlayer::errorOccurred, QCoreApplication::instance(), schedule);
    QObject::connect(&pending.player, &QMediaPlayer::mediaStatusChanged, QCoreApplication::instance(),
                     [schedule](QMediaPlayer::MediaStatus status)
                     {
                         if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::InvalidMedia)
                             schedule();
                     });
    pending.timeout.setSingleShot(true);
    QObject::connect(&pending.timeout, &QTimer::timeout, QCoreApplication::instance(), schedule);
    pending.timeout.start(15000);
    pending.player.setSource(QUrl::fromLocalFile(pending.path));
}

bool OriginalAudioPlayer::isLoading() const
{
    return bool(impl_->pending);
}

void OriginalAudioPlayer::close()
{
    impl_->pending.reset();
    impl_->completion = {};
    impl_->player.stop();
    impl_->player.setSource({});
    impl_->path.clear();
    impl_->duration = 0;
    impl_->rate = 1;
    impl_->player.setPlaybackRate(1);
}

bool OriginalAudioPlayer::isOpen() const
{
    return !impl_->path.isEmpty() && impl_->player.error() == QMediaPlayer::NoError;
}

bool OriginalAudioPlayer::play()
{
    if (!impl_->ready())
        return false;
    if (positionSeconds() >= impl_->duration)
        impl_->player.setPosition(0);
    impl_->error.clear();
    impl_->player.play();
    return impl_->player.error() == QMediaPlayer::NoError;
}

bool OriginalAudioPlayer::pause()
{
    if (!impl_->ready())
        return false;
    impl_->player.pause();
    return true;
}

bool OriginalAudioPlayer::stop()
{
    if (!impl_->ready())
        return false;
    impl_->player.stop();
    impl_->player.setPosition(0);
    return true;
}

bool OriginalAudioPlayer::seek(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0 || seconds > impl_->duration)
    {
        impl_->error = trText("messages.audio_source.invalid_position");
        return false;
    }
    if (!impl_->ready())
        return false;
    impl_->player.setPosition(static_cast<qint64>(std::llround(seconds * 1000)));
    return true;
}

bool OriginalAudioPlayer::setSpeed(double factor)
{
    if (!std::isfinite(factor) || factor < 0.25 || factor > 2)
    {
        impl_->error = trText("messages.audio.invalid_speed");
        return false;
    }
    if (!impl_->ready())
        return false;
    impl_->player.setPlaybackRate(factor);
    impl_->rate = factor;
    return true;
}

bool OriginalAudioPlayer::setVolume(double volume)
{
    if (!std::isfinite(volume) || volume < 0 || volume > 1)
    {
        impl_->error = trText("messages.audio.invalid_volume");
        return false;
    }
    impl_->volume = volume;
    impl_->output.setVolume(static_cast<float>(volume));
    return true;
}

bool OriginalAudioPlayer::isPlaying() const
{
    return isOpen() && impl_->player.playbackState() == QMediaPlayer::PlayingState;
}
double OriginalAudioPlayer::positionSeconds() const
{
    return isOpen() ? impl_->player.position() / 1000.0 : 0.0;
}
double OriginalAudioPlayer::durationSeconds() const
{
    return impl_->duration;
}
double OriginalAudioPlayer::speed() const
{
    return impl_->rate;
}
QString OriginalAudioPlayer::sourcePath() const
{
    return impl_->path;
}
QString OriginalAudioPlayer::errorString() const
{
    return impl_->error;
}
} // namespace singlilt

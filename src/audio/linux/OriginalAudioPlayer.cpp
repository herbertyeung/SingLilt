// UI-thread-owned Linux original audio playback through Qt Multimedia.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "audio/OriginalAudioPlayer.h"
#include "i18n/LanguageManager.h"
#include <QAudioOutput>
#include <QEventLoop>
#include <QFileInfo>
#include <QMediaPlayer>
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

    Impl()
    {
        player.setAudioOutput(&output);
        QObject::connect(&player, &QMediaPlayer::errorOccurred, &player,
                         [this](QMediaPlayer::Error, const QString &message) { error = message; });
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
    const QFileInfo file(path);
    if (!file.isFile())
    {
        impl_->error = trText("messages.audio_source.missing_file").arg(path);
        return false;
    }
    if (isOpen() && impl_->path == file.absoluteFilePath())
        return true;
    auto candidate = std::make_unique<Impl>();
    candidate->volume = impl_->volume;
    candidate->output.setVolume(static_cast<float>(candidate->volume));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&candidate->player, &QMediaPlayer::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&candidate->player, &QMediaPlayer::mediaStatusChanged, &loop,
                     [&](QMediaPlayer::MediaStatus status)
                     {
                         if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::InvalidMedia)
                             loop.quit();
                     });
    timeout.start(15000);
    candidate->player.setSource(QUrl::fromLocalFile(file.absoluteFilePath()));
    if (candidate->player.mediaStatus() != QMediaPlayer::LoadedMedia &&
        candidate->player.error() == QMediaPlayer::NoError)
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    candidate->duration = candidate->player.duration() / 1000.0;
    if (candidate->player.mediaStatus() != QMediaPlayer::LoadedMedia || candidate->duration <= 0 ||
        candidate->duration > 1200 || candidate->player.error() != QMediaPlayer::NoError)
    {
        impl_->error =
            candidate->error.isEmpty() ? trText("messages.audio_transcription.decode_failed") : candidate->error;
        return false;
    }
    candidate->path = file.absoluteFilePath();
    impl_ = std::move(candidate);
    return true;
}

void OriginalAudioPlayer::close()
{
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

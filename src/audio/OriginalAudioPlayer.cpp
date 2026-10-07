// Original audio playback, seeking, and transport state.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "OriginalAudioPlayer.h"
#include "i18n/LanguageManager.h"

#include <QFileInfo>
#include <QPointer>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <Windows.h>
#include <mmsystem.h>
// digitalv.h extends the MCI types declared by mmsystem.h.
#include <digitalv.h>
#endif

namespace singlilt
{
struct OriginalAudioPlayer::Impl
{
    QString path;
    mutable QString error;
    double duration = 0.0;
    double rate = 1.0;
    double volume = 0.9;
#ifdef _WIN32
    MCIDEVICEID device = 0;

    bool check(MCIERROR status, const char *operation) const
    {
        if (status == 0)
            return true;
        wchar_t description[256]{};
        mciGetErrorStringW(status, description, 256);
        error = trText("messages.audio_source.operation_failed")
                    .arg(QString::fromLatin1(operation))
                    .arg(status)
                    .arg(QString::fromWCharArray(description));
        return false;
    }

    bool ready() const
    {
        if (device != 0)
            return true;
        error = trText("messages.audio_source.not_open");
        return false;
    }

    DWORD_PTR status(DWORD item) const
    {
        if (!ready())
            return 0;
        MCI_STATUS_PARMS parameters{};
        parameters.dwItem = item;
        if (!check(mciSendCommandW(device, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT,
                                   reinterpret_cast<DWORD_PTR>(&parameters)),
                   "status"))
            return 0;
        return parameters.dwReturn;
    }
#endif
};

OriginalAudioPlayer::OriginalAudioPlayer() : impl_(std::make_unique<Impl>()) {}
OriginalAudioPlayer::~OriginalAudioPlayer()
{
    close();
}

void OriginalAudioPlayer::openAsync(const QString &path, QObject *context, std::function<void(bool)> completion)
{
    const QPointer<QObject> receiver(context);
    if (!receiver)
        return;
    const bool opened = open(path);
    if (receiver && completion)
        completion(opened);
}

bool OriginalAudioPlayer::isLoading() const
{
    return false;
}

void OriginalAudioPlayer::cancelOpen() {}

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
#ifdef _WIN32
    const auto nativePath = file.absoluteFilePath().toStdWString();
    MCI_OPEN_PARMSW parameters{};
    parameters.lpstrDeviceType = L"mpegvideo";
    parameters.lpstrElementName = nativePath.c_str();
    if (!impl_->check(mciSendCommandW(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_WAIT,
                                      reinterpret_cast<DWORD_PTR>(&parameters)),
                      "open"))
        return false;
    MCI_SET_PARMS timeFormat{};
    timeFormat.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
    if (!impl_->check(mciSendCommandW(parameters.wDeviceID, MCI_SET, MCI_SET_TIME_FORMAT | MCI_WAIT,
                                      reinterpret_cast<DWORD_PTR>(&timeFormat)),
                      "time format"))
    {
        mciSendCommandW(parameters.wDeviceID, MCI_CLOSE, MCI_WAIT, 0);
        return false;
    }
    MCI_STATUS_PARMS length{};
    length.dwItem = MCI_STATUS_LENGTH;
    if (!impl_->check(mciSendCommandW(parameters.wDeviceID, MCI_STATUS, MCI_STATUS_ITEM | MCI_WAIT,
                                      reinterpret_cast<DWORD_PTR>(&length)),
                      "duration"))
    {
        mciSendCommandW(parameters.wDeviceID, MCI_CLOSE, MCI_WAIT, 0);
        return false;
    }
    const double duration = static_cast<double>(length.dwReturn) / 1000.0;
    if (duration <= 0.0 || duration > 1200.0)
    {
        mciSendCommandW(parameters.wDeviceID, MCI_CLOSE, MCI_WAIT, 0);
        impl_->error = trText("messages.audio_transcription.duration_limit");
        return false;
    }
    close();
    impl_->device = parameters.wDeviceID;
    impl_->path = file.absoluteFilePath();
    impl_->duration = duration;
    impl_->error.clear();
    return setVolume(impl_->volume);
#else
    impl_->error = trText("messages.audio.windows_required");
    return false;
#endif
}

void OriginalAudioPlayer::close()
{
#ifdef _WIN32
    if (impl_->device != 0)
    {
        mciSendCommandW(impl_->device, MCI_CLOSE, MCI_WAIT, 0);
        impl_->device = 0;
    }
#endif
    impl_->path.clear();
    impl_->duration = 0.0;
    impl_->rate = 1.0;
}

bool OriginalAudioPlayer::isOpen() const
{
#ifdef _WIN32
    return impl_->device != 0;
#else
    return false;
#endif
}

bool OriginalAudioPlayer::play()
{
#ifdef _WIN32
    if (!impl_->ready())
        return false;
    if (positionSeconds() >= impl_->duration && !seek(0.0))
        return false;
    MCI_PLAY_PARMS parameters{};
    if (!impl_->check(mciSendCommandW(impl_->device, MCI_PLAY, 0, reinterpret_cast<DWORD_PTR>(&parameters)),
                      "play"))
        return false;
    impl_->error.clear();
    return true;
#else
    return false;
#endif
}

bool OriginalAudioPlayer::pause()
{
#ifdef _WIN32
    if (!impl_->ready())
        return false;
    MCI_GENERIC_PARMS parameters{};
    return impl_->check(
        mciSendCommandW(impl_->device, MCI_PAUSE, MCI_WAIT, reinterpret_cast<DWORD_PTR>(&parameters)), "pause");
#else
    return false;
#endif
}

bool OriginalAudioPlayer::stop()
{
#ifdef _WIN32
    if (!impl_->ready())
        return false;
    MCI_GENERIC_PARMS parameters{};
    if (!impl_->check(mciSendCommandW(impl_->device, MCI_STOP, MCI_WAIT, reinterpret_cast<DWORD_PTR>(&parameters)),
                      "stop"))
        return false;
    return seek(0.0);
#else
    return false;
#endif
}

bool OriginalAudioPlayer::seek(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds > impl_->duration)
    {
        impl_->error = trText("messages.audio_source.invalid_position");
        return false;
    }
#ifdef _WIN32
    if (!impl_->ready())
        return false;
    const bool resume = isPlaying();
    MCI_SEEK_PARMS position{};
    position.dwTo = static_cast<DWORD>(std::llround(seconds * 1000.0));
    if (!impl_->check(
            mciSendCommandW(impl_->device, MCI_SEEK, MCI_TO | MCI_WAIT, reinterpret_cast<DWORD_PTR>(&position)),
            "seek"))
        return false;
    return !resume || seconds >= impl_->duration || play();
#else
    return false;
#endif
}

bool OriginalAudioPlayer::setSpeed(double factor)
{
    if (!std::isfinite(factor) || factor < 0.25 || factor > 2.0)
    {
        impl_->error = trText("messages.audio.invalid_speed");
        return false;
    }
#ifdef _WIN32
    if (!impl_->ready())
        return false;
    MCI_DGV_SET_PARMS parameters{};
    parameters.dwSpeed = static_cast<DWORD>(std::lround(factor * 1000.0));
    if (!impl_->check(mciSendCommandW(impl_->device, MCI_SET, MCI_DGV_SET_SPEED | MCI_WAIT,
                                      reinterpret_cast<DWORD_PTR>(&parameters)),
                      "speed"))
        return false;
    impl_->rate = factor;
    return true;
#else
    return false;
#endif
}

bool OriginalAudioPlayer::setVolume(double volume)
{
    if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0)
    {
        impl_->error = trText("messages.audio.invalid_volume");
        return false;
    }
    if (!isOpen())
    {
        impl_->volume = volume;
        return true;
    }
#ifdef _WIN32
    MCI_DGV_SETAUDIO_PARMS parameters{};
    parameters.dwItem = MCI_DGV_SETAUDIO_VOLUME;
    parameters.dwValue = static_cast<DWORD>(std::lround(volume * 1000.0));
    if (!impl_->check(mciSendCommandW(impl_->device, MCI_SETAUDIO,
                                      MCI_DGV_SETAUDIO_ITEM | MCI_DGV_SETAUDIO_VALUE | MCI_WAIT,
                                      reinterpret_cast<DWORD_PTR>(&parameters)),
                      "volume"))
        return false;
    impl_->volume = volume;
    return true;
#else
    return false;
#endif
}

bool OriginalAudioPlayer::isPlaying() const
{
#ifdef _WIN32
    return isOpen() && impl_->status(MCI_STATUS_MODE) == MCI_MODE_PLAY;
#else
    return false;
#endif
}

double OriginalAudioPlayer::positionSeconds() const
{
#ifdef _WIN32
    return isOpen() ? static_cast<double>(impl_->status(MCI_STATUS_POSITION)) / 1000.0 : 0.0;
#else
    return 0.0;
#endif
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

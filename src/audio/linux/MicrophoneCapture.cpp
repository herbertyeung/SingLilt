// ALSA capture with bounded queues and worker-owned device lifetime.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "audio/MicrophoneCapture.h"
#include "i18n/LanguageManager.h"
#include <algorithm>
#include <alsa/asoundlib.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace singlilt
{
namespace
{
void checkAlsa(int status)
{
    if (status < 0)
        throw std::runtime_error(QString("ALSA: %1").arg(QString::fromUtf8(snd_strerror(status))).toStdString());
}
} // namespace

struct MicrophoneCapture::Impl
{
    std::atomic<State> state{State::Stopped};
    mutable std::mutex mutex;
    QString error;
    std::deque<MicrophoneBlock> blocks;
    std::jthread worker;

    void fail(const QString &message)
    {
        std::lock_guard lock(mutex);
        error = message;
        state = State::Failed;
    }

    void capture(std::stop_token stop, const QString &id)
    {
        try
        {
            snd_pcm_t *rawDevice = nullptr;
            const auto deviceName = id.isEmpty() ? QByteArray("default") : id.toUtf8();
            checkAlsa(snd_pcm_open(&rawDevice, deviceName.constData(), SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK));
            std::unique_ptr<snd_pcm_t, decltype(&snd_pcm_close)> device(rawDevice, snd_pcm_close);
            constexpr int Rate = 48000;
            checkAlsa(snd_pcm_set_params(device.get(), SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1,
                                         Rate, 1, 50000));
            checkAlsa(snd_pcm_start(device.get()));
            state = State::Running;
            std::array<std::int16_t, 480> buffer{};
            bool discontinuity = false;
            while (!stop.stop_requested())
            {
                const int available = snd_pcm_wait(device.get(), 50);
                if (stop.stop_requested())
                    break;
                const auto frames = available > 0 ? snd_pcm_readi(device.get(), buffer.data(), buffer.size())
                                                  : static_cast<snd_pcm_sframes_t>(available);
                if (frames == -EPIPE)
                {
                    checkAlsa(snd_pcm_prepare(device.get()));
                    checkAlsa(snd_pcm_start(device.get()));
                    discontinuity = true;
                    continue;
                }
                if (frames == -EAGAIN || frames == 0)
                    continue;
                checkAlsa(static_cast<int>(frames));
                snd_pcm_sframes_t delay = 0;
                checkAlsa(snd_pcm_delay(device.get(), &delay));
                MicrophoneBlock block;
                block.sampleRate = Rate;
                block.clockSeconds = MicrophoneCapture::clockSeconds() -
                                     double(frames + std::max<snd_pcm_sframes_t>(0, delay)) / Rate;
                block.discontinuity = discontinuity;
                discontinuity = false;
                block.samples.reserve(static_cast<std::size_t>(frames));
                for (snd_pcm_sframes_t frame = 0; frame < frames; ++frame)
                    block.samples.push_back(buffer[static_cast<std::size_t>(frame)] / 32768.0F);
                std::lock_guard lock(mutex);
                if (blocks.size() >= 128)
                    throw std::runtime_error(trText("messages.mic.overflow").toStdString());
                blocks.push_back(std::move(block));
            }
            state = State::Stopped;
        }
        catch (const std::exception &cause)
        {
            fail(QString::fromUtf8(cause.what()));
        }
    }
};

MicrophoneCapture::MicrophoneCapture() : impl_(std::make_unique<Impl>()) {}
MicrophoneCapture::~MicrophoneCapture()
{
    stop();
}

std::vector<MicrophoneDevice> MicrophoneCapture::devices()
{
    std::vector<MicrophoneDevice> devices;
    try
    {
        void **rawHints = nullptr;
        checkAlsa(snd_device_name_hint(-1, "pcm", &rawHints));
        std::unique_ptr<void *, decltype(&snd_device_name_free_hint)> hints(rawHints, snd_device_name_free_hint);
        for (void **hint = hints.get(); hint && *hint; ++hint)
        {
            std::unique_ptr<char, decltype(&std::free)> name(snd_device_name_get_hint(*hint, "NAME"), std::free);
            std::unique_ptr<char, decltype(&std::free)> io(snd_device_name_get_hint(*hint, "IOID"), std::free);
            std::unique_ptr<char, decltype(&std::free)> label(snd_device_name_get_hint(*hint, "DESC"), std::free);
            if (!name || QByteArray(name.get()) == "null" || (io && QByteArray(io.get()) == "Output"))
                continue;
            const QString id = QString::fromUtf8(name.get());
            const QString description = label ? QString::fromUtf8(label.get()).replace('\n', ' ') : id;
            if (id == "default")
                devices.insert(devices.begin(), {id, description});
            else
                devices.push_back({id, description});
        }
        std::lock_guard lock(impl_->mutex);
        impl_->error.clear();
    }
    catch (const std::exception &cause)
    {
        impl_->fail(QString::fromUtf8(cause.what()));
    }
    return devices;
}

void MicrophoneCapture::start(const QString &deviceId)
{
    stop();
    {
        std::lock_guard lock(impl_->mutex);
        impl_->error.clear();
        impl_->blocks.clear();
    }
    impl_->state = State::Starting;
    impl_->worker = std::jthread([this, deviceId](std::stop_token stop) { impl_->capture(stop, deviceId); });
}

void MicrophoneCapture::stop()
{
    if (impl_->worker.joinable())
    {
        impl_->worker.request_stop();
        impl_->worker.join();
    }
    impl_->state = State::Stopped;
}

MicrophoneCapture::State MicrophoneCapture::state() const
{
    return impl_->state.load();
}
QString MicrophoneCapture::errorString() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->error;
}
std::vector<MicrophoneBlock> MicrophoneCapture::takeBlocks()
{
    std::lock_guard lock(impl_->mutex);
    std::vector<MicrophoneBlock> result;
    result.reserve(impl_->blocks.size());
    while (!impl_->blocks.empty())
    {
        result.push_back(std::move(impl_->blocks.front()));
        impl_->blocks.pop_front();
    }
    return result;
}
double MicrophoneCapture::clockSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace singlilt

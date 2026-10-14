// SPDX-License-Identifier: MIT
#include "CaptureSampleClock.h"
#include "MicrophoneCapture.h"
#include "i18n/LanguageManager.h"
#include <QAudioDevice>
#include <QAudioSource>
#include <QCoreApplication>
#include <QIODevice>
#include <QMediaDevices>
#include <QPermissions>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
namespace singlilt
{
struct MicrophoneCapture::Impl : QObject
{
    std::unique_ptr<QAudioSource> source;
    QAudioFormat format;
    QByteArray pending;
    std::mutex mutex;
    std::deque<MicrophoneBlock> blocks;
    QString error;
    std::atomic<State> state{State::Stopped};
    std::atomic_uint generation{0};
    CaptureSampleClock clock;
    void setError(const QString &message)
    {
        std::lock_guard lock(mutex);
        error = message;
    }
    void stopSource()
    {
        if (source)
            source->stop();
        source.reset();
        pending.clear();
        clock = CaptureSampleClock{};
        std::lock_guard lock(mutex);
        blocks.clear();
    }
    void fail(const QString &message)
    {
        setError(message);
        state = State::Failed;
        if (source)
            source->stop();
    }
    void begin(const QString &id)
    {
        auto inputs = QMediaDevices::audioInputs();
        auto device = QMediaDevices::defaultAudioInput();
        if (!id.isEmpty())
        {
            auto found = std::find_if(inputs.begin(), inputs.end(),
                                      [&](const auto &d) { return QString::fromLatin1(d.id().toHex()) == id; });
            if (found == inputs.end())
            {
                fail(trText("messages.mic.no_device"));
                return;
            }
            device = *found;
        }
        if (device.isNull())
        {
            fail(trText("messages.mic.no_device"));
            return;
        }
        format = device.preferredFormat();
        if (!format.isValid())
        {
            fail(trText("messages.mic.format"));
            return;
        }
        source = std::make_unique<QAudioSource>(device, format);
        QObject::connect(source.get(), &QAudioSource::stateChanged, this,
                         [this](auto next)
                         {
                             if (source && next == QAudio::StoppedState && source->error() != QAudio::NoError)
                             {
                                 setError(trText("messages.mic.native_error").arg(int(source->error())));
                                 state = State::Failed;
                             }
                         });
        QIODevice *stream = source->start();
        if (!stream || source->error() != QAudio::NoError)
        {
            fail(trText("messages.mic.native_error").arg(int(source->error())));
            return;
        }
        state = State::Running;
        QObject::connect(
            stream, &QIODevice::readyRead, this,
            [this, stream]
            {
                if (state.load() != State::Running)
                    return;
                pending.append(stream->readAll());
                const int frameBytes = format.bytesPerFrame();
                int frames = pending.size() / frameBytes;
                if (frames == 0)
                    return;
                if (frames > format.sampleRate() * 2)
                {
                    fail(trText("messages.mic.overflow"));
                    return;
                }
                MicrophoneBlock block;
                block.sampleRate = format.sampleRate();
                block.clockSeconds = clock.next(MicrophoneCapture::clockSeconds(), frames, block.sampleRate);
                block.samples.resize(frames);
                for (int frame = 0; frame < frames; ++frame)
                {
                    double value = 0;
                    for (int channel = 0; channel < format.channelCount(); ++channel)
                    {
                        auto bytes = pending.constData() + frame * frameBytes + channel * format.bytesPerSample();
                        float sample = 0;
                        switch (format.sampleFormat())
                        {
                        case QAudioFormat::UInt8:
                            sample = (static_cast<unsigned char>(*bytes) - 128) / 128.0F;
                            break;
                        case QAudioFormat::Int16:
                        {
                            qint16 n;
                            std::memcpy(&n, bytes, 2);
                            sample = n / 32768.0F;
                            break;
                        }
                        case QAudioFormat::Int32:
                        {
                            qint32 n;
                            std::memcpy(&n, bytes, 4);
                            sample = float(n / 2147483648.0);
                            break;
                        }
                        case QAudioFormat::Float:
                            std::memcpy(&sample, bytes, 4);
                            break;
                        default:
                            fail(trText("messages.mic.format"));
                            return;
                        }
                        if (!std::isfinite(sample))
                        {
                            fail(trText("messages.mic.format"));
                            return;
                        }
                        value += sample;
                    }
                    block.samples[frame] = float(value / format.channelCount());
                }
                pending.remove(0, frames * frameBytes);
                bool overflow = false;
                {
                    std::lock_guard lock(mutex);
                    overflow = blocks.size() >= 128;
                    if (!overflow)
                        blocks.push_back(std::move(block));
                }
                if (overflow)
                    fail(trText("messages.mic.overflow"));
            });
    }
};
MicrophoneCapture::MicrophoneCapture() : impl_(std::make_unique<Impl>()) {}
MicrophoneCapture::~MicrophoneCapture()
{
    stop();
}
std::vector<MicrophoneDevice> MicrophoneCapture::devices()
{
    std::vector<MicrophoneDevice> result;
    auto inputs = QMediaDevices::audioInputs();
    auto defaultId = QMediaDevices::defaultAudioInput().id();
    for (auto &device : inputs)
    {
        MicrophoneDevice d{QString::fromLatin1(device.id().toHex()), device.description()};
        if (device.id() == defaultId)
            result.insert(result.begin(), d);
        else
            result.push_back(d);
    }
    return result;
}
void MicrophoneCapture::start(const QString &id)
{
    stop();
    impl_->setError({});
    impl_->state = State::Starting;
    auto token = impl_->generation.load();
    QMicrophonePermission permission;
    auto status = qApp->checkPermission(permission);
    if (status == Qt::PermissionStatus::Granted)
    {
        impl_->begin(id);
        return;
    }
    if (status == Qt::PermissionStatus::Denied)
    {
        impl_->fail(trText("messages.mic.permission_denied"));
        return;
    }
    qApp->requestPermission(permission, impl_.get(),
                            [this, id, token](const QPermission &result)
                            {
                                if (token != impl_->generation)
                                    return;
                                if (result.status() != Qt::PermissionStatus::Granted)
                                    impl_->fail(trText("messages.mic.permission_denied"));
                                else
                                    impl_->begin(id);
                            });
}
void MicrophoneCapture::stop()
{
    const unsigned token = ++impl_->generation;
    impl_->state = State::Stopped;
    if (QThread::currentThread() == impl_->thread())
        impl_->stopSource();
    else
    {
        // PracticeSession joins the analyzer on the UI thread: never block that analyzer
        // waiting for UI teardown, or the two threads deadlock. QObject context cancels
        // queued work if this capture is destroyed before the event is delivered.
        auto *context = impl_.get();
        QMetaObject::invokeMethod(
            context,
            [context, token]
            {
                if (context->generation.load() == token)
                    context->stopSource();
            },
            Qt::QueuedConnection);
    }
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

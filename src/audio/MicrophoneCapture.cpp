// Microphone sample capture and recording lifetime.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MicrophoneCapture.h"
#include "i18n/LanguageManager.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
// Windows property keys require the base COM/property declarations first.
// clang-format off
#include <Windows.h>
#include <mmdeviceapi.h>
#include <propkey.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
// clang-format on
#endif

namespace singlilt
{
namespace
{
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
class ComApartment
{
  public:
    ComApartment() : status_(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
    {
        if (FAILED(status_) && status_ != RPC_E_CHANGED_MODE)
            throw std::runtime_error("COM initialization failed");
    }
    ~ComApartment()
    {
        if (SUCCEEDED(status_))
            CoUninitialize();
    }

  private:
    HRESULT status_;
};
void check(HRESULT status)
{
    if (FAILED(status))
        throw std::runtime_error(trText("messages.mic.native_error")
                                     .arg(QString::number(static_cast<unsigned long>(status), 16))
                                     .toStdString());
}
struct CoTaskDeleter
{
    void operator()(void *pointer) const
    {
        CoTaskMemFree(pointer);
    }
};
struct EventDeleter
{
    void operator()(void *handle) const
    {
        if (handle)
            CloseHandle(handle);
    }
};

std::vector<float> monoSamples(const BYTE *buffer, UINT32 frames, const WAVEFORMATEX &format, bool silent)
{
    if (format.nSamplesPerSec < 8000 || format.nSamplesPerSec > 192000 || format.nChannels == 0 ||
        format.nChannels > 32 ||
        (format.wBitsPerSample != 16 && format.wBitsPerSample != 24 && format.wBitsPerSample != 32) ||
        format.nBlockAlign != format.nChannels * format.wBitsPerSample / 8)
        throw std::runtime_error(trText("messages.mic.format").toStdString());
    bool floating = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto &extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE &>(format);
        if (format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) ||
            (extended.SubFormat != KSDATAFORMAT_SUBTYPE_PCM &&
             extended.SubFormat != KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            throw std::runtime_error(trText("messages.mic.format").toStdString());
        floating = extended.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    else if (format.wFormatTag != WAVE_FORMAT_PCM && !floating)
        throw std::runtime_error(trText("messages.mic.format").toStdString());
    if (floating && format.wBitsPerSample != 32)
        throw std::runtime_error(trText("messages.mic.format").toStdString());
    std::vector<float> mono(frames, 0.0f);
    if (silent)
        return mono;
    for (UINT32 i = 0; i < frames; ++i)
    {
        double total = 0.0;
        for (int channel = 0; channel < format.nChannels; ++channel)
        {
            const BYTE *sample = buffer + i * format.nBlockAlign + channel * format.wBitsPerSample / 8;
            if (floating)
            {
                float number;
                std::memcpy(&number, sample, sizeof(number));
                if (!std::isfinite(number))
                    throw std::runtime_error(trText("messages.mic.format").toStdString());
                total += std::clamp(double(number), -1.0, 1.0);
            }
            else if (format.wBitsPerSample == 16)
            {
                short number;
                std::memcpy(&number, sample, sizeof(number));
                total += number / 32768.0;
            }
            else if (format.wBitsPerSample == 24)
            {
                int number = sample[0] | (sample[1] << 8) | (sample[2] << 16);
                if (number & 0x800000)
                    number -= 0x1000000;
                total += number / 8388608.0;
            }
            else
            {
                std::int32_t number;
                std::memcpy(&number, sample, sizeof(number));
                total += number / 2147483648.0;
            }
        }
        mono[i] = static_cast<float>(total / format.nChannels);
    }
    return mono;
}
#endif
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
#ifdef _WIN32
        try
        {
            ComApartment apartment;
            ComPtr<IMMDeviceEnumerator> enumerator;
            check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
            ComPtr<IMMDevice> device;
            check(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(id.utf16()), &device));
            ComPtr<IAudioClient> client;
            check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void **>(client.GetAddressOf())));
            WAVEFORMATEX *rawFormat = nullptr;
            check(client->GetMixFormat(&rawFormat));
            const std::unique_ptr<WAVEFORMATEX, CoTaskDeleter> format(rawFormat);
            monoSamples(nullptr, 0, *format, true);
            check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 1000000, 0,
                                     format.get(), nullptr));
            std::unique_ptr<void, EventDeleter> event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
            if (!event)
                throw std::runtime_error(trText("messages.mic.event").toStdString());
            check(client->SetEventHandle(event.get()));
            ComPtr<IAudioCaptureClient> reader;
            check(client->GetService(IID_PPV_ARGS(&reader)));
            check(client->Start());
            state = State::Running;
            while (!stop.stop_requested())
            {
                const auto wait = WaitForSingleObject(event.get(), 50);
                if (wait == WAIT_FAILED)
                    throw std::runtime_error(trText("messages.mic.event").toStdString());
                UINT32 frames = 0;
                check(reader->GetNextPacketSize(&frames));
                while (frames > 0 && !stop.stop_requested())
                {
                    BYTE *buffer = nullptr;
                    DWORD flags = 0;
                    UINT64 devicePosition = 0, qpcPosition = 0;
                    check(reader->GetBuffer(&buffer, &frames, &flags, &devicePosition, &qpcPosition));
                    MicrophoneBlock block;
                    try
                    {
                        block.sampleRate = static_cast<int>(format->nSamplesPerSec);
                        block.clockSeconds =
                            (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                                ? MicrophoneCapture::clockSeconds() - double(frames) / block.sampleRate
                                : qpcPosition / 1e7;
                        block.discontinuity = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
                        block.samples =
                            monoSamples(buffer, frames, *format, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
                    }
                    catch (...)
                    {
                        reader->ReleaseBuffer(frames);
                        throw;
                    }
                    check(reader->ReleaseBuffer(frames));
                    {
                        std::lock_guard lock(mutex);
                        if (blocks.size() >= 128)
                            throw std::runtime_error(trText("messages.mic.overflow").toStdString());
                        blocks.push_back(std::move(block));
                    }
                    check(reader->GetNextPacketSize(&frames));
                }
            }
            check(client->Stop());
            state = State::Stopped;
        }
        catch (const std::exception &cause)
        {
            fail(QString::fromUtf8(cause.what()));
        }
#else
        Q_UNUSED(stop);
        Q_UNUSED(id);
        fail(trText("messages.mic.platform"));
#endif
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
#ifdef _WIN32
    try
    {
        ComApartment apartment;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
        ComPtr<IMMDeviceCollection> collection;
        check(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection));
        UINT count = 0;
        check(collection->GetCount(&count));
        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> device;
            check(collection->Item(i, &device));
            LPWSTR rawId = nullptr;
            check(device->GetId(&rawId));
            std::unique_ptr<wchar_t, CoTaskDeleter> id(rawId);
            QString name = QString::fromWCharArray(id.get());
            ComPtr<IPropertyStore> properties;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)))
            {
                PROPVARIANT property;
                PropVariantInit(&property);
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &property)) &&
                    property.vt == VT_LPWSTR)
                    name = QString::fromWCharArray(property.pwszVal);
                PropVariantClear(&property);
            }
            devices.push_back({QString::fromWCharArray(id.get()), name});
        }
        ComPtr<IMMDevice> defaultDevice;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &defaultDevice)))
        {
            LPWSTR rawId = nullptr;
            if (SUCCEEDED(defaultDevice->GetId(&rawId)))
            {
                std::unique_ptr<wchar_t, CoTaskDeleter> id(rawId);
                const QString selected = QString::fromWCharArray(id.get());
                const auto item = std::find_if(devices.begin(), devices.end(), [&selected](const auto &device) { return device.id == selected; });
                if (item != devices.end()) std::rotate(devices.begin(), item, std::next(item));
            }
        }
        std::lock_guard lock(impl_->mutex);
        impl_->error.clear();
    }
    catch (const std::exception &cause)
    {
        impl_->fail(QString::fromUtf8(cause.what()));
    }
#endif
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
#ifdef _WIN32
    LARGE_INTEGER count, frequency;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&frequency);
    return double(count.QuadPart) / frequency.QuadPart;
#else
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
} // namespace singlilt

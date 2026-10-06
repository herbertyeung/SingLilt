// Lesson recording sessions and assessment coordination.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "PracticeSession.h"
#include "i18n/LanguageManager.h"
#include <QDataStream>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace singlilt
{
struct PracticeSession::Impl
{
    MicrophoneCapture microphone;
    mutable std::mutex mutex;
    Snapshot status;
    std::vector<PitchObservation> frames;
    std::vector<float> recording;
    int recordingRate = 0;
    std::jthread analyzer;

    void run(std::stop_token stop, bool calibration, double start, double duration, double latency)
    {
        try
        {
            int rate = 0;
            std::vector<float> pending;
            std::vector<double> noise;
            double pendingStart = 0.0, firstBlock = 0.0;
            double gate;
            {
                std::lock_guard lock(mutex);
                gate = status.noiseGate;
            }
            while (!stop.stop_requested())
            {
                if (microphone.state() == MicrophoneCapture::State::Failed)
                    throw std::runtime_error(microphone.errorString().toStdString());
                const double now = MicrophoneCapture::clockSeconds();
                if ((calibration && firstBlock > 0.0 && now > firstBlock + 2.2) ||
                    (!calibration && now > start + duration + latency + 0.2))
                    break;
                if (firstBlock == 0.0 && now > start + 5.0)
                    throw std::runtime_error(trText("messages.mic.timeout").toStdString());
                for (auto &block : microphone.takeBlocks())
                {
                    if (stop.stop_requested())
                        break;
                    if (rate == 0)
                    {
                        rate = block.sampleRate;
                        firstBlock = block.clockSeconds;
                        pendingStart = block.clockSeconds;
                        if (!calibration && firstBlock > start + 0.1)
                            throw std::runtime_error(trText("messages.mic.late_start").toStdString());
                        std::lock_guard lock(mutex);
                        status.state = calibration ? State::Monitoring : State::Recording;
                        recordingRate = rate;
                    }
                    else if (block.sampleRate != rate || block.discontinuity ||
                             std::abs(block.clockSeconds - (pendingStart + double(pending.size()) / rate)) > 0.05)
                        throw std::runtime_error(trText("messages.mic.discontinuity").toStdString());
                    pending.insert(pending.end(), block.samples.begin(), block.samples.end());
                    if (!calibration)
                    {
                        const int begin =
                            std::clamp(static_cast<int>(std::ceil((start + latency - block.clockSeconds) * rate)),
                                       0, static_cast<int>(block.samples.size()));
                        const int end = std::clamp(
                            static_cast<int>(std::ceil((start + duration + latency - block.clockSeconds) * rate)),
                            begin, static_cast<int>(block.samples.size()));
                        if (end > begin)
                        {
                            std::lock_guard lock(mutex);
                            if (recording.size() + (end - begin) > static_cast<std::size_t>(rate) * 120)
                                throw std::runtime_error(trText("messages.mic.limit").toStdString());
                            recording.insert(recording.end(), block.samples.begin() + begin,
                                             block.samples.begin() + end);
                        }
                    }
                    const int window = std::min(32768, static_cast<int>(rate * 0.086));
                    const int hop = rate / 50;
                    while (pending.size() >= static_cast<std::size_t>(window))
                    {
                        auto observation = detectSingingPitch(std::span<const float>(pending.data(), window), rate,
                                                              calibration ? 0.008 : gate);
                        observation.seconds = pendingStart + double(window) / (2 * rate) - start - latency;
                        observation.durationSeconds = double(hop) / rate;
                        const bool retain =
                            !calibration && observation.seconds >= 0.0 && observation.seconds < duration;
                        {
                            std::lock_guard lock(mutex);
                            status.latest = observation;
                            if (retain)
                            {
                                if (frames.size() >= 6000)
                                    throw std::runtime_error(trText("messages.mic.limit").toStdString());
                                frames.push_back(observation);
                            }
                        }
                        if (calibration && !observation.clipped)
                            noise.push_back(observation.rms);
                        pending.erase(pending.begin(), pending.begin() + hop);
                        pendingStart += double(hop) / rate;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            microphone.stop();
            std::lock_guard lock(mutex);
            if (calibration && !noise.empty() && !stop.stop_requested())
            {
                std::sort(noise.begin(), noise.end());
                const double measured = noise[noise.size() / 2] * 2.5;
                if (measured > 0.1)
                {
                    status.state = State::Failed;
                    status.error = trText("messages.mic.noisy");
                    return;
                }
                status.noiseGate = std::max(0.008, measured);
            }
            status.state = stop.stop_requested() ? State::Idle : State::Finished;
        }
        catch (const std::exception &error)
        {
            microphone.stop();
            std::lock_guard lock(mutex);
            status.state = State::Failed;
            status.error = QString::fromUtf8(error.what());
        }
    }
};

PracticeSession::PracticeSession() : impl_(std::make_unique<Impl>()) {}
PracticeSession::~PracticeSession()
{
    stop();
}
std::vector<MicrophoneDevice> PracticeSession::devices()
{
    return impl_->microphone.devices();
}
void PracticeSession::stop()
{
    if (impl_->analyzer.joinable())
    {
        impl_->analyzer.request_stop();
        impl_->analyzer.join();
    }
    impl_->microphone.stop();
}
void PracticeSession::calibrateNoise(const QString &deviceId)
{
    stop();
    const double start = MicrophoneCapture::clockSeconds();
    {
        std::lock_guard lock(impl_->mutex);
        impl_->status.state = State::Starting;
        impl_->status.error.clear();
        impl_->status.latest = {};
        impl_->frames.clear();
        impl_->recording.clear();
        impl_->status.recordStartSeconds = start;
    }
    impl_->microphone.start(deviceId);
    impl_->analyzer =
        std::jthread([this, start](std::stop_token stop) { impl_->run(stop, true, start, 2.2, 0.0); });
}
void PracticeSession::record(const QString &deviceId, double startClockSeconds, double durationSeconds,
                             double latencySeconds)
{
    if (!std::isfinite(startClockSeconds) || !std::isfinite(durationSeconds) || durationSeconds <= 0.0 ||
        durationSeconds > 111.0 || !std::isfinite(latencySeconds) || latencySeconds < 0.0 || latencySeconds > 0.5)
        throw std::invalid_argument("Invalid microphone session");
    stop();
    {
        std::lock_guard lock(impl_->mutex);
        impl_->status.state = State::Starting;
        impl_->status.error.clear();
        impl_->status.latest = {};
        impl_->status.recordStartSeconds = startClockSeconds;
        impl_->frames.clear();
        impl_->recording.clear();
    }
    impl_->microphone.start(deviceId);
    impl_->analyzer =
        std::jthread([this, startClockSeconds, durationSeconds, latencySeconds](std::stop_token stop)
                     { impl_->run(stop, false, startClockSeconds, durationSeconds, latencySeconds); });
}
PracticeSession::Snapshot PracticeSession::snapshot() const
{
    std::lock_guard lock(impl_->mutex);
    auto snapshot = impl_->status;
    snapshot.clockSeconds = MicrophoneCapture::clockSeconds();
    const auto count = std::min<std::size_t>(1000, impl_->frames.size());
    snapshot.observations.assign(impl_->frames.end() - count, impl_->frames.end());
    return snapshot;
}
std::vector<PitchObservation> PracticeSession::observations() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->frames;
}
bool PracticeSession::hasRecording() const
{
    std::lock_guard lock(impl_->mutex);
    return !impl_->recording.empty();
}
void PracticeSession::exportWave(const QString &path) const
{
    std::vector<float> samples;
    int rate;
    {
        std::lock_guard lock(impl_->mutex);
        samples = impl_->recording;
        rate = impl_->recordingRate;
    }
    if (samples.empty() || rate == 0)
        throw std::runtime_error(trText("messages.mic.no_recording").toStdString());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    const quint32 bytes = static_cast<quint32>(samples.size() * 2);
    QDataStream output(&file);
    output.setByteOrder(QDataStream::LittleEndian);
    output.writeRawData("RIFF", 4);
    output << quint32(bytes + 36);
    output.writeRawData("WAVEfmt ", 8);
    output << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2)
           << quint16(16);
    output.writeRawData("data", 4);
    output << bytes;
    for (float sample : samples)
        output << qint16(std::lround(std::clamp(double(sample), -1.0, 1.0) * 32767.0));
    if (output.status() != QDataStream::Ok || !file.commit())
        throw std::runtime_error(file.errorString().toStdString());
}
} // namespace singlilt

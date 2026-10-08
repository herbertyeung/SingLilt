// Audio decoding, melody analysis, and local lyric transcription.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AudioTranscriber.h"
#include "VocalSeparator.h"
#include "i18n/LanguageManager.h"
#include "platform/RuntimePaths.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#ifdef Q_OS_LINUX
#include "linux/AudioDecoder.h"
#endif

#ifdef _WIN32
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>
#endif

namespace singlilt
{
namespace
{
constexpr int DecodeRate = 16000;
constexpr int AnalysisRate = 8000;
constexpr int PitchWindow = 512;
constexpr int PitchHop = 80;
constexpr int TickQuantum = TicksPerQuarter / 4;

struct Cancelled
{
};

void checkCancellation(const std::shared_ptr<std::atomic_bool> &cancellation)
{
    if (cancellation && cancellation->load(std::memory_order_relaxed))
        throw Cancelled{};
}

[[noreturn]] void fail(const char *key)
{
    throw std::runtime_error(trText(key).toStdString());
}

void report(const AudioTranscriptionProgress &progress, int percent, const char *stage)
{
    if (progress)
        progress(percent, QString::fromLatin1(stage));
}

struct DecodedAudio
{
    std::vector<float> samples;
    double duration = 0.0;
    double start = 0.0;
    double end = 0.0;
};

#ifdef _WIN32
void checkNative(HRESULT status, const char *operation)
{
    if (FAILED(status))
        throw std::runtime_error(
            trText("messages.audio_transcription.native_failed")
                .arg(QString::fromLatin1(operation), QString::number(static_cast<quint32>(status), 16))
                .toStdString());
}

class MediaRuntime final
{
  public:
    MediaRuntime()
    {
        const HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (status != RPC_E_CHANGED_MODE)
            checkNative(status, "CoInitializeEx");
        ownsCom_ = SUCCEEDED(status);
        const HRESULT startup = MFStartup(MF_VERSION);
        if (FAILED(startup))
        {
            if (ownsCom_)
                CoUninitialize();
            checkNative(startup, "MFStartup");
        }
    }
    ~MediaRuntime()
    {
        MFShutdown();
        if (ownsCom_)
            CoUninitialize();
    }
    MediaRuntime(const MediaRuntime &) = delete;
    MediaRuntime &operator=(const MediaRuntime &) = delete;

  private:
    bool ownsCom_ = false;
};

class BufferLock final
{
  public:
    explicit BufferLock(IMFMediaBuffer *buffer) : buffer_(buffer)
    {
        checkNative(buffer_->Lock(&bytes, nullptr, &length), "IMFMediaBuffer::Lock");
    }
    ~BufferLock()
    {
        buffer_->Unlock();
    }
    BufferLock(const BufferLock &) = delete;
    BufferLock &operator=(const BufferLock &) = delete;
    BYTE *bytes = nullptr;
    DWORD length = 0;

  private:
    IMFMediaBuffer *buffer_;
};
#endif

DecodedAudio decodeAudio(const QString &path, const AudioTranscriptionOptions &options,
                         const std::shared_ptr<std::atomic_bool> &cancellation,
                         const AudioTranscriptionProgress &progress, double *durationMetadata = nullptr)
{
    checkCancellation(cancellation);
#ifdef _WIN32
    MediaRuntime runtime;
    using Microsoft::WRL::ComPtr;
    constexpr DWORD AudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    ComPtr<IMFSourceReader> reader;
    const auto nativePath = path.toStdWString();
    checkNative(MFCreateSourceReaderFromURL(nativePath.c_str(), nullptr, &reader), "open audio");
    PROPVARIANT duration{};
    const HRESULT metadata = reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
                                                              MF_PD_DURATION, &duration);
    const double sourceDuration = SUCCEEDED(metadata) && duration.vt == VT_UI8
                                      ? static_cast<double>(duration.uhVal.QuadPart) / 10000000.0
                                      : 0.0;
    PropVariantClear(&duration);
    if (!std::isfinite(sourceDuration) || sourceDuration <= 0.0 || sourceDuration > 1200.0)
        fail("messages.audio_transcription.duration_limit");
    if (durationMetadata)
        *durationMetadata = sourceDuration;
    if (options.startSeconds >= sourceDuration)
        fail("messages.audio_transcription.invalid_selection");
    DecodedAudio audio;
    audio.duration = sourceDuration;
    audio.start = options.startSeconds;
    audio.end = std::min(options.endSeconds, sourceDuration);
    audio.samples.assign(static_cast<std::size_t>(std::llround((audio.end - audio.start) * DecodeRate)), 0.0F);

    checkNative(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE),
                "disable unused streams");
    checkNative(reader->SetStreamSelection(AudioStream, TRUE), "select audio stream");
    ComPtr<IMFMediaType> type;
    checkNative(MFCreateMediaType(&type), "MFCreateMediaType");
    checkNative(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "audio media type");
    checkNative(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "PCM media type");
    checkNative(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1), "mono output");
    checkNative(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, DecodeRate), "16 kHz output");
    checkNative(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "PCM16 output");
    checkNative(type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 2), "PCM alignment");
    checkNative(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, DecodeRate * 2), "PCM byte rate");
    checkNative(reader->SetCurrentMediaType(AudioStream, nullptr, type.Get()), "decode format");
    ComPtr<IMFMediaType> actual;
    checkNative(reader->GetCurrentMediaType(AudioStream, &actual), "verify decode format");
    UINT32 sampleRate = 0, channels = 0, bits = 0;
    checkNative(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate), "sample rate");
    checkNative(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels), "channel count");
    checkNative(actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits), "sample depth");
    if (sampleRate != DecodeRate || channels != 1 || bits != 16)
        fail("messages.audio_transcription.unsupported_pcm");
    if (audio.start > 0.0)
    {
        PROPVARIANT seek{};
        seek.vt = VT_I8;
        seek.hVal.QuadPart = static_cast<LONGLONG>(std::llround(audio.start * 10000000.0));
        checkNative(reader->SetCurrentPosition(GUID_NULL, seek), "seek selected interval");
    }
    const auto firstFrame = static_cast<std::int64_t>(std::llround(audio.start * DecodeRate));
    const auto lastFrame = firstFrame + static_cast<std::int64_t>(audio.samples.size());
    std::int64_t copiedUntil = firstFrame;
    int lastProgress = -1;
    for (int iteration = 0; iteration < 200000; ++iteration)
    {
        checkCancellation(cancellation);
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        checkNative(reader->ReadSample(AudioStream, 0, nullptr, &flags, &timestamp, &sample), "read audio sample");
        if (flags & MF_SOURCE_READERF_ERROR)
            fail("messages.audio_transcription.decode_failed");
        if (sample)
        {
            ComPtr<IMFMediaBuffer> buffer;
            checkNative(sample->ConvertToContiguousBuffer(&buffer), "PCM sample buffer");
            BufferLock locked(buffer.Get());
            if (locked.length % 2 != 0)
                fail("messages.audio_transcription.unsupported_pcm");
            const auto sampleStart =
                static_cast<std::int64_t>(std::llround(timestamp * (DecodeRate / 10000000.0)));
            const auto sampleEnd = sampleStart + locked.length / 2;
            const auto start = std::max(firstFrame, sampleStart);
            const auto end = std::min(lastFrame, sampleEnd);
            for (auto frame = start; frame < end; ++frame)
                audio.samples[static_cast<std::size_t>(frame - firstFrame)] =
                    qFromLittleEndian<qint16>(locked.bytes + (frame - sampleStart) * 2) / 32768.0F;
            copiedUntil = std::max(copiedUntil, end);
            const int percent =
                2 + static_cast<int>(23 * std::clamp<double>(double(copiedUntil - firstFrame) /
                                                                 std::max<std::size_t>(1, audio.samples.size()),
                                                             0.0, 1.0));
            if (percent != lastProgress)
            {
                report(progress, percent, "messages.audio_transcription.progress_decode");
                lastProgress = percent;
            }
            if (sampleEnd >= lastFrame)
                break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
        if (iteration == 199999)
            fail("messages.audio_transcription.decode_failed");
    }
    if (copiedUntil <= firstFrame || audio.samples.empty())
        fail("messages.audio_transcription.decode_failed");
    return audio;
#elif defined(Q_OS_LINUX)
    try
    {
        auto decoded = decodeLinuxAudio(path, options.startSeconds, options.endSeconds, cancellation.get());
        checkCancellation(cancellation);
        if (durationMetadata)
            *durationMetadata = decoded.duration;
        report(progress, 25, "messages.audio_transcription.progress_decode");
        return {std::move(decoded.samples), decoded.duration, decoded.start, decoded.end};
    }
    catch (const AudioDecodeCancelled &)
    {
        throw Cancelled{};
    }
#else
    Q_UNUSED(path);
    Q_UNUSED(options);
    Q_UNUSED(progress);
    fail("messages.audio.windows_required");
#endif
}

struct PitchFrame
{
    int pitch = -1;
    double confidence = 0.0;
    double energy = 0.0;
};

std::vector<float> analysisSamples(const std::vector<float> &samples, bool enhanceVoice)
{
    std::vector<float> reduced;
    reduced.reserve(samples.size() / 2);
    double previousInput = 0.0, highpass = 0.0, lowpass = 0.0;
    const double highCoefficient = std::exp(-2.0 * std::numbers::pi * 80.0 / AnalysisRate);
    const double lowCoefficient = 1.0 - std::exp(-2.0 * std::numbers::pi * 1800.0 / AnalysisRate);
    for (std::size_t i = 0; i + 1 < samples.size(); i += 2)
    {
        double sample = (samples[i] + samples[i + 1]) * 0.5;
        if (enhanceVoice)
        {
            highpass = highCoefficient * (highpass + sample - previousInput);
            previousInput = sample;
            lowpass += lowCoefficient * (highpass - lowpass);
            sample = lowpass;
        }
        reduced.push_back(static_cast<float>(sample));
    }
    return reduced;
}

PitchFrame estimatePitch(const std::array<float, PitchWindow> &samples, double gate)
{
    PitchFrame frame;
    double squareSum = 0.0;
    for (const auto sample : samples)
        squareSum += sample * sample;
    frame.energy = std::sqrt(squareSum / PitchWindow);
    if (frame.energy < gate)
        return frame;
    constexpr int MinimumLag = 4;
    constexpr int MaximumLag = 123;
    constexpr int ComparisonSamples = PitchWindow / 2;
    std::array<double, MaximumLag + 1> difference{};
    double accumulated = 0.0;
    for (int lag = 1; lag <= MaximumLag; ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i < ComparisonSamples; ++i)
        {
            const double delta = samples[i] - samples[i + lag];
            sum += delta * delta;
        }
        accumulated += sum;
        difference[lag] = accumulated > 0.0 ? sum * lag / accumulated : 1.0;
    }
    int selected = -1;
    for (int lag = MinimumLag; lag < MaximumLag; ++lag)
    {
        if (difference[lag] >= 0.18)
            continue;
        while (lag < MaximumLag - 1 && difference[lag + 1] < difference[lag])
            ++lag;
        selected = lag;
        break;
    }
    if (selected < 0)
    {
        const auto best = std::min_element(difference.begin() + MinimumLag, difference.end());
        if (*best > 0.28)
            return frame;
        selected = static_cast<int>(best - difference.begin());
    }
    double period = selected;
    if (selected > MinimumLag && selected < MaximumLag)
    {
        const double left = difference[selected - 1];
        const double middle = difference[selected];
        const double right = difference[selected + 1];
        const double denominator = left - 2.0 * middle + right;
        if (std::abs(denominator) > 1e-12)
            period += 0.5 * (left - right) / denominator;
    }
    const double frequency = AnalysisRate / period;
    const int midi = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(frequency / 440.0)));
    if (midi >= 36 && midi <= 96)
    {
        frame.pitch = midi;
        frame.confidence = std::clamp(1.0 - difference[selected], 0.0, 1.0);
    }
    return frame;
}

struct PitchSegment
{
    int pitch;
    double start;
    double end;
    double confidence;
};

std::vector<PitchSegment> extractSegments(const std::vector<float> &samples,
                                          const std::shared_ptr<std::atomic_bool> &cancellation,
                                          const AudioTranscriptionProgress &progress)
{
    double peakEnergy = 0.0;
    for (std::size_t first = 0; first < samples.size(); first += PitchHop)
    {
        const auto end = std::min(samples.size(), first + PitchHop);
        double squareSum = 0.0;
        for (auto i = first; i < end; ++i)
            squareSum += samples[i] * samples[i];
        peakEnergy = std::max(peakEnergy, std::sqrt(squareSum / (end - first)));
    }
    const double gate = std::max(0.006, peakEnergy * 0.06);
    std::vector<PitchFrame> frames;
    frames.reserve((samples.size() + PitchHop - 1) / PitchHop);
    std::array<float, PitchWindow> window{};
    int previousPercent = -1;
    for (std::size_t center = 0; center < samples.size(); center += PitchHop)
    {
        checkCancellation(cancellation);
        for (int i = 0; i < PitchWindow; ++i)
        {
            const auto source = static_cast<std::int64_t>(center) + i - PitchWindow / 2;
            window[i] = source >= 0 && source < static_cast<std::int64_t>(samples.size())
                            ? samples[static_cast<std::size_t>(source)]
                            : 0.0F;
        }
        frames.push_back(estimatePitch(window, gate));
        const int percent = 30 + static_cast<int>(35 * center / samples.size());
        if (percent != previousPercent)
        {
            report(progress, percent, "messages.audio_transcription.progress_pitch");
            previousPercent = percent;
        }
    }
    std::vector<int> labels;
    labels.reserve(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        std::array<int, 5> neighbours{};
        for (int j = -2; j <= 2; ++j)
        {
            const auto index = std::clamp<std::int64_t>(static_cast<std::int64_t>(i) + j, 0,
                                                        static_cast<std::int64_t>(frames.size()) - 1);
            neighbours[j + 2] = frames[static_cast<std::size_t>(index)].pitch;
        }
        std::sort(neighbours.begin(), neighbours.end());
        labels.push_back(neighbours[2]);
    }
    const double duration = static_cast<double>(samples.size()) / AnalysisRate;
    std::vector<PitchSegment> segments;
    for (std::size_t first = 0; first < labels.size();)
    {
        std::size_t end = first + 1;
        double confidence = frames[first].confidence;
        while (end < labels.size() && labels[end] == labels[first])
            confidence += frames[end++].confidence;
        segments.push_back({labels[first], double(first * PitchHop) / AnalysisRate,
                            std::min(duration, double(end * PitchHop) / AnalysisRate),
                            confidence / (end - first)});
        first = end;
    }
    // Brief dropouts and vibrato excursions are not separate sung syllables.
    for (std::size_t i = 0; i < segments.size() && segments.size() > 1;)
    {
        if (segments[i].end - segments[i].start >= 0.08)
        {
            ++i;
            continue;
        }
        if (i == 0)
        {
            segments[1].start = segments[0].start;
            segments.erase(segments.begin());
        }
        else
        {
            segments[i - 1].end = segments[i].end;
            segments.erase(segments.begin() + i);
            if (i < segments.size() && segments[i - 1].pitch == segments[i].pitch)
            {
                segments[i - 1].end = segments[i].end;
                segments.erase(segments.begin() + i);
            }
        }
    }
    return segments;
}

double estimateTempo(const std::vector<PitchSegment> &segments)
{
    std::vector<double> intervals;
    double previous = -1.0;
    for (const auto &segment : segments)
    {
        if (segment.pitch < 0)
            continue;
        if (previous >= 0.0 && segment.start - previous >= 0.18 && segment.start - previous <= 2.0)
            intervals.push_back(segment.start - previous);
        previous = segment.start;
    }
    if (intervals.empty())
        return 90.0;
    std::sort(intervals.begin(), intervals.end());
    double bpm = 60.0 / intervals[intervals.size() / 2];
    while (bpm < 70.0)
        bpm *= 2.0;
    while (bpm > 150.0)
        bpm *= 0.5;
    return std::round(bpm);
}

int estimateKey(const std::vector<PitchSegment> &segments)
{
    constexpr std::array<double, 12> major{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    constexpr std::array<double, 12> minor{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    std::array<double, 12> histogram{};
    for (const auto &segment : segments)
        if (segment.pitch >= 0)
            histogram[segment.pitch % 12] += (segment.end - segment.start) * segment.confidence;
    double best = -std::numeric_limits<double>::infinity();
    int tonic = 0;
    for (int root = 0; root < 12; ++root)
    {
        for (const bool isMinor : {false, true})
        {
            const auto &profile = isMinor ? minor : major;
            const double average = std::accumulate(profile.begin(), profile.end(), 0.0) / profile.size();
            double weight = 0.0;
            double variance = 0.0;
            for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            {
                const double centered = profile[(pitchClass - root + 12) % 12] - average;
                weight += histogram[pitchClass] * centered;
                variance += centered * centered;
            }
            weight /= std::sqrt(variance);
            if (weight > best)
            {
                best = weight;
                // Score.tonic is the numbered-notation mapping, not the minor tonic.
                tonic = isMinor ? (root + 3) % 12 : root;
            }
        }
    }
    return tonic;
}

Note pitchNote(int pitch, int tonic)
{
    Note note;
    if (pitch < 0)
    {
        note.degree = 0;
        return note;
    }
    constexpr std::array<int, 7> scale{0, 2, 4, 5, 7, 9, 11};
    int bestDistance = 128;
    for (int octave = -6; octave <= 6; ++octave)
    {
        for (int degree = 0; degree < 7; ++degree)
        {
            const int accidental = pitch - (60 + tonic + scale[degree] + 12 * octave);
            if (std::abs(accidental) < bestDistance)
            {
                note.degree = degree + 1;
                note.octave = octave;
                note.accidental = accidental;
                bestDistance = std::abs(accidental);
            }
        }
    }
    return note;
}

void quantizeScore(AudioTranscriptionResult &result, const std::vector<PitchSegment> &segments, double bpm,
                   int tonic, const std::shared_ptr<std::atomic_bool> &cancellation)
{
    result.score.title = QFileInfo(result.sourcePath).completeBaseName().toStdString();
    result.score.bpm = bpm;
    result.score.tonic = tonic;
    result.score.versePrograms = {0};
    const auto barTicks = static_cast<std::int64_t>(ticksPerBar(result.score));
    std::int64_t startTick = 0;
    for (const auto &segment : segments)
    {
        checkCancellation(cancellation);
        const auto endTick =
            static_cast<std::int64_t>(std::llround(segment.end * bpm * TicksPerQuarter / (60.0 * TickQuantum))) *
            TickQuantum;
        if (endTick <= startTick)
            continue;
        for (auto tick = startTick; tick < endTick;)
        {
            const auto pieceEnd = std::min(endTick, (tick / barTicks + 1) * barTicks);
            Note note = pitchNote(segment.pitch, tonic);
            note.id = static_cast<int>(result.score.notes.size());
            note.durationTicks = static_cast<int>(pieceEnd - tick);
            note.measure = static_cast<int>(tick / barTicks);
            note.confidence = segment.pitch < 0 ? 1.0 : segment.confidence;
            note.tieToNext = segment.pitch >= 0 && pieceEnd < endTick;
            result.score.notes.push_back(note);
            const double secondsPerTick = (segment.end - segment.start) / (endTick - startTick);
            const double sourceStart =
                result.selectedStartSeconds + segment.start + (tick - startTick) * secondsPerTick;
            const double sourceEnd =
                result.selectedStartSeconds +
                (pieceEnd == endTick ? segment.end : segment.start + (pieceEnd - startTick) * secondsPerTick);
            result.timings.push_back(
                {note.id,
                 result.timings.empty() ? sourceStart : std::max(sourceStart, result.timings.back().endSeconds),
                 std::min(sourceEnd, result.selectedEndSeconds), tick, pieceEnd});
            tick = pieceEnd;
        }
        startTick = endTick;
    }
}

QStringList lyricUnits(const QString &text)
{
    static const QRegularExpression expression(QStringLiteral(
        "[\\p{Han}\\p{Hiragana}\\p{Katakana}\\p{Hangul}]|[\\p{L}\\p{N}]+(?:['’][\\p{L}\\p{N}]+)*|[_~]"));
    QStringList units;
    auto matches = expression.globalMatch(text);
    while (matches.hasNext())
        units.append(matches.next().captured());
    return units;
}

std::vector<AudioLyricTiming> distributeLyrics(const QString &text, double start, double end)
{
    const auto units = lyricUnits(text);
    std::vector<AudioLyricTiming> timings;
    for (qsizetype i = 0; i < units.size(); ++i)
        timings.push_back({units[i].toStdString(), start + (end - start) * i / units.size(),
                           start + (end - start) * (i + 1) / units.size(), -1});
    return timings;
}

void appendLyric(Note &note, const QString &text)
{
    const QString previous = QString::fromStdString(note.lyric);
    const bool needsSpace =
        !previous.isEmpty() && !text.isEmpty() && previous.back().unicode() < 128 && text.front().unicode() < 128;
    note.lyric = (previous + (needsSpace ? QStringLiteral(" ") : QString{}) + text).toStdString();
}

void alignTimedLyrics(AudioTranscriptionResult &result, std::vector<AudioLyricTiming> lyrics)
{
    std::stable_sort(lyrics.begin(), lyrics.end(),
                     [](const AudioLyricTiming &left, const AudioLyricTiming &right)
                     {
                         return left.startSeconds != right.startSeconds ? left.startSeconds < right.startSeconds
                                                                        : left.endSeconds < right.endSeconds;
                     });
    double previousStart =
        result.lyricTimings.empty() ? result.selectedStartSeconds : result.lyricTimings.back().startSeconds;
    double previousEnd =
        result.lyricTimings.empty() ? result.selectedStartSeconds : result.lyricTimings.back().endSeconds;
    for (auto &lyric : lyrics)
    {
        if (!std::isfinite(lyric.startSeconds) || !std::isfinite(lyric.endSeconds) ||
            lyric.endSeconds <= result.selectedStartSeconds || lyric.startSeconds >= result.selectedEndSeconds)
            continue;
        lyric.startSeconds = std::clamp(lyric.startSeconds, previousStart, result.selectedEndSeconds);
        lyric.endSeconds = std::clamp(lyric.endSeconds, previousEnd, result.selectedEndSeconds);
        if (lyric.endSeconds <= lyric.startSeconds)
            continue;
        previousStart = lyric.startSeconds;
        previousEnd = lyric.endSeconds;
        int bestIndex = -1;
        double bestOverlap = -1.0;
        for (const auto &timing : result.timings)
        {
            if (result.score.notes[timing.sourceNoteIndex].degree == 0)
                continue;
            const double overlap =
                std::min(lyric.endSeconds, timing.endSeconds) - std::max(lyric.startSeconds, timing.startSeconds);
            if (overlap > bestOverlap)
            {
                bestOverlap = overlap;
                bestIndex = timing.sourceNoteIndex;
            }
        }
        if (bestIndex >= 0 && bestOverlap >= 0.0)
        {
            lyric.sourceNoteIndex = bestIndex;
            const QString text = QString::fromStdString(lyric.text);
            if (text != "_" && text != "~")
                appendLyric(result.score.notes[bestIndex], text);
            else
                result.score.notes[bestIndex].verseLyrics = {""};
        }
        result.lyricTimings.push_back(std::move(lyric));
    }
}

void applyManualLyrics(AudioTranscriptionResult &result, const QString &text)
{
    static const QRegularExpression stamp(QStringLiteral("\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?\\]"));
    struct Line
    {
        double time;
        QString text;
    };
    std::vector<Line> lines;
    for (const auto &line : text.split('\n'))
    {
        auto matches = stamp.globalMatch(line);
        while (matches.hasNext())
        {
            const auto match = matches.next();
            const QString fraction = match.captured(3);
            const double time = match.captured(1).toInt() * 60.0 + match.captured(2).toInt() +
                                (fraction.isEmpty() ? 0.0 : fraction.toInt() / std::pow(10.0, fraction.size()));
            QString content = line;
            content.remove(stamp);
            lines.push_back({time, content.trimmed()});
        }
    }
    if (!lines.empty())
    {
        std::stable_sort(lines.begin(), lines.end(),
                         [](const Line &left, const Line &right) { return left.time < right.time; });
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const double end = i + 1 < lines.size() ? lines[i + 1].time : result.selectedEndSeconds;
            if (end <= result.selectedStartSeconds || lines[i].time >= result.selectedEndSeconds)
                continue;
            alignTimedLyrics(result,
                             distributeLyrics(lines[i].text, std::max(lines[i].time, result.selectedStartSeconds),
                                              std::min(end, result.selectedEndSeconds)));
        }
    }
    else
    {
        const auto units = lyricUnits(text);
        qsizetype next = 0;
        for (std::size_t i = 0; i < result.score.notes.size() && next < units.size(); ++i)
        {
            if (result.score.notes[i].degree == 0 || (i > 0 && result.score.notes[i - 1].tieToNext))
                continue;
            const auto &timing = result.timings[i];
            const QString unit = units[next++];
            if (unit == "_" || unit == "~")
                result.score.notes[i].verseLyrics = {""};
            else
                result.score.notes[i].lyric = unit.toStdString();
            result.lyricTimings.push_back(
                {unit.toStdString(), timing.startSeconds, timing.endSeconds, static_cast<int>(i)});
        }
        if (next < units.size())
            result.warnings.append("messages.audio_transcription.lyrics_unmatched");
    }
    result.recognizedLyrics = text.trimmed();
    result.warnings.append("messages.audio_transcription.lyrics_alignment_suggestion");
}

void writeWhisperWave(const QString &path, const std::vector<float> &samples)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    const auto bytes = static_cast<quint32>(samples.size() * 2);
    stream.writeRawData("RIFF", 4);
    stream << quint32(bytes + 36);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(DecodeRate) << quint32(DecodeRate * 2)
           << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << bytes;
    if (file.write(header) != header.size())
        throw std::runtime_error(file.errorString().toStdString());
    std::array<char, 8192> pcm{};
    for (std::size_t first = 0; first < samples.size(); first += pcm.size() / 2)
    {
        const auto count = std::min(samples.size() - first, pcm.size() / 2);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto sample =
                static_cast<qint16>(std::lround(std::clamp(samples[first + i], -1.0F, 1.0F) * 32767.0F));
            qToLittleEndian(sample, reinterpret_cast<uchar *>(pcm.data() + i * 2));
        }
        if (file.write(pcm.data(), static_cast<qint64>(count * 2)) != static_cast<qint64>(count * 2))
            throw std::runtime_error(file.errorString().toStdString());
    }
    if (!file.commit())
        throw std::runtime_error(file.errorString().toStdString());
}

void recognizeLyrics(AudioTranscriptionResult &result, const DecodedAudio &audio,
                     const AudioTranscriptionOptions &options,
                     const std::shared_ptr<std::atomic_bool> &cancellation,
                     const AudioTranscriptionProgress &progress)
{
    const QString executable =
        options.whisperExecutable.isEmpty()
            ? QCoreApplication::applicationDirPath() + "/tools/whisper/whisper-cli" + NativeExecutableSuffix
            : options.whisperExecutable;
    const QString model = options.whisperModel.isEmpty()
                              ? QCoreApplication::applicationDirPath() + "/models/ggml-base.bin"
                              : options.whisperModel;
    if (!QFileInfo(executable).isFile() || !QFileInfo(model).isFile())
        fail("messages.audio_transcription.missing_whisper");
    checkCancellation(cancellation);
    QTemporaryDir temporary(QDir::tempPath() + "/singlilt-audio-XXXXXX");
    if (!temporary.isValid())
        fail("messages.audio_transcription.temporary_failed");
    const QString wave = temporary.filePath("selected.wav");
    const QString output = temporary.filePath("lyrics");
    writeWhisperWave(wave, audio.samples);
    QProcess process;
    process.setWorkingDirectory(QFileInfo(executable).absolutePath());
    process.setProgram(QFileInfo(executable).absoluteFilePath());
    process.setArguments({"-m", QFileInfo(model).absoluteFilePath(), "-f", wave, "-l", options.language, "-ng",
                          "-ojf", "-of", output, "-np", "-pp"});
    process.start();
    if (!process.waitForStarted(5000))
        throw std::runtime_error(
            trText("messages.audio_transcription.whisper_failed").arg(process.errorString()).toStdString());
    QElapsedTimer timer;
    timer.start();
    QByteArray diagnostic;
    static const QRegularExpression progressExpression(QStringLiteral("progress\\s*=\\s*(\\d+)%"));
    while (!process.waitForFinished(100))
    {
        if ((cancellation && cancellation->load(std::memory_order_relaxed)) || timer.elapsed() > 600000)
        {
            process.kill();
            process.waitForFinished(5000);
            checkCancellation(cancellation);
            fail("messages.audio_transcription.whisper_timeout");
        }
        const auto chunk = process.readAllStandardError();
        diagnostic.append(chunk);
        if (diagnostic.size() > 16384)
            diagnostic = diagnostic.right(16384);
        process.readAllStandardOutput();
        auto matches = progressExpression.globalMatch(QString::fromUtf8(chunk));
        while (matches.hasNext())
            report(progress, 75 + matches.next().captured(1).toInt() * 23 / 100,
                   "messages.audio_transcription.progress_lyrics");
    }
    checkCancellation(cancellation);
    diagnostic.append(process.readAllStandardError());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error(trText("messages.audio_transcription.whisper_failed")
                                     .arg(QString::fromUtf8(diagnostic.right(2000)))
                                     .toStdString());
    QFile json(output + ".json");
    if (!json.open(QIODevice::ReadOnly) || json.size() > 16 * 1024 * 1024)
        fail("messages.audio_transcription.invalid_whisper_json");
    QJsonParseError parseError;
    const auto jsonBytes = json.readAll();
    auto document = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error == QJsonParseError::IllegalUTF8String)
    {
        // whisper-cli 1.8 emits raw BPE bytes in token.text. A token may split a
        // CJK code point; repair those fragments, never the authoritative text.
        document = QJsonDocument::fromJson(QString::fromUtf8(jsonBytes).toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError)
            result.warnings.append("messages.audio_transcription.whisper_token_fallback");
    }
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        !document.object().value("transcription").isArray())
        fail("messages.audio_transcription.invalid_whisper_json");
    std::vector<AudioLyricTiming> lyricTimings;
    for (const auto &item : document.object().value("transcription").toArray())
    {
        checkCancellation(cancellation);
        const auto segment = item.toObject();
        const QString text = segment.value("text").toString().trimmed();
        if (text.contains(QChar::ReplacementCharacter))
            fail("messages.audio_transcription.invalid_whisper_json");
        if (!text.isEmpty())
            result.recognizedLyrics +=
                (result.recognizedLyrics.isEmpty() ? QString{} : QStringLiteral("\n")) + text;
        const auto offsets = segment.value("offsets").toObject();
        const double start = offsets.value("from").toDouble(-1.0) / 1000.0;
        const double end = offsets.value("to").toDouble(-1.0) / 1000.0;
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0.0 || end <= start ||
            end > audio.end - audio.start + 1.0)
            continue;
        std::vector<AudioLyricTiming> tokenTimings;
        QStringList tokenUnits;
        for (const auto &tokenItem : segment.value("tokens").toArray())
        {
            const auto token = tokenItem.toObject();
            const QString tokenText = token.value("text").toString();
            if (tokenText.startsWith("[_") || tokenText.contains(QChar::ReplacementCharacter))
                continue;
            const auto tokenOffsets = token.value("offsets").toObject();
            const double tokenStart = tokenOffsets.value("from").toDouble(-1.0) / 1000.0;
            const double tokenEnd = tokenOffsets.value("to").toDouble(-1.0) / 1000.0;
            if (tokenStart < 0.0 || tokenEnd <= tokenStart || tokenEnd > audio.end - audio.start + 1.0)
                continue;
            tokenUnits.append(lyricUnits(tokenText));
            auto units = distributeLyrics(tokenText, audio.start + tokenStart, audio.start + tokenEnd);
            tokenTimings.insert(tokenTimings.end(), units.begin(), units.end());
        }
        // Some CJK BPE tokens contain only part of a UTF-8 character. Segment text
        // stays authoritative rather than displaying replacement characters.
        if (!tokenTimings.empty() && tokenUnits == lyricUnits(text))
            lyricTimings.insert(lyricTimings.end(), tokenTimings.begin(), tokenTimings.end());
        else
        {
            auto units = distributeLyrics(text, audio.start + start, audio.start + end);
            lyricTimings.insert(lyricTimings.end(), units.begin(), units.end());
        }
    }
    alignTimedLyrics(result, std::move(lyricTimings));
    result.warnings.append("messages.audio_transcription.lyrics_alignment_suggestion");
    if (result.recognizedLyrics.isEmpty())
        result.warnings.append("messages.audio_transcription.no_lyrics");
}
AudioTranscriptionResult transcribeClip(const QString &path, const AudioTranscriptionOptions &options,
                                        const std::shared_ptr<std::atomic_bool> &cancellation,
                                        const AudioTranscriptionProgress &progress, bool allowSilence = false,
                                        double *durationMetadata = nullptr)
{
    QElapsedTimer elapsed;
    elapsed.start();
    AudioTranscriptionResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    try
    {
        checkCancellation(cancellation);
        if (!QFileInfo(result.sourcePath).isFile())
            throw std::runtime_error(
                trText("messages.audio_source.missing_file").arg(result.sourcePath).toStdString());
        if (!std::isfinite(options.startSeconds) || !std::isfinite(options.endSeconds) ||
            options.startSeconds < 0.0 || options.endSeconds <= options.startSeconds ||
            options.endSeconds - options.startSeconds > 120.0 || !std::isfinite(options.bpm) ||
            (options.bpm != 0.0 && (options.bpm < 1.0 || options.bpm > 1000.0)) || options.tonic < -1 ||
            options.tonic > 11)
            fail("messages.audio_transcription.invalid_selection");
        report(progress, 1, "messages.audio_transcription.progress_decode");
        const auto audio = decodeAudio(result.sourcePath, options, cancellation, progress, durationMetadata);
        result.sourceDurationSeconds = audio.duration;
        result.selectedStartSeconds = audio.start;
        result.selectedEndSeconds = audio.end;
        auto samples = analysisSamples(audio.samples, options.enhanceVoice);
        auto segments = extractSegments(samples, cancellation, progress);
        const bool hasPitch = std::any_of(segments.begin(), segments.end(),
                                          [](const PitchSegment &segment) { return segment.pitch >= 0; });
        if (!hasPitch && !allowSilence)
            fail("messages.audio_transcription.no_notes");
        if (!hasPitch)
            result.warnings.append("messages.audio_transcription.silent_chunk");
        result.estimatedBpm = estimateTempo(segments);
        result.estimatedTonic = estimateKey(segments);
        result.warnings.append("messages.audio_transcription.monophonic_suggestion");
        result.warnings.append("messages.audio_transcription.tempo_meter_suggestion");
        if (options.enhanceVoice)
            result.warnings.append("messages.audio_transcription.vocal_filter_suggestion");
        report(progress, 68, "messages.audio_transcription.progress_quantize");
        quantizeScore(result, segments, options.bpm == 0.0 ? result.estimatedBpm : options.bpm,
                      options.tonic < 0 ? result.estimatedTonic : options.tonic, cancellation);
        if (result.score.notes.empty())
            fail("messages.audio_transcription.no_notes");
        if (!options.lyricsText.trimmed().isEmpty())
            applyManualLyrics(result, options.lyricsText);
        else if (options.recognizeLyrics && hasPitch)
        {
            report(progress, 75, "messages.audio_transcription.progress_lyrics");
            recognizeLyrics(result, audio, options, cancellation, progress);
        }
        checkCancellation(cancellation);
        result.elapsedMilliseconds = elapsed.elapsed();
        report(progress, 100, "messages.audio_transcription.progress_ready");
        return result;
    }
    catch (const Cancelled &)
    {
        result.score.notes.clear();
        result.timings.clear();
        result.lyricTimings.clear();
        result.recognizedLyrics.clear();
        result.cancelled = true;
        result.elapsedMilliseconds = elapsed.elapsed();
        return result;
    }
}

void appendChunk(AudioTranscriptionResult &result, const AudioTranscriptionResult &chunk, std::int64_t tickOffset,
                 const std::shared_ptr<std::atomic_bool> &cancellation)
{
    std::vector<int> noteMapping(chunk.score.notes.size(), -1);
    const auto barTicks = static_cast<std::int64_t>(ticksPerBar(result.score));
    if (!result.score.notes.empty())
        result.score.notes.back().tieToNext = false;
    for (const auto &timing : chunk.timings)
    {
        checkCancellation(cancellation);
        const auto &sourceNote = chunk.score.notes[timing.sourceNoteIndex];
        const auto noteStart = tickOffset + timing.startTick;
        const auto noteEnd = tickOffset + timing.endTick;
        for (auto tick = noteStart; tick < noteEnd;)
        {
            if (result.score.notes.size() >= 100000)
                fail("messages.audio_transcription.note_limit");
            const auto pieceEnd = std::min(noteEnd, (tick / barTicks + 1) * barTicks);
            Note note = sourceNote;
            note.id = static_cast<int>(result.score.notes.size());
            note.durationTicks = static_cast<int>(pieceEnd - tick);
            note.measure = static_cast<int>(tick / barTicks);
            note.tieToNext = sourceNote.degree != 0 && (pieceEnd < noteEnd || sourceNote.tieToNext);
            if (tick == noteStart)
            {
                noteMapping[timing.sourceNoteIndex] = note.id;
                if (timing.sourceNoteIndex == 0 && !result.score.notes.empty())
                    note.keyOverride = chunk.score.tonic;
            }
            else
            {
                note.keyOverride = -1;
                note.lyric.clear();
                note.verseLyrics = {""};
            }
            result.score.notes.push_back(std::move(note));
            const double secondsPerTick = (timing.endSeconds - timing.startSeconds) / (noteEnd - noteStart);
            const double startSeconds = tick == noteStart
                                            ? timing.startSeconds
                                            : timing.startSeconds + (tick - noteStart) * secondsPerTick;
            const double endSeconds = pieceEnd == noteEnd
                                          ? timing.endSeconds
                                          : timing.startSeconds + (pieceEnd - noteStart) * secondsPerTick;
            result.timings.push_back(
                {static_cast<int>(result.score.notes.size() - 1),
                 result.timings.empty() ? startSeconds : std::max(startSeconds, result.timings.back().endSeconds),
                 endSeconds, tick, pieceEnd});
            tick = pieceEnd;
        }
    }
    for (auto lyric : chunk.lyricTimings)
    {
        if (lyric.sourceNoteIndex >= 0 && lyric.sourceNoteIndex < static_cast<int>(noteMapping.size()))
            lyric.sourceNoteIndex = noteMapping[lyric.sourceNoteIndex];
        result.lyricTimings.push_back(std::move(lyric));
    }
    if (!chunk.recognizedLyrics.isEmpty())
        result.recognizedLyrics +=
            (result.recognizedLyrics.isEmpty() ? QString{} : QStringLiteral("\n")) + chunk.recognizedLyrics;
    result.warnings.append(chunk.warnings);
}
} // namespace

AudioTranscriptionResult transcribeAudio(const QString &path, const AudioTranscriptionOptions &options,
                                         const std::shared_ptr<std::atomic_bool> &cancellation,
                                         const AudioTranscriptionProgress &progress)
{
    if (options.separateVocals)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        VocalSeparationOptions separationOptions;
        separationOptions.pythonExecutable = options.separatorPython;
        separationOptions.workerScript = options.separatorScript;
        separationOptions.modelDirectory = options.separatorModelDirectory;
        separationOptions.cacheDirectory = options.separatorCacheDirectory;
        const auto separationProgress = [&](int percent, const QString &stage)
        {
            if (progress)
                progress(percent * 70 / 100, stage);
        };
        const auto stems = separateVocals(path, separationOptions, cancellation, separationProgress);
        if (stems.cancelled)
        {
            AudioTranscriptionResult cancelled;
            cancelled.sourcePath = QFileInfo(path).absoluteFilePath();
            cancelled.cancelled = true;
            cancelled.elapsedMilliseconds = elapsed.elapsed();
            return cancelled;
        }
        AudioTranscriptionOptions vocalOptions = options;
        vocalOptions.separateVocals = false;
        const auto vocalProgress = [&](int percent, const QString &stage)
        {
            if (progress)
                progress(70 + percent * 30 / 100, stage);
        };
        auto result = transcribeAudio(stems.vocalsPath, vocalOptions, cancellation, vocalProgress);
        result.sourcePath = stems.originalPath;
        result.score.title = QFileInfo(stems.originalPath).completeBaseName().toStdString();
        result.sourceDurationSeconds = stems.durationSeconds;
        // MF timestamps use 100 ns units; the manifest retains exact sample length.
        result.selectedEndSeconds = std::min(result.selectedEndSeconds, stems.durationSeconds);
        for (auto &timing : result.timings)
            timing.endSeconds = std::min(timing.endSeconds, result.selectedEndSeconds);
        for (auto &lyric : result.lyricTimings)
            lyric.endSeconds = std::min(lyric.endSeconds, result.selectedEndSeconds);
        result.elapsedMilliseconds = elapsed.elapsed();
        if (result.cancelled || (cancellation && cancellation->load(std::memory_order_relaxed)))
        {
            result.score.notes.clear();
            result.timings.clear();
            result.lyricTimings.clear();
            result.recognizedLyrics.clear();
            result.cancelled = true;
            return result;
        }
        result.vocalsSeparated = true;
        result.vocalsPath = stems.vocalsPath;
        result.instrumentalPath = stems.instrumentalPath;
        result.separationModel = stems.model;
        result.separationManifest = stems.manifestPath;
        result.separationSeconds = stems.separationSeconds;
        result.warnings.append("messages.audio_transcription.separated_vocals_suggestion");
        if (stems.cacheHit)
            result.warnings.append("messages.audio_transcription.separation_cache_hit");
        return result;
    }
    if (!options.wholeSong)
        return transcribeClip(path, options, cancellation, progress);
    QElapsedTimer elapsed;
    elapsed.start();
    AudioTranscriptionResult result;
    result.sourcePath = QFileInfo(path).absoluteFilePath();
    const bool manualLyrics = !options.lyricsText.trimmed().isEmpty();
    double duration = 0.0;
    double start = 0.0;
    std::int64_t tickOffset = 0;
    try
    {
        do
        {
            checkCancellation(cancellation);
            AudioTranscriptionOptions chunkOptions = options;
            chunkOptions.wholeSong = false;
            chunkOptions.startSeconds = start;
            chunkOptions.endSeconds = duration == 0.0 ? 120.0 : std::min(start + 120.0, duration);
            chunkOptions.lyricsText.clear();
            if (manualLyrics)
                chunkOptions.recognizeLyrics = false;
            if (!result.score.notes.empty())
                chunkOptions.bpm = result.score.bpm;
            const auto chunkProgress = [&](int percent, const QString &stage)
            {
                if (!progress)
                    return;
                const double chunkEnd = duration == 0.0 ? start : std::min(start + 120.0, duration);
                const int totalPercent =
                    duration == 0.0
                        ? 0
                        : static_cast<int>(100.0 * (start + (chunkEnd - start) * percent / 100.0) / duration);
                progress(std::clamp(totalPercent, 0, 99), stage);
            };
            auto chunk = transcribeClip(path, chunkOptions, cancellation, chunkProgress, true, &duration);
            if (chunk.cancelled)
                throw Cancelled{};
            if (result.score.notes.empty())
            {
                result.score = chunk.score;
                result.score.notes.clear();
                result.estimatedBpm = chunk.estimatedBpm;
                result.estimatedTonic = chunk.estimatedTonic;
                result.sourceDurationSeconds = duration;
                result.selectedEndSeconds = duration;
            }
            appendChunk(result, chunk, tickOffset, cancellation);
            tickOffset = result.timings.back().endTick;
            start = chunk.selectedEndSeconds;
        } while (start < duration);
        if (std::none_of(result.score.notes.begin(), result.score.notes.end(),
                         [](const Note &note) { return note.degree > 0; }))
            fail("messages.audio_transcription.no_notes");
        if (manualLyrics)
            applyManualLyrics(result, options.lyricsText);
        checkCancellation(cancellation);
        result.warnings.append("messages.audio_transcription.whole_song_chunked");
        result.warnings.removeDuplicates();
        result.elapsedMilliseconds = elapsed.elapsed();
        report(progress, 100, "messages.audio_transcription.progress_ready");
        return result;
    }
    catch (const Cancelled &)
    {
        result.score.notes.clear();
        result.timings.clear();
        result.lyricTimings.clear();
        result.recognizedLyrics.clear();
        result.cancelled = true;
        result.elapsedMilliseconds = elapsed.elapsed();
        return result;
    }
}
QJsonObject audioProcessingMetadata(const AudioTranscriptionResult &result,
                                    const AudioTranscriptionOptions &options)
{
    QJsonObject metadata{{"algorithm", "monophonic-pitch-v1"},
                         {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                         {"wholeSong", options.wholeSong},
                         {"requestedStartSeconds", options.startSeconds},
                         {"requestedEndSeconds", options.endSeconds},
                         {"requestedBpm", options.bpm},
                         {"requestedTonic", options.tonic},
                         {"recognizeLyrics", options.recognizeLyrics},
                         {"enhanceVoice", options.enhanceVoice},
                         {"separateVocals", options.separateVocals},
                         {"lyricLanguage", options.language},
                         {"lyricModel", QFileInfo(options.whisperModel).fileName()},
                         {"lyricsProvided", !options.lyricsText.trimmed().isEmpty()},
                         {"elapsedMilliseconds", result.elapsedMilliseconds},
                         {"estimatedBpm", result.estimatedBpm},
                         {"estimatedTonic", result.estimatedTonic},
                         {"separationSeconds", result.separationSeconds}};
    if (!result.separationManifest.isEmpty())
    {
        QFile file(result.separationManifest);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
            fail("messages.package.invalid_metadata");
        const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
        QJsonObject separation;
        for (const char *key :
             {"workerVersion", "inputSHA256", "modelName", "modelSignature", "modelVersion", "modelSHA256",
              "sampleRate", "channels", "inputFrames", "configuration", "torchVersion", "instrumentalMethod"})
            if (manifest.contains(key))
                separation.insert(key, manifest.value(key));
        metadata.insert("separation", separation);
    }
    return metadata;
}
} // namespace singlilt

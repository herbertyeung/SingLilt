// Offline WAV rendering with the current practice settings.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "WaveRenderer.h"
#include "i18n/LanguageManager.h"

#include "SoundFontInstrument.h"
#include "domain/Timeline.h"

#include <QByteArray>
#include <QDataStream>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace singlilt
{
namespace
{

constexpr int SampleRate = 48000;
constexpr int ChannelCount = 2;
constexpr int ChunkFrames = 512;
constexpr qint64 ReleaseFrames = SampleRate * 3 / 2;
constexpr qint64 ClickFrames = SampleRate * 35 / 1000;

qint64 frameAtTick(std::int64_t tick, double bpm)
{
    // Convert each absolute tick boundary, never accumulate rounded durations.
    return static_cast<qint64>(std::llround(static_cast<long double>(tick) * SampleRate * 60.0L /
                                            (static_cast<long double>(bpm) * TicksPerQuarter)));
}

struct Click
{
    qint64 frame;
    bool accent;
};

std::vector<Click> metronomeClicks(const Score &score, const Timeline &timeline, qint64 endFrame,
                                   qint64 startFrame = 0)
{
    std::vector<Click> clicks;
    if (!timeline.metronomeBeats.empty())
    {
        if (timeline.metronomeBeats.size() > 1000000)
            throw std::runtime_error(trText("messages.audio.beat_limit").toStdString());
        std::int64_t previous = -1;
        for (const auto &beat : timeline.metronomeBeats)
        {
            if (beat.startTick <= previous || beat.startTick < 0 || beat.startTick >= timeline.durationTicks)
                throw std::runtime_error(trText("messages.audio.invalid_timeline").toStdString());
            previous = beat.startTick;
            const auto frame = frameAtTick(beat.startTick, timeline.bpm);
            if (frame >= startFrame && frame < endFrame)
                clicks.push_back({frame - startFrame, beat.downbeat});
        }
        return clicks;
    }
    const auto beatTicks = ticksPerMetronomeBeat(score);
    const auto barTicks = ticksPerBar(score);
    // Group written-measure occurrences; a repeat jump starts a new occurrence.
    // The event/tempo limits already bound this schedule independently of audio size.
    for (std::size_t first = 0; first < timeline.events.size();)
    {
        const auto &start = timeline.events[first];
        if (frameAtTick(start.startTick, timeline.bpm) >= endFrame)
            break;
        std::size_t end = first + 1;
        while (end < timeline.events.size())
        {
            const auto &previous = timeline.events[end - 1];
            const auto &next = timeline.events[end];
            if (next.sourceNoteIndex != previous.sourceNoteIndex + 1 ||
                score.notes[next.sourceNoteIndex].measure != score.notes[start.sourceNoteIndex].measure)
                break;
            ++end;
        }
        const auto &last = timeline.events[end - 1];
        const auto endTick = last.startTick + last.durationTicks;
        if (frameAtTick(endTick, timeline.bpm) <= startFrame)
        {
            first = end;
            continue;
        }
        const auto estimatedTick = static_cast<std::int64_t>(static_cast<long double>(startFrame) * timeline.bpm *
                                                             TicksPerQuarter / (SampleRate * 60.0L));
        const auto skippedBeats = std::max<std::int64_t>(0, (estimatedTick - start.startTick) / beatTicks);
        for (auto tick = start.startTick + skippedBeats * beatTicks; tick < endTick; tick += beatTicks)
        {
            const auto frame = frameAtTick(tick, timeline.bpm);
            if (frame >= endFrame)
                break;
            if (frame >= startFrame)
                clicks.push_back({frame - startFrame, (tick - start.startTick) % barTicks == 0});
        }
        first = end;
    }
    return clicks;
}

QByteArray waveHeader(qint64 frames)
{
    const auto dataBytes = static_cast<quint32>(frames * ChannelCount * sizeof(qint16));
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataBytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(ChannelCount) << quint32(SampleRate)
           << quint32(SampleRate * ChannelCount * sizeof(qint16)) << quint16(ChannelCount * sizeof(qint16))
           << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataBytes;
    return bytes;
}

} // namespace

WaveRenderResult renderWave(const Score &score, const QString &output, double maxSeconds, bool metronome)
{
    if (!std::isfinite(maxSeconds) || maxSeconds < 1.0 || maxSeconds > 120.0)
        throw std::runtime_error(trText("messages.audio.wave_duration_cap").toStdString());
    if (output.trimmed().isEmpty())
        throw std::runtime_error(trText("messages.audio.wave_empty_path").toStdString());
    const auto timeline = buildTimeline(score);
    for (const auto &diagnostic : timeline.diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::Error)
            throw std::runtime_error(localizeMessage(QString::fromStdString(diagnostic.message)).toStdString());
    if (timeline.events.empty() || timeline.durationTicks <= 0)
        throw std::runtime_error(trText("messages.audio.wave_no_timeline").toStdString());

    const auto musicFrames = std::min(frameAtTick(timeline.durationTicks, timeline.bpm),
                                      static_cast<qint64>(std::llround(maxSeconds * SampleRate)));
    const auto totalFrames = musicFrames + ReleaseFrames;
    const auto clicks = metronome ? metronomeClicks(score, timeline, musicFrames) : std::vector<Click>{};
    SoundFontInstrument instrument;
    const auto checkSampler = [&instrument](bool ok)
    {
        if (!ok)
            throw std::runtime_error(instrument.errorString().toStdString());
    };
    checkSampler(instrument.open(false));
    checkSampler(instrument.setChannelVolume(0, 0.9));
    checkSampler(instrument.setChannelVolume(9, 0.65));
    WaveRenderResult result;
    result.frames = totalFrames;
    result.musicFrames = musicFrames;
    result.sampleRate = SampleRate;
    result.endSeconds = double(musicFrames) / SampleRate;
    result.fragment = musicFrames < frameAtTick(timeline.durationTicks, timeline.bpm);
    result.engine = instrument.deviceName();
    result.pianoPath = instrument.pianoPath();
    result.gmSoundFontPath = instrument.effectiveGmSoundFontPath();

    QSaveFile file(output);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    const auto header = waveHeader(totalFrames);
    if (file.write(header) != header.size())
        throw std::runtime_error(file.errorString().toStdString());

    std::array<float, ChunkFrames * ChannelCount> samples{};
    std::array<char, ChunkFrames * ChannelCount * sizeof(qint16)> pcm{};
    qint64 renderedFrames = 0;
    long double squareSum = 0.0L;
    std::size_t nextClick = 0;
    int percussionPitch = -1;
    qint64 percussionOff = std::numeric_limits<qint64>::max();
    const auto stopPercussion = [&]
    {
        if (percussionPitch >= 0)
            checkSampler(instrument.noteOff(9, percussionPitch));
        percussionPitch = -1;
        percussionOff = std::numeric_limits<qint64>::max();
    };

    const auto renderUntil = [&](qint64 target)
    {
        while (renderedFrames < target)
        {
            if (percussionOff <= renderedFrames)
                stopPercussion();
            if (nextClick < clicks.size() && clicks[nextClick].frame <= renderedFrames)
            {
                stopPercussion();
                const bool accent = clicks[nextClick].accent;
                percussionPitch = accent ? 76 : 77;
                checkSampler(instrument.noteOn(9, percussionPitch, accent ? 110 : 80));
                percussionOff = renderedFrames + ClickFrames;
                ++nextClick;
            }
            auto end = std::min(target, renderedFrames + ChunkFrames);
            end = std::min(end, percussionOff);
            if (nextClick < clicks.size())
                end = std::min(end, clicks[nextClick].frame);
            const int frames = static_cast<int>(end - renderedFrames);
            if (frames <= 0)
                throw std::runtime_error(trText("messages.audio.wave_boundary").toStdString());
            checkSampler(instrument.render(samples.data(), frames));
            for (int i = 0; i < frames * ChannelCount; ++i)
            {
                const double sample = samples[static_cast<std::size_t>(i)];
                if (!std::isfinite(sample))
                    throw std::runtime_error(trText("messages.audio.wave_nonfinite").toStdString());
                result.peak = std::max(result.peak, std::abs(sample));
                squareSum += static_cast<long double>(sample) * sample;
                if (sample < -1.0 || sample > 1.0)
                    ++result.clippedSamples;
                const auto quantized = static_cast<qint16>(std::lround(std::clamp(sample, -1.0, 1.0) * 32767.0));
                qToLittleEndian<qint16>(quantized, reinterpret_cast<uchar *>(pcm.data() + i * sizeof(qint16)));
            }
            const qint64 byteCount = frames * ChannelCount * sizeof(qint16);
            if (file.write(pcm.data(), byteCount) != byteCount)
                throw std::runtime_error(file.errorString().toStdString());
            renderedFrames = end;
        }
    };

    int heldPitch = -1;
    int currentProgram = -1;
    const auto stopMelody = [&]
    {
        if (heldPitch >= 0)
            checkSampler(instrument.noteOff(0, heldPitch));
        heldPitch = -1;
    };
    for (const auto &event : timeline.events)
    {
        const auto start = frameAtTick(event.startTick, timeline.bpm);
        if (start >= musicFrames)
            break;
        renderUntil(start);
        if (event.midiPitch < 0 || event.attack || heldPitch != event.midiPitch)
            stopMelody();
        if (event.midiPitch >= 0 && heldPitch < 0)
        {
            if (currentProgram != event.program)
            {
                checkSampler(instrument.setProgram(event.program));
                currentProgram = event.program;
            }
            checkSampler(instrument.noteOn(0, event.midiPitch, event.velocity));
            heldPitch = event.midiPitch;
        }
        renderUntil(std::min(frameAtTick(event.startTick + event.durationTicks, timeline.bpm), musicFrames));
    }
    renderUntil(musicFrames);
    stopMelody();
    stopPercussion();
    nextClick = clicks.size();
    renderUntil(totalFrames);
    result.rms = std::sqrt(static_cast<double>(squareSum / (totalFrames * ChannelCount)));
    if (!file.commit())
        throw std::runtime_error(file.errorString().toStdString());
    return result;
}

WaveRenderResult renderWave(const Score &score, const Timeline &timeline, const AccompanimentPlan &plan,
                            const QString &output, const WaveRenderOptions &options)
{
    if (!std::isfinite(options.maxSeconds) || options.maxSeconds < 1.0 || options.maxSeconds > 600.0)
        throw std::runtime_error(trText("messages.audio.wave_extended_duration_cap").toStdString());
    if (output.trimmed().isEmpty())
        throw std::runtime_error(trText("messages.audio.wave_empty_path").toStdString());
    if (!std::isfinite(options.speed) || options.speed < 0.25 || options.speed > 2.0)
        throw std::runtime_error(trText("messages.audio.invalid_speed").toStdString());
    if (options.transpose < -24 || options.transpose > 24)
        throw std::runtime_error(trText("messages.audio.transpose_range").toStdString());
    const auto &mix = options.mix;
    if (!std::isfinite(mix.melodyVolume) || !std::isfinite(mix.accompanimentVolume) || mix.melodyVolume < 0.0 ||
        mix.melodyVolume > 1.0 || mix.accompanimentVolume < 0.0 || mix.accompanimentVolume > 1.0)
        throw std::runtime_error(trText("messages.audio.invalid_practice_mix").toStdString());
    if (!timeline.valid() || timeline.events.empty() || timeline.events.size() > 100000 ||
        timeline.durationTicks <= 0 || !std::isfinite(timeline.bpm) || timeline.bpm < MinimumScoreBpm ||
        timeline.bpm > MaximumScoreBpm || ticksPerBar(score) <= 0 || ticksPerMetronomeBeat(score) <= 0)
        throw std::runtime_error(trText("messages.audio.wave_no_timeline").toStdString());
    std::int64_t previousEnd = 0;
    for (const auto &event : timeline.events)
    {
        if (event.sourceNoteIndex >= score.notes.size() || event.startTick < previousEnd ||
            event.startTick >= timeline.durationTicks || event.durationTicks <= 0 ||
            event.durationTicks > MaximumNoteDurationTicks ||
            event.durationTicks > timeline.durationTicks - event.startTick || event.midiPitch < -1 ||
            event.midiPitch > 127 || event.velocity < 0 || event.velocity > 127 ||
            (event.midiPitch >= 0 && event.velocity == 0) || event.program < 0 || event.program > 127)
            throw std::runtime_error(trText("messages.audio.invalid_timeline").toStdString());
        if (event.midiPitch >= 0 &&
            (event.midiPitch + options.transpose < 0 || event.midiPitch + options.transpose > 127))
            throw std::runtime_error(trText("messages.audio.transpose_range").toStdString());
        previousEnd = event.startTick + event.durationTicks;
    }
    if (!plan.valid() || plan.events.size() > 1000000 ||
        (!plan.events.empty() && plan.durationTicks != timeline.durationTicks) ||
        options.settings.chordProgram < 0 || options.settings.chordProgram > 127 ||
        options.settings.bassProgram < 0 || options.settings.bassProgram > 127)
        throw std::runtime_error(trText("messages.audio.invalid_accompaniment").toStdString());
    for (const auto &event : plan.events)
    {
        if (event.startTick < 0 || event.startTick >= timeline.durationTicks || event.durationTicks <= 0 ||
            event.durationTicks > timeline.durationTicks - event.startTick || event.midiPitch < 0 ||
            event.midiPitch > 127 || event.velocity < 1 || event.velocity > 127 ||
            (event.role != AccompanimentRole::Chord && event.role != AccompanimentRole::Bass))
            throw std::runtime_error(trText("messages.audio.invalid_accompaniment").toStdString());
        if (event.midiPitch + options.transpose < 0 || event.midiPitch + options.transpose > 127)
            throw std::runtime_error(trText("messages.audio.transpose_range").toStdString());
    }

    // Scheduling uses exactly the supplied plans; no harmony generation occurs here.
    const double bpm = timeline.bpm * options.speed;
    const auto fullFrames = frameAtTick(timeline.durationTicks, bpm);
    const double fullSeconds = double(fullFrames) / SampleRate;
    if (!std::isfinite(options.startSeconds) || options.startSeconds < 0.0 ||
        options.startSeconds >= fullSeconds || !std::isfinite(options.endSeconds) ||
        (options.endSeconds != -1.0 &&
         (options.endSeconds <= options.startSeconds || options.endSeconds > fullSeconds + 0.5 / SampleRate ||
          options.endSeconds - options.startSeconds > options.maxSeconds + 0.5 / SampleRate)))
        throw std::runtime_error(trText("messages.export.invalid_range").toStdString());
    const auto startFrame = static_cast<qint64>(std::llround(options.startSeconds * SampleRate));
    const auto capFrames = static_cast<qint64>(std::llround(options.maxSeconds * SampleRate));
    const auto endFrame =
        options.endSeconds < 0.0
            ? std::min(fullFrames, startFrame + capFrames)
            : std::min(fullFrames, static_cast<qint64>(std::llround(options.endSeconds * SampleRate)));
    const auto musicFrames = endFrame - startFrame;
    if (musicFrames <= 0 || musicFrames > capFrames)
        throw std::runtime_error(trText("messages.export.invalid_range").toStdString());
    const auto totalFrames = musicFrames + ReleaseFrames;
    auto clickTimeline = timeline;
    clickTimeline.bpm = bpm;
    const auto clicks =
        options.metronome ? metronomeClicks(score, clickTimeline, endFrame, startFrame) : std::vector<Click>{};
    struct NoteCommand
    {
        qint64 frame;
        int channel;
        int pitch;
        int velocity;
        int program;
        bool noteOn;
    };
    std::vector<NoteCommand> commands;
    const auto appendSpan =
        [&](std::int64_t startTick, std::int64_t endTick, int channel, int pitch, int velocity, int program)
    {
        const auto start = std::max(frameAtTick(startTick, bpm), startFrame) - startFrame;
        const auto end = std::min(frameAtTick(endTick, bpm), endFrame) - startFrame;
        if (start >= musicFrames || end <= start)
            return;
        commands.push_back({start, channel, pitch + options.transpose, velocity, program, true});
        commands.push_back({end, channel, pitch + options.transpose, velocity, program, false});
    };
    if (mix.melodyEnabled && !options.settings.originalStaff)
    {
        for (std::size_t first = 0; first < timeline.events.size();)
        {
            const auto &event = timeline.events[first];
            auto endTick = event.startTick + event.durationTicks;
            std::size_t next = first + 1;
            while (next < timeline.events.size())
            {
                const auto &following = timeline.events[next];
                if (following.attack || following.startTick != endTick || following.midiPitch != event.midiPitch ||
                    following.program != event.program)
                    break;
                endTick = following.startTick + following.durationTicks;
                ++next;
            }
            if (event.midiPitch >= 0)
                appendSpan(event.startTick, endTick, 0, event.midiPitch, event.velocity, event.program);
            first = next;
        }
    }
    if (mix.accompanimentEnabled || (options.settings.originalStaff && mix.melodyEnabled))
    {
        for (const auto &event : plan.events)
        {
            const int channel = event.role == AccompanimentRole::Chord ? 1 : 2;
            if (options.settings.originalStaff && !(channel == 1 ? mix.melodyEnabled : mix.accompanimentEnabled))
                continue;
            const int program = channel == 1 ? options.settings.chordProgram : options.settings.bassProgram;
            appendSpan(event.startTick, event.startTick + event.durationTicks, channel, event.midiPitch,
                       event.velocity, program);
        }
    }
    std::stable_sort(commands.begin(), commands.end(),
                     [](const NoteCommand &left, const NoteCommand &right)
                     {
                         if (left.frame != right.frame)
                             return left.frame < right.frame;
                         return left.noteOn < right.noteOn;
                     });

    SoundFontInstrument instrument;
    const auto checkSampler = [&instrument](bool ok)
    {
        if (!ok)
            throw std::runtime_error(instrument.errorString().toStdString());
    };
    if (!options.gmSoundFontPath.isNull())
        checkSampler(instrument.setGmSoundFontPath(options.gmSoundFontPath));
    instrument.setOutputBoost(options.outputBoost);
    checkSampler(instrument.open(false));
    checkSampler(instrument.setChannelVolume(0, mix.melodyVolume));
    checkSampler(instrument.setChannelVolume(1, options.settings.originalStaff ? mix.melodyVolume
                                                                               : mix.accompanimentVolume));
    checkSampler(instrument.setChannelVolume(2, mix.accompanimentVolume));
    checkSampler(instrument.setChannelVolume(9, 0.65));
    WaveRenderResult result;
    result.frames = totalFrames;
    result.musicFrames = musicFrames;
    result.startSeconds = double(startFrame) / SampleRate;
    result.endSeconds = double(endFrame) / SampleRate;
    result.fragment = startFrame > 0 || endFrame < fullFrames;
    result.engine = instrument.deviceName();
    result.pianoPath = instrument.pianoPath();
    result.gmSoundFontPath = instrument.effectiveGmSoundFontPath();
    QSaveFile file(output);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    const auto header = waveHeader(totalFrames);
    if (file.write(header) != header.size())
        throw std::runtime_error(file.errorString().toStdString());

    std::array<std::array<int, 128>, 3> voiceCounts{};
    std::array<int, 3> appliedPrograms{-1, -1, -1};
    std::array<float, ChunkFrames * ChannelCount> samples{};
    std::array<char, ChunkFrames * ChannelCount * sizeof(qint16)> pcm{};
    std::size_t nextCommand = 0;
    std::size_t nextClick = 0;
    int percussionPitch = -1;
    qint64 percussionOff = std::numeric_limits<qint64>::max();
    qint64 renderedFrames = 0;
    long double squareSum = 0.0L;
    const auto stopPercussion = [&]
    {
        if (percussionPitch >= 0)
            checkSampler(instrument.noteOff(9, percussionPitch));
        percussionPitch = -1;
        percussionOff = std::numeric_limits<qint64>::max();
    };
    while (renderedFrames < totalFrames)
    {
        while (nextCommand < commands.size() && commands[nextCommand].frame <= renderedFrames)
        {
            const auto command = commands[nextCommand++];
            auto &count = voiceCounts[command.channel][command.pitch];
            if (command.noteOn)
            {
                if (count == 0)
                {
                    if (appliedPrograms[command.channel] != command.program)
                    {
                        checkSampler(instrument.setProgram(command.channel, command.program));
                        appliedPrograms[command.channel] = command.program;
                    }
                    checkSampler(instrument.noteOn(command.channel, command.pitch, command.velocity));
                }
                ++count;
            }
            else if (--count == 0)
            {
                checkSampler(instrument.noteOff(command.channel, command.pitch));
            }
        }
        if (renderedFrames >= musicFrames)
        {
            stopPercussion();
            nextClick = clicks.size();
        }
        else
        {
            if (percussionOff <= renderedFrames)
                stopPercussion();
            if (nextClick < clicks.size() && clicks[nextClick].frame <= renderedFrames)
            {
                stopPercussion();
                const bool accent = clicks[nextClick++].accent;
                percussionPitch = accent ? 76 : 77;
                checkSampler(instrument.noteOn(9, percussionPitch, accent ? 110 : 80));
                percussionOff = renderedFrames + ClickFrames;
            }
        }
        auto end = std::min(totalFrames, renderedFrames + ChunkFrames);
        if (renderedFrames < musicFrames)
            end = std::min(end, musicFrames);
        if (nextCommand < commands.size())
            end = std::min(end, commands[nextCommand].frame);
        if (nextClick < clicks.size())
            end = std::min(end, clicks[nextClick].frame);
        end = std::min(end, percussionOff);
        const int frames = static_cast<int>(end - renderedFrames);
        if (frames <= 0)
            throw std::runtime_error(trText("messages.audio.wave_boundary").toStdString());
        checkSampler(instrument.render(samples.data(), frames));
        for (int i = 0; i < frames * ChannelCount; ++i)
        {
            const double sample = samples[static_cast<std::size_t>(i)];
            if (!std::isfinite(sample))
                throw std::runtime_error(trText("messages.audio.wave_nonfinite").toStdString());
            result.peak = std::max(result.peak, std::abs(sample));
            squareSum += static_cast<long double>(sample) * sample;
            if (sample < -1.0 || sample > 1.0)
                ++result.clippedSamples;
            const auto quantized = static_cast<qint16>(std::lround(std::clamp(sample, -1.0, 1.0) * 32767.0));
            qToLittleEndian<qint16>(quantized, reinterpret_cast<uchar *>(pcm.data() + i * sizeof(qint16)));
        }
        const qint64 byteCount = frames * ChannelCount * sizeof(qint16);
        if (file.write(pcm.data(), byteCount) != byteCount)
            throw std::runtime_error(file.errorString().toStdString());
        renderedFrames = end;
    }
    result.rms = std::sqrt(static_cast<double>(squareSum / (totalFrames * ChannelCount)));
    if (!file.commit())
        throw std::runtime_error(file.errorString().toStdString());
    return result;
}

} // namespace singlilt

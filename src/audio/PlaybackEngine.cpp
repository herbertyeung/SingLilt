// Timeline transport and melody/accompaniment event scheduling.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "PlaybackEngine.h"
#include "InstrumentOutput.h"
#include "i18n/LanguageManager.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>
#endif

namespace singlilt
{

namespace
{
using Clock = std::chrono::steady_clock;

// Matched lifetime; reduces wake-up quantization, not a claim of hard realtime.
struct TimerResolution
{
#ifdef _WIN32
    bool active = timeBeginPeriod(1) == TIMERR_NOERROR;
    ~TimerResolution()
    {
        if (active)
            timeEndPeriod(1);
    }
#endif
};

struct SoundSpan
{
    std::int64_t start = 0;
    std::int64_t end = 0;
    int pitch = -1;
    int program = 0;
    int velocity = 100;
};

// Kept separate from merged sound spans: a sustained tie may cross a verse.
struct VerseMarker
{
    std::int64_t tick = 0;
    int verseIndex = 0;
};

struct Beat
{
    std::int64_t tick = 0;
    bool accent = false;
};

struct NotePreview
{
    int pitch;
    int program;
    int velocity;
    int durationMilliseconds;
};

struct AccompanimentBoundary
{
    std::int64_t tick = 0;
    std::size_t eventIndex = 0;
    bool noteOn = false;
};

int roleIndex(AccompanimentRole role)
{
    return role == AccompanimentRole::Chord ? 0 : 1;
}

bool validPlan(const AccompanimentPlan &plan, const AccompanimentSettings &settings, std::int64_t totalTicks,
               int transpose)
{
    if (!plan.valid() || plan.events.size() > 1000000 ||
        (!plan.events.empty() && plan.durationTicks != totalTicks) || settings.chordProgram < 0 ||
        settings.chordProgram > 127 || settings.bassProgram < 0 || settings.bassProgram > 127)
        return false;
    for (const auto &event : plan.events)
    {
        if (event.startTick < 0 || event.startTick >= totalTicks || event.durationTicks <= 0 ||
            event.durationTicks > totalTicks - event.startTick || event.midiPitch < 0 || event.midiPitch > 127 ||
            event.midiPitch + transpose < 0 || event.midiPitch + transpose > 127 || event.velocity < 1 ||
            event.velocity > 127 ||
            (event.role != AccompanimentRole::Chord && event.role != AccompanimentRole::Bass))
            return false;
    }
    return true;
}
} // namespace

struct PlaybackEngine::Impl
{
    mutable std::mutex mutex;
    std::condition_variable changed;
    InstrumentOutput instrument;
    std::vector<SoundSpan> spans;
    std::vector<VerseMarker> verses;
    std::vector<Beat> beats;
    std::int64_t totalTicks = 0;
    double bpm = 90.0;
    double rate = 1.0;
    PracticeMix mix;
    double clickVolume = 0.65;
    int semitones = 0;
    std::optional<int> programOverride;
    int appliedProgram = -1;
    bool metronome = false;
    bool playing = false;
    bool loaded = false;
    double anchorTick = 0.0;
    Clock::time_point anchorTime = Clock::now();
    std::optional<std::size_t> activeSpan;
    int activePitch = -1;
    int activeClick = -1;
    AccompanimentPlan accompaniment;
    AccompanimentSettings accompanimentSettings;
    std::vector<AccompanimentBoundary> accompanimentBoundaries;
    std::vector<bool> activeAccompanimentEvents;
    std::array<std::array<int, 128>, 2> accompanimentVoices{};
    std::array<std::uint64_t, 3> noteOns{};
    std::size_t nextAccompanimentBoundary = 0;
    bool restoreAccompaniment = true;
    std::array<bool, 2> restoreAccompanimentRoles{true, true};
    Clock::time_point clickOff = Clock::time_point::max();
    std::optional<NotePreview> requestedPreview;
    std::atomic_bool previewLoading{false};
    int previewPitch = -1;
    int previewProgram = 0;
    int previewVelocity = 0;
    std::uint64_t previewNoteOns = 0;
    Clock::time_point previewOff = Clock::time_point::max();
    std::size_t nextBeat = 0;
    QString error;
    std::jthread worker;

    Impl() : worker([this](std::stop_token stop) { run(stop); }) {}

    ~Impl()
    {
        {
            std::lock_guard lock(mutex);
            worker.request_stop();
        }
        changed.notify_all();
        worker.join();
        instrument.allNotesOff();
        instrument.close();
    }

    double ticksPerSecond() const
    {
        return bpm * TicksPerQuarter * rate / 60.0;
    }

    double position(Clock::time_point now) const
    {
        const double elapsed = playing ? std::chrono::duration<double>(now - anchorTime).count() : 0.0;
        return std::clamp(anchorTick + elapsed * ticksPerSecond(), 0.0, static_cast<double>(totalTicks));
    }

    void rebase(Clock::time_point now)
    {
        anchorTick = position(now);
        anchorTime = now;
    }

    int programAt(double tick) const
    {
        if (programOverride)
            return *programOverride;
        if (spans.empty())
            return 0;
        const auto next =
            std::upper_bound(spans.begin(), spans.end(), tick, [](double target, const SoundSpan &span)
                             { return target < static_cast<double>(span.start); });
        return next == spans.begin() ? spans.front().program : std::prev(next)->program;
    }

    int verseAt(double tick) const
    {
        if (verses.empty())
            return 0;
        const auto next =
            std::upper_bound(verses.begin(), verses.end(), tick, [](double target, const VerseMarker &marker)
                             { return target < static_cast<double>(marker.tick); });
        return next == verses.begin() ? verses.front().verseIndex : std::prev(next)->verseIndex;
    }

    bool silence()
    {
        requestedPreview.reset();
        previewLoading = false;
        previewPitch = -1;
        previewVelocity = 0;
        previewOff = Clock::time_point::max();
        activeSpan.reset();
        activePitch = -1;
        activeClick = -1;
        clickOff = Clock::time_point::max();
        accompanimentVoices = {};
        std::fill(activeAccompanimentEvents.begin(), activeAccompanimentEvents.end(), false);
        restoreAccompaniment = true;
        restoreAccompanimentRoles = {true, true};
        if (instrument.allNotesOff())
            return true;
        error = instrument.errorString();
        return false;
    }

    bool check(bool success)
    {
        if (success)
            return true;
        rebase(Clock::now());
        playing = false;
        const QString failure = instrument.errorString();
        silence();
        error = failure;
        instrument.close();
        appliedProgram = -1;
        return false;
    }

    bool applyProgram(int program)
    {
        if (appliedProgram == program)
            return true;
        if (!check(instrument.setProgram(program)))
            return false;
        appliedProgram = program;
        return true;
    }

    bool applyAccompanimentPrograms()
    {
        const int chordProgram = accompanimentSettings.chordProgram;
        const int bassProgram = accompanimentSettings.bassProgram;
        return check(instrument.setProgram(1, chordProgram)) && check(instrument.setProgram(2, bassProgram));
    }

    bool accompanimentRoleEnabled(int role) const
    {
        return accompanimentSettings.originalStaff && role == 0 ? mix.melodyEnabled : mix.accompanimentEnabled;
    }

    bool anyAccompanimentEnabled() const
    {
        return accompanimentRoleEnabled(0) || accompanimentRoleEnabled(1);
    }

    bool applyMixVolumes()
    {
        return check(instrument.setChannelVolume(0, mix.melodyVolume)) &&
               check(instrument.setChannelVolume(
                   1, accompanimentSettings.originalStaff ? mix.melodyVolume : mix.accompanimentVolume)) &&
               check(instrument.setChannelVolume(2, mix.accompanimentVolume));
    }

    bool silenceAccompanimentRole(int role)
    {
        accompanimentVoices[role] = {};
        for (std::size_t i = 0; i < accompaniment.events.size(); ++i)
            if (roleIndex(accompaniment.events[i].role) == role)
                activeAccompanimentEvents[i] = false;
        restoreAccompanimentRoles[role] = true;
        return !instrument.isOpen() || check(instrument.silenceChannel(role + 1));
    }

    bool silenceAccompaniment()
    {
        accompanimentVoices = {};
        std::fill(activeAccompanimentEvents.begin(), activeAccompanimentEvents.end(), false);
        restoreAccompaniment = true;
        restoreAccompanimentRoles = {true, true};
        if (!instrument.isOpen())
            return true;
        bool success = instrument.silenceChannel(1);
        success = instrument.silenceChannel(2) && success;
        return check(success);
    }

    void assignAccompaniment(const AccompanimentPlan &plan, const AccompanimentSettings &settings)
    {
        accompaniment = plan;
        accompanimentSettings = settings;
        accompanimentBoundaries.clear();
        accompanimentBoundaries.reserve(plan.events.size() * 2);
        for (std::size_t i = 0; i < plan.events.size(); ++i)
        {
            const auto &event = plan.events[i];
            accompanimentBoundaries.push_back({event.startTick, i, true});
            accompanimentBoundaries.push_back({event.startTick + event.durationTicks, i, false});
        }
        std::sort(accompanimentBoundaries.begin(), accompanimentBoundaries.end(),
                  [](const AccompanimentBoundary &left, const AccompanimentBoundary &right)
                  {
                      if (left.tick != right.tick)
                          return left.tick < right.tick;
                      if (left.noteOn != right.noteOn)
                          return !left.noteOn;
                      return left.eventIndex < right.eventIndex;
                  });
        activeAccompanimentEvents.assign(plan.events.size(), false);
        accompanimentVoices = {};
        restoreAccompaniment = true;
        restoreAccompanimentRoles = {true, true};
    }

    bool startAccompanimentEvent(std::size_t index)
    {
        if (activeAccompanimentEvents[index])
            return true;
        const auto &event = accompaniment.events[index];
        const int role = roleIndex(event.role);
        const int pitch = event.midiPitch + semitones;
        auto &count = accompanimentVoices[role][pitch];
        // A shared channel/pitch has one MIDI lifetime, even if intervals overlap.
        if (count == 0)
        {
            if (!check(instrument.noteOn(role + 1, pitch, event.velocity)))
                return false;
            ++noteOns[role + 1];
        }
        ++count;
        activeAccompanimentEvents[index] = true;
        return true;
    }

    bool updateAccompaniment(double tick)
    {
        if (!anyAccompanimentEnabled())
            return true;
        if (restoreAccompaniment)
        {
            nextAccompanimentBoundary = static_cast<std::size_t>(
                std::upper_bound(accompanimentBoundaries.begin(), accompanimentBoundaries.end(), tick,
                                 [](double target, const AccompanimentBoundary &boundary)
                                 { return target < static_cast<double>(boundary.tick); }) -
                accompanimentBoundaries.begin());
            restoreAccompaniment = false;
        }
        for (int role = 0; role < 2; ++role)
        {
            if (!restoreAccompanimentRoles[role] || !accompanimentRoleEnabled(role))
                continue;
            for (std::size_t i = 0; i < accompaniment.events.size(); ++i)
            {
                const auto &event = accompaniment.events[i];
                if (roleIndex(event.role) == role && !activeAccompanimentEvents[i] && event.startTick <= tick &&
                    tick < event.startTick + event.durationTicks && !startAccompanimentEvent(i))
                    return false;
            }
            restoreAccompanimentRoles[role] = false;
        }
        while (nextAccompanimentBoundary < accompanimentBoundaries.size() &&
               accompanimentBoundaries[nextAccompanimentBoundary].tick <= tick)
        {
            const auto boundary = accompanimentBoundaries[nextAccompanimentBoundary++];
            const auto &event = accompaniment.events[boundary.eventIndex];
            const int role = roleIndex(event.role);
            const int pitch = event.midiPitch + semitones;
            if (boundary.noteOn)
            {
                // Do not emit obsolete short notes after a delayed worker wake-up.
                if (accompanimentRoleEnabled(role) && event.startTick + event.durationTicks > tick &&
                    !startAccompanimentEvent(boundary.eventIndex))
                    return false;
            }
            else if (activeAccompanimentEvents[boundary.eventIndex])
            {
                activeAccompanimentEvents[boundary.eventIndex] = false;
                auto &count = accompanimentVoices[role][pitch];
                if (--count == 0 && !check(instrument.noteOff(role + 1, pitch)))
                    return false;
            }
        }
        return true;
    }

    void resetBeatCursor(double tick)
    {
        nextBeat = static_cast<std::size_t>(std::lower_bound(beats.begin(), beats.end(), tick,
                                                             [](const Beat &beat, double target)
                                                             { return static_cast<double>(beat.tick) < target; }) -
                                            beats.begin());
    }

    Clock::time_point timeAt(double tick) const
    {
        return anchorTime + std::chrono::duration_cast<Clock::duration>(
                                std::chrono::duration<double>((tick - anchorTick) / ticksPerSecond()));
    }

    void run(std::stop_token stop)
    {
        TimerResolution timerResolution;
        std::unique_lock lock(mutex);
        while (!stop.stop_requested())
        {
            if (requestedPreview)
            {
                const auto preview = *requestedPreview;
                requestedPreview.reset();
                bool accepted = true;
                if (instrument.isOpen())
                    accepted = check(instrument.silenceChannel(3));
                previewPitch = -1;
                previewVelocity = 0;
                previewOff = Clock::time_point::max();
                if (accepted && preview.pitch >= 0 && check(instrument.open()) &&
                    check(instrument.setProgram(3, preview.program)) &&
                    check(instrument.setChannelVolume(3, mix.melodyVolume)) &&
                    check(instrument.noteOn(3, preview.pitch, preview.velocity)))
                {
                    previewPitch = preview.pitch;
                    previewProgram = preview.program;
                    previewVelocity = preview.velocity;
                    ++previewNoteOns;
                    previewOff = Clock::now() + std::chrono::milliseconds(preview.durationMilliseconds);
                }
                previewLoading = false;
            }
            if (previewPitch >= 0 && Clock::now() >= previewOff)
            {
                if (!check(instrument.noteOff(3, previewPitch)))
                    continue;
                previewPitch = -1;
                previewVelocity = 0;
                previewOff = Clock::time_point::max();
            }
            if (!playing)
            {
                if (previewPitch >= 0)
                    changed.wait_until(lock, previewOff);
                else
                    changed.wait(lock,
                                 [&] { return stop.stop_requested() || playing || requestedPreview.has_value(); });
                continue;
            }
            const auto now = Clock::now();
            const double tick = position(now);
            if (tick >= static_cast<double>(totalTicks))
            {
                anchorTick = static_cast<double>(totalTicks);
                playing = false;
                silence();
                continue;
            }

            auto next = std::upper_bound(spans.begin(), spans.end(), tick, [](double target, const SoundSpan &span)
                                         { return target < static_cast<double>(span.start); });
            std::optional<std::size_t> desired;
            if (next != spans.begin())
            {
                const auto current = std::prev(next);
                if (tick < static_cast<double>(current->end))
                    desired = static_cast<std::size_t>(current - spans.begin());
            }
            if (desired != activeSpan)
            {
                if (activePitch >= 0 && !check(instrument.noteOff(0, activePitch)))
                    continue;
                activePitch = -1;
                activeSpan = desired;
                // Establish the verse's timbre even if its first event is a rest.
                // Only channel 0 changes; percussion continues independently.
                if (desired && !applyProgram(programOverride.value_or(spans[*desired].program)))
                    continue;
                if (!accompanimentSettings.originalStaff && mix.melodyEnabled && desired &&
                    spans[*desired].pitch >= 0)
                {
                    activePitch = spans[*desired].pitch + semitones;
                    if (!check(instrument.noteOn(0, activePitch, spans[*desired].velocity)))
                        continue;
                    ++noteOns[0];
                }
            }

            if (!updateAccompaniment(tick))
                continue;

            if (activeClick >= 0 && now >= clickOff)
            {
                if (!check(instrument.noteOff(9, activeClick)))
                    continue;
                activeClick = -1;
                clickOff = Clock::time_point::max();
            }
            std::optional<std::size_t> dueBeat;
            while (nextBeat < beats.size() && static_cast<double>(beats[nextBeat].tick) <= tick)
                dueBeat = nextBeat++;
            if (metronome && dueBeat)
            {
                if (activeClick >= 0 && !check(instrument.noteOff(9, activeClick)))
                    continue;
                activeClick = beats[*dueBeat].accent ? 76 : 77;
                if (!check(instrument.noteOn(9, activeClick, beats[*dueBeat].accent ? 110 : 80)))
                    continue;
                clickOff = now + std::chrono::milliseconds(35);
            }

            double wakeTick = static_cast<double>(totalTicks);
            if (desired)
                wakeTick = std::min(wakeTick, static_cast<double>(spans[*desired].end));
            if (next != spans.end())
                wakeTick = std::min(wakeTick, static_cast<double>(next->start));
            if (nextBeat < beats.size())
                wakeTick = std::min(wakeTick, static_cast<double>(beats[nextBeat].tick));
            if (anyAccompanimentEnabled() && nextAccompanimentBoundary < accompanimentBoundaries.size())
                wakeTick = std::min(wakeTick,
                                    static_cast<double>(accompanimentBoundaries[nextAccompanimentBoundary].tick));
            changed.wait_until(lock, std::min({timeAt(wakeTick), clickOff, previewOff}));
        }
        silence();
    }
};

PlaybackEngine::PlaybackEngine() : impl_(std::make_unique<Impl>()) {}
PlaybackEngine::~PlaybackEngine() = default;

bool PlaybackEngine::load(const Score &score)
{
    return load(score, buildTimeline(score));
}

bool PlaybackEngine::load(const Score &score, const Timeline &timeline)
{
    return load(score, timeline, {});
}

bool PlaybackEngine::load(const Score &score, const Timeline &timeline, const AccompanimentPlan &plan,
                          const AccompanimentSettings &settings)
{
    std::lock_guard lock(impl_->mutex);
    impl_->playing = false;
    impl_->anchorTick = 0.0;
    impl_->silence();
    impl_->loaded = false;
    impl_->spans.clear();
    impl_->verses.clear();
    impl_->beats.clear();
    impl_->programOverride.reset();
    impl_->assignAccompaniment({}, {});
    impl_->noteOns = {};
    impl_->totalTicks = 0;
    impl_->error.clear();
    if (!timeline.valid() || timeline.events.empty() || timeline.events.size() > 100000 ||
        timeline.durationTicks <= 0 || !std::isfinite(timeline.bpm) || timeline.bpm < MinimumScoreBpm ||
        timeline.bpm > MaximumScoreBpm || ticksPerBar(score) <= 0 || ticksPerMetronomeBeat(score) <= 0)
    {
        impl_->error = trText("messages.audio.no_playable_timeline");
        impl_->changed.notify_all();
        return false;
    }
    if (!validPlan(plan, settings, timeline.durationTicks, impl_->semitones))
    {
        impl_->error = trText("messages.audio.invalid_accompaniment");
        impl_->changed.notify_all();
        return false;
    }
    std::int64_t previousEnd = 0;
    for (const auto &event : timeline.events)
    {
        if (event.sourceNoteIndex >= score.notes.size() || event.startTick < previousEnd ||
            event.startTick > timeline.durationTicks || event.durationTicks <= 0 ||
            event.durationTicks > MaximumNoteDurationTicks ||
            event.durationTicks > timeline.durationTicks - event.startTick || event.midiPitch < -1 ||
            event.midiPitch > 127 || event.program < 0 || event.program > 127 || event.verseIndex < 0 ||
            event.velocity < 0 || event.velocity > 127 || (event.midiPitch >= 0 && event.velocity == 0) ||
            (event.midiPitch >= 0 &&
             (event.midiPitch + impl_->semitones < 0 || event.midiPitch + impl_->semitones > 127)))
        {
            impl_->error = trText("messages.audio.invalid_timeline");
            impl_->spans.clear();
            impl_->verses.clear();
            return false;
        }
        const auto end = event.startTick + event.durationTicks;
        if (!event.attack && !impl_->spans.empty() && impl_->spans.back().end == event.startTick &&
            impl_->spans.back().pitch == event.midiPitch && impl_->spans.back().program == event.program)
        {
            impl_->spans.back().end = end;
        }
        else
        {
            impl_->spans.push_back({event.startTick, end, event.midiPitch, event.program, event.velocity});
        }
        if (impl_->verses.empty() || impl_->verses.back().verseIndex != event.verseIndex)
            impl_->verses.push_back({event.startTick, event.verseIndex});
        previousEnd = end;
    }

    // Start each written measure occurrence independently, including repeat jumps.
    // A long unsplit measure (e.g. a diagnostic score) still accents each bar.
    const auto barTicks = static_cast<std::int64_t>(ticksPerBar(score));
    const auto beatTicks = static_cast<std::int64_t>(ticksPerMetronomeBeat(score));
    if (!timeline.metronomeBeats.empty())
    {
        if (timeline.metronomeBeats.size() > 1000000)
        {
            impl_->error = trText("messages.audio.beat_limit");
            return false;
        }
        std::int64_t previousBeat = -1;
        for (const auto &beat : timeline.metronomeBeats)
        {
            if (beat.startTick <= previousBeat || beat.startTick < 0 || beat.startTick >= timeline.durationTicks)
            {
                impl_->error = trText("messages.audio.invalid_timeline");
                return false;
            }
            impl_->beats.push_back({beat.startTick, beat.downbeat});
            previousBeat = beat.startTick;
        }
    }
    else
    {
        std::size_t group = 0;
        while (group < timeline.events.size())
        {
            std::size_t end = group + 1;
            const int measure = score.notes[timeline.events[group].sourceNoteIndex].measure;
            while (end < timeline.events.size() &&
                   score.notes[timeline.events[end].sourceNoteIndex].measure == measure &&
                   timeline.events[end].sourceNoteIndex == timeline.events[end - 1].sourceNoteIndex + 1)
                ++end;
            const auto startTick = timeline.events[group].startTick;
            const auto endTick =
                end < timeline.events.size() ? timeline.events[end].startTick : timeline.durationTicks;
            for (auto tick = startTick; tick < endTick; tick += beatTicks)
            {
                if (impl_->beats.size() >= 1000000)
                {
                    impl_->error = trText("messages.audio.beat_limit");
                    impl_->spans.clear();
                    impl_->verses.clear();
                    impl_->beats.clear();
                    return false;
                }
                impl_->beats.push_back({tick, (tick - startTick) % barTicks == 0});
            }
            group = end;
        }
    }
    impl_->bpm = timeline.bpm;
    impl_->totalTicks = timeline.durationTicks;
    impl_->assignAccompaniment(plan, settings);
    impl_->loaded = true;
    impl_->nextBeat = 0;
    if (impl_->instrument.isOpen() && !impl_->applyProgram(impl_->programAt(0.0)))
    {
        impl_->loaded = false;
        return false;
    }
    impl_->changed.notify_all();
    return true;
}

bool PlaybackEngine::setAccompanimentPlan(const AccompanimentPlan &plan, const AccompanimentSettings &settings)
{
    std::lock_guard lock(impl_->mutex);
    if (!impl_->loaded || !validPlan(plan, settings, impl_->totalTicks, impl_->semitones))
    {
        impl_->error = trText("messages.audio.invalid_accompaniment");
        return false;
    }
    const bool staffRoutingChanged = impl_->accompanimentSettings.originalStaff != settings.originalStaff;
    if (staffRoutingChanged)
    {
        impl_->activeSpan.reset();
        impl_->activePitch = -1;
        if (impl_->instrument.isOpen() && !impl_->check(impl_->instrument.silenceChannel(0)))
            return false;
    }
    if (!impl_->silenceAccompaniment())
        return false;
    impl_->assignAccompaniment(plan, settings);
    if (impl_->instrument.isOpen() &&
        (!impl_->applyAccompanimentPrograms() || (staffRoutingChanged && !impl_->applyMixVolumes())))
        return false;
    impl_->error.clear();
    impl_->changed.notify_all();
    return true;
}

bool PlaybackEngine::setAccompanimentSettings(const AccompanimentSettings &settings)
{
    std::lock_guard lock(impl_->mutex);
    if (!impl_->loaded || settings.originalStaff != impl_->accompanimentSettings.originalStaff ||
        !validPlan(impl_->accompaniment, settings, impl_->totalTicks, impl_->semitones))
    {
        impl_->error = trText("messages.audio.invalid_accompaniment");
        return false;
    }
    const std::array<bool, 2> changed{settings.chordProgram != impl_->accompanimentSettings.chordProgram,
                                      settings.bassProgram != impl_->accompanimentSettings.bassProgram};
    const std::array<int, 2> programs{settings.chordProgram, settings.bassProgram};
    for (int role = 0; role < 2; ++role)
    {
        if (!changed[role])
            continue;
        if (!impl_->silenceAccompanimentRole(role))
            return false;
        if (impl_->instrument.isOpen() && !impl_->check(impl_->instrument.setProgram(role + 1, programs[role])))
            return false;
    }
    impl_->accompanimentSettings = settings;
    impl_->error.clear();
    impl_->changed.notify_all();
    return true;
}

bool PlaybackEngine::setPracticeMix(const PracticeMix &mix)
{
    std::lock_guard lock(impl_->mutex);
    if (!std::isfinite(mix.melodyVolume) || !std::isfinite(mix.accompanimentVolume) || mix.melodyVolume < 0.0 ||
        mix.melodyVolume > 1.0 || mix.accompanimentVolume < 0.0 || mix.accompanimentVolume > 1.0)
    {
        impl_->error = trText("messages.audio.invalid_practice_mix");
        return false;
    }
    if (impl_->accompanimentSettings.originalStaff)
    {
        if (impl_->mix.melodyEnabled != mix.melodyEnabled && !impl_->silenceAccompanimentRole(0))
            return false;
        if (impl_->mix.accompanimentEnabled != mix.accompanimentEnabled && !impl_->silenceAccompanimentRole(1))
            return false;
    }
    else
    {
        if (impl_->mix.melodyEnabled != mix.melodyEnabled)
        {
            impl_->activeSpan.reset();
            impl_->activePitch = -1;
            if (impl_->instrument.isOpen() && !impl_->check(impl_->instrument.silenceChannel(0)))
                return false;
        }
        if (impl_->mix.accompanimentEnabled != mix.accompanimentEnabled && !impl_->silenceAccompaniment())
            return false;
    }
    impl_->mix = mix;
    if (impl_->instrument.isOpen() && !impl_->applyMixVolumes())
        return false;
    impl_->error.clear();
    impl_->changed.notify_all();
    return true;
}

PracticeMix PlaybackEngine::practiceMix() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->mix;
}

PlaybackVoiceState PlaybackEngine::voiceState() const
{
    std::lock_guard lock(impl_->mutex);
    PlaybackVoiceState state;
    state.melodyPitch = impl_->activePitch;
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        if (impl_->accompanimentVoices[0][pitch] > 0)
            state.chordPitches.push_back(pitch);
        if (impl_->accompanimentVoices[1][pitch] > 0)
            state.bassPitches.push_back(pitch);
    }
    state.melodyNoteOns = impl_->noteOns[0];
    state.chordNoteOns = impl_->noteOns[1];
    state.bassNoteOns = impl_->noteOns[2];
    state.previewPitch = impl_->previewPitch;
    state.previewProgram = impl_->previewProgram;
    state.previewVelocity = impl_->previewVelocity;
    state.previewNoteOns = impl_->previewNoteOns;
    return state;
}

bool PlaybackEngine::previewNote(int midiPitch, int program, int velocity, int durationMilliseconds)
{
    if (midiPitch < -1 || midiPitch > 127 || program < 0 || program > 127 || velocity < 1 || velocity > 127 ||
        durationMilliseconds < 50 || durationMilliseconds > 2000)
        return false;
    std::lock_guard lock(impl_->mutex);
    impl_->error.clear();
    impl_->requestedPreview = NotePreview{midiPitch, program, velocity, durationMilliseconds};
    impl_->previewLoading = true;
    impl_->changed.notify_all();
    return true;
}

bool PlaybackEngine::isPreviewLoading() const
{
    return impl_->previewLoading.load();
}

bool PlaybackEngine::play()
{
    std::lock_guard lock(impl_->mutex);
    if (impl_->playing)
        return true;
    if (!impl_->loaded)
    {
        impl_->error = trText("messages.audio.load_score_first");
        return false;
    }
    impl_->error.clear();
    for (const auto &span : impl_->spans)
    {
        if (span.pitch >= 0 && (span.pitch + impl_->semitones < 0 || span.pitch + impl_->semitones > 127))
        {
            impl_->error = trText("messages.audio.transpose_range");
            return false;
        }
    }
    if (!validPlan(impl_->accompaniment, impl_->accompanimentSettings, impl_->totalTicks, impl_->semitones))
    {
        impl_->error = trText("messages.audio.invalid_accompaniment");
        return false;
    }
    if (impl_->anchorTick >= static_cast<double>(impl_->totalTicks))
        impl_->anchorTick = 0.0;
    if (!impl_->check(impl_->instrument.open()) || !impl_->applyProgram(impl_->programAt(impl_->anchorTick)) ||
        !impl_->applyAccompanimentPrograms() || !impl_->applyMixVolumes() ||
        !impl_->check(impl_->instrument.setChannelVolume(9, impl_->clickVolume)))
        return false;
    impl_->activeSpan.reset();
    impl_->restoreAccompaniment = true;
    impl_->resetBeatCursor(impl_->anchorTick);
    impl_->anchorTime = Clock::now();
    impl_->playing = true;
    impl_->changed.notify_all();
    return true;
}

bool PlaybackEngine::setAudioBackend(AudioBackend backend)
{
    std::lock_guard lock(impl_->mutex);
    if (backend == impl_->instrument.backend())
        return true;
    impl_->rebase(Clock::now());
    impl_->playing = false;
    impl_->silence();
    if (!impl_->instrument.setBackend(backend))
    {
        impl_->error = impl_->instrument.errorString();
        impl_->changed.notify_all();
        return false;
    }
    impl_->appliedProgram = -1;
    impl_->error.clear();
    impl_->changed.notify_all();
    return true;
}

AudioBackend PlaybackEngine::audioBackend() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->instrument.backend();
}

bool PlaybackEngine::setGmSoundFontPath(const QString &path)
{
    std::lock_guard lock(impl_->mutex);
    if (path == impl_->instrument.gmSoundFontPath())
        return true;
    // The sampler validates the path before closing; rejected choices keep playing.
    if (!impl_->instrument.setGmSoundFontPath(path))
    {
        impl_->error = impl_->instrument.errorString();
        return false;
    }
    impl_->rebase(Clock::now());
    impl_->playing = false;
    impl_->silence();
    impl_->appliedProgram = -1;
    impl_->error.clear();
    impl_->changed.notify_all();
    return true;
}

QString PlaybackEngine::gmSoundFontPath() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->instrument.gmSoundFontPath();
}

void PlaybackEngine::pause()
{
    std::lock_guard lock(impl_->mutex);
    impl_->rebase(Clock::now());
    impl_->playing = false;
    impl_->silence();
    impl_->changed.notify_all();
}

void PlaybackEngine::releaseAudioDevice()
{
    std::lock_guard lock(impl_->mutex);
    impl_->rebase(Clock::now());
    impl_->playing = false;
    impl_->silence();
    impl_->instrument.close();
    impl_->appliedProgram = -1;
    impl_->changed.notify_all();
}

void PlaybackEngine::stop()
{
    std::lock_guard lock(impl_->mutex);
    impl_->playing = false;
    impl_->anchorTick = 0.0;
    impl_->silence();
    if (impl_->instrument.isOpen())
        impl_->applyProgram(impl_->programAt(0.0));
    impl_->nextBeat = 0;
    impl_->changed.notify_all();
}

void PlaybackEngine::seek(std::int64_t tick)
{
    std::lock_guard lock(impl_->mutex);
    impl_->anchorTick = static_cast<double>(std::clamp<std::int64_t>(tick, 0, impl_->totalTicks));
    impl_->anchorTime = Clock::now();
    if (!impl_->silence())
        impl_->playing = false;
    if (impl_->instrument.isOpen())
        impl_->applyProgram(impl_->programAt(impl_->anchorTick));
    impl_->resetBeatCursor(impl_->anchorTick);
    impl_->changed.notify_all();
}

void PlaybackEngine::setSpeed(double factor)
{
    std::lock_guard lock(impl_->mutex);
    if (!std::isfinite(factor))
    {
        impl_->error = trText("messages.audio.invalid_speed");
        return;
    }
    impl_->rebase(Clock::now());
    impl_->rate = std::clamp(factor, 0.25, 2.0);
    impl_->changed.notify_all();
}

bool PlaybackEngine::setTranspose(int semitones)
{
    std::lock_guard lock(impl_->mutex);
    if (semitones < -24 || semitones > 24)
    {
        impl_->error = trText("messages.audio.transpose_range");
        return false;
    }
    for (const auto &span : impl_->spans)
    {
        if (span.pitch >= 0 && (span.pitch + semitones < 0 || span.pitch + semitones > 127))
        {
            impl_->error = trText("messages.audio.transpose_range");
            return false;
        }
    }
    for (const auto &event : impl_->accompaniment.events)
    {
        if (event.midiPitch + semitones < 0 || event.midiPitch + semitones > 127)
        {
            impl_->error = trText("messages.audio.transpose_range");
            return false;
        }
    }
    if (impl_->semitones == semitones)
        return true;
    impl_->rebase(Clock::now());
    impl_->semitones = semitones;
    const bool silenced = impl_->silence();
    if (!silenced)
        impl_->playing = false;
    impl_->resetBeatCursor(impl_->anchorTick);
    impl_->changed.notify_all();
    return silenced;
}

void PlaybackEngine::setMetronome(bool enabled)
{
    std::lock_guard lock(impl_->mutex);
    if (impl_->metronome == enabled)
        return;
    impl_->metronome = enabled;
    impl_->resetBeatCursor(impl_->position(Clock::now()));
    if (!enabled && impl_->instrument.isOpen())
    {
        impl_->check(impl_->instrument.silenceChannel(9));
        impl_->activeClick = -1;
        impl_->clickOff = Clock::time_point::max();
    }
    impl_->changed.notify_all();
}

void PlaybackEngine::setVolume(double volume)
{
    std::lock_guard lock(impl_->mutex);
    if (!std::isfinite(volume))
    {
        impl_->error = trText("messages.audio.invalid_volume");
        return;
    }
    impl_->mix.melodyVolume = std::clamp(volume, 0.0, 1.0);
    if (impl_->instrument.isOpen())
        impl_->check(impl_->instrument.setChannelVolume(impl_->accompanimentSettings.originalStaff ? 1 : 0,
                                                        impl_->mix.melodyVolume));
}

void PlaybackEngine::setMetronomeVolume(double volume)
{
    std::lock_guard lock(impl_->mutex);
    if (!std::isfinite(volume))
    {
        impl_->error = trText("messages.audio.invalid_metronome_volume");
        return;
    }
    impl_->clickVolume = std::clamp(volume, 0.0, 1.0);
    if (impl_->instrument.isOpen())
        impl_->check(impl_->instrument.setChannelVolume(9, impl_->clickVolume));
}

void PlaybackEngine::setOutputBoost(bool enabled)
{
    std::lock_guard lock(impl_->mutex);
    impl_->instrument.setOutputBoost(enabled);
}

void PlaybackEngine::setProgram(int program)
{
    std::lock_guard lock(impl_->mutex);
    impl_->programOverride = std::clamp(program, 0, 127);
    if (impl_->instrument.isOpen())
    {
        impl_->rebase(Clock::now());
        impl_->activeSpan.reset();
        impl_->activePitch = -1;
        if (!impl_->check(impl_->instrument.silenceChannel(0)))
            return;
        impl_->applyProgram(*impl_->programOverride);
        impl_->changed.notify_all();
    }
}

std::int64_t PlaybackEngine::positionTicks() const
{
    std::lock_guard lock(impl_->mutex);
    return static_cast<std::int64_t>(impl_->position(Clock::now()));
}

std::int64_t PlaybackEngine::durationTicks() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->totalTicks;
}

bool PlaybackEngine::isPlaying() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->playing;
}

double PlaybackEngine::speed() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->rate;
}

int PlaybackEngine::transpose() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->semitones;
}

int PlaybackEngine::currentProgram() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->programAt(impl_->position(Clock::now()));
}

int PlaybackEngine::currentVerseIndex() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->verseAt(impl_->position(Clock::now()));
}

QString PlaybackEngine::errorString() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->error;
}

QString PlaybackEngine::deviceName() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->instrument.deviceName();
}

} // namespace singlilt

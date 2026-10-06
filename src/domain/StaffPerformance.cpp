// Complete staff voices, measures, ties, and performance validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffPerformance.h"
#include "AudioSource.h"
#include <algorithm>
#include <deque>
#include <map>
#include <tuple>

namespace singlilt
{
std::string staffTimingFingerprint(const Score &score)
{
    return "staff:" + audioTimingFingerprint(score);
}

AccompanimentPlan buildStaffPerformancePlan(const Score &score, const Timeline &timeline,
                                            const StaffPerformance &performance)
{
    AccompanimentPlan plan;
    plan.durationTicks = timeline.durationTicks;
    const auto fail = [&](const char *message)
    { plan.diagnostics.push_back({DiagnosticSeverity::Error, message, -1}); };
    if (!timeline.valid() || performance.timingFingerprint != staffTimingFingerprint(score))
    {
        fail("messages.staff.performance_stale");
        return plan;
    }
    std::vector<std::int64_t> starts{0};
    for (const auto &note : score.notes)
        starts.push_back(starts.back() + note.durationTicks);
    if (performance.durationTicks != starts.back() || performance.notes.empty() ||
        performance.notes.size() > 100000 || performance.staffCount < 1 || performance.staffCount > 2 ||
        performance.primaryStaff < 1 || performance.primaryStaff > performance.staffCount)
    {
        fail("messages.staff.invalid_performance");
        return plan;
    }
    auto notes = performance.notes;
    std::stable_sort(notes.begin(), notes.end(),
                     [](const auto &left, const auto &right)
                     {
                         return std::tie(left.startTick, left.staff, left.voice, left.midiPitch) <
                                std::tie(right.startTick, right.staff, right.voice, right.midiPitch);
                     });
    std::vector<StaffPerformanceNote> sounding;
    std::map<std::tuple<int, std::string, int>, std::map<std::int64_t, std::deque<std::size_t>>> pendingTies;
    const int keyShift = score.tonic - performance.sourceTonic;
    for (const auto &note : notes)
    {
        if (note.startTick < 0 || note.durationTicks <= 0 || note.startTick >= performance.durationTicks ||
            note.durationTicks > performance.durationTicks - note.startTick || note.midiPitch + keyShift < 0 ||
            note.midiPitch + keyShift > 127 || note.staff < 1 || note.staff > performance.staffCount ||
            note.velocity < 1 || note.velocity > 127 || note.voice.empty())
        {
            fail("messages.staff.invalid_performance");
            return plan;
        }
        if (note.unresolvedSoundTie)
        {
            sounding.push_back(note);
            plan.diagnostics.push_back(
                {DiagnosticSeverity::Warning, "messages.staff.unresolved_sound_tie", note.sourceNoteIndex});
            continue;
        }
        const auto key = std::make_tuple(note.staff, note.voice, note.midiPitch);
        if (note.tieStop)
        {
            const auto previous = pendingTies.find(key);
            if (previous == pendingTies.end())
            {
                fail("messages.staff.invalid_performance_tie");
                return plan;
            }
            auto ending = previous->second.find(note.startTick);
            if (ending == previous->second.end() || ending->second.empty())
            {
                fail("messages.staff.invalid_performance_tie");
                return plan;
            }
            const auto heldIndex = ending->second.front();
            ending->second.pop_front();
            if (ending->second.empty())
                previous->second.erase(ending);
            auto &held = sounding[heldIndex];
            held.durationTicks += note.durationTicks;
            held.tieStart = note.tieStart;
            if (note.tieStart)
                pendingTies[key][held.startTick + held.durationTicks].push_back(heldIndex);
            continue;
        }
        if (note.tieStart)
            pendingTies[key][note.startTick + note.durationTicks].push_back(sounding.size());
        sounding.push_back(note);
    }
    struct Run
    {
        std::int64_t sourceStart;
        std::int64_t sourceEnd;
        std::int64_t playbackStart;
    };
    if (std::any_of(sounding.begin(), sounding.end(),
                    [](const auto &note) { return note.tieStart && !note.unresolvedSoundTie; }))
    {
        fail("messages.staff.invalid_performance_tie");
        return plan;
    }
    std::vector<Run> runs;
    for (const auto &event : timeline.events)
    {
        const auto start = starts[event.sourceNoteIndex];
        if (!runs.empty() && runs.back().sourceEnd == start &&
            runs.back().playbackStart + runs.back().sourceEnd - runs.back().sourceStart == event.startTick)
            runs.back().sourceEnd = start + event.durationTicks;
        else
            runs.push_back({start, start + event.durationTicks, event.startTick});
    }
    std::size_t comparisons = 0;
    for (const auto &run : runs)
        for (const auto &note : sounding)
        {
            if (++comparisons > 20000000)
            {
                fail("messages.staff.performance_limit");
                return plan;
            }
            const auto start = std::max(run.sourceStart, note.startTick);
            const auto end = std::min(run.sourceEnd, note.startTick + note.durationTicks);
            if (start >= end)
                continue;
            if (plan.events.size() >= 400000)
            {
                fail("messages.staff.performance_limit");
                return plan;
            }
            plan.events.push_back(
                {run.playbackStart + start - run.sourceStart, end - start, note.midiPitch + keyShift,
                 note.velocity,
                 note.staff == performance.primaryStaff ? AccompanimentRole::Chord : AccompanimentRole::Bass});
        }
    std::stable_sort(plan.events.begin(), plan.events.end(),
                     [](const auto &left, const auto &right)
                     {
                         return std::tie(left.startTick, left.role, left.midiPitch) <
                                std::tie(right.startTick, right.role, right.midiPitch);
                     });
    return plan;
}
} // namespace singlilt

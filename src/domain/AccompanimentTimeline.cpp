// Chord and bass events on the expanded playback timeline.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AccompanimentTimeline.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

namespace singlilt
{
namespace
{

constexpr std::size_t MaximumAccompanimentEvents = 400000;

struct SourceRun
{
    std::int64_t startTick;
    std::int64_t endTick;
    std::int64_t playbackStartTick;
};

void error(AccompanimentPlan &plan, const char *message)
{
    plan.diagnostics.push_back({DiagnosticSeverity::Error, message});
}

std::array<int, 3> voicing(const ChordSpan &chord)
{
    int root = 48 + chord.rootPitchClass;
    if (root > 55)
        root -= 12;
    std::array<int, 3> pitches{root, root + (chord.quality == ChordQuality::Major ? 4 : 3),
                               root + (chord.quality == ChordQuality::Diminished ? 6 : 7)};
    for (int i = 0; i < chord.inversion; ++i)
    {
        const int lowest = pitches[0];
        pitches[0] = pitches[1];
        pitches[1] = pitches[2];
        pitches[2] = lowest + 12;
    }
    return pitches;
}

bool appendEvent(std::vector<AccompanimentEvent> &events, AccompanimentEvent event)
{
    if (events.size() >= MaximumAccompanimentEvents)
        return false;
    events.push_back(event);
    return true;
}

bool sourceEvents(const Score &score, const AccompanimentArrangement &arrangement,
                  std::vector<AccompanimentEvent> &events)
{
    const int groupTicks = score.beatUnit == 8 && score.beatsPerBar == 6 ? 720
                           : score.beatsPerBar == 4                      ? 960
                                                                         : ticksPerBar(score);
    const int arpeggioTicks = score.beatUnit == 8 ? 240 : TicksPerQuarter / 2;
    int previousBass = -1;
    for (const auto &chord : arrangement.chords)
    {
        if (chord.quality == ChordQuality::None)
            continue;
        const auto pitches = voicing(chord);
        int bass = 36 + pitches[0] % 12;
        if (previousBass >= 0 && bass + 12 <= 55 &&
            std::abs(bass + 12 - previousBass) < std::abs(bass - previousBass))
            bass += 12;
        previousBass = bass;
        const auto pattern = chord.patternOverride.value_or(arrangement.settings.pattern);
        if (pattern == AccompanimentPattern::Sparse)
        {
            // One short, open voicing per bar leaves the other beats for the singer.
            for (auto tick = chord.startTick; tick < chord.endTick; tick += ticksPerBar(score))
            {
                const auto duration = std::min<std::int64_t>(ticksPerMetronomeBeat(score), chord.endTick - tick);
                if (!appendEvent(events, {tick, duration, bass, arrangement.settings.bassVelocity,
                                          AccompanimentRole::Bass}))
                    return false;
                for (const auto voice : {std::size_t{0}, std::size_t{2}})
                {
                    if (!appendEvent(events, {tick, duration, pitches[voice], arrangement.settings.chordVelocity,
                                              AccompanimentRole::Chord}))
                        return false;
                }
            }
            continue;
        }
        for (auto tick = chord.startTick; tick < chord.endTick; tick += groupTicks)
        {
            const auto duration = std::min<std::int64_t>(groupTicks, chord.endTick - tick);
            if (!appendEvent(events,
                             {tick, duration, bass, arrangement.settings.bassVelocity, AccompanimentRole::Bass}))
                return false;
            if (pattern == AccompanimentPattern::BlockChords)
            {
                for (const int pitch : pitches)
                {
                    if (!appendEvent(events, {tick, duration, pitch, arrangement.settings.chordVelocity,
                                              AccompanimentRole::Chord}))
                        return false;
                }
            }
        }
        if (pattern == AccompanimentPattern::Arpeggio)
        {
            std::size_t voice = 0;
            for (auto tick = chord.startTick; tick < chord.endTick; tick += arpeggioTicks)
            {
                const auto duration = std::min<std::int64_t>(arpeggioTicks, chord.endTick - tick);
                if (!appendEvent(events, {tick, duration, pitches[voice], arrangement.settings.chordVelocity,
                                          AccompanimentRole::Chord}))
                    return false;
                voice = (voice + 1) % pitches.size();
            }
        }
    }
    std::sort(events.begin(), events.end(), [](const AccompanimentEvent &left, const AccompanimentEvent &right)
              { return left.startTick < right.startTick; });
    return true;
}

bool sourceRuns(const Score &score, const Timeline &timeline, std::vector<SourceRun> &runs)
{
    std::vector<std::int64_t> sourceTicks{0};
    sourceTicks.reserve(score.notes.size() + 1);
    for (const auto &note : score.notes)
        sourceTicks.push_back(sourceTicks.back() + note.durationTicks);
    std::int64_t playbackTick = 0;
    std::size_t previousSource = score.notes.size();
    for (const auto &event : timeline.events)
    {
        if (event.sourceNoteIndex >= score.notes.size() || event.startTick != playbackTick ||
            event.durationTicks != score.notes[event.sourceNoteIndex].durationTicks)
            return false;
        const auto sourceStart = sourceTicks[event.sourceNoteIndex];
        const auto sourceEnd = sourceTicks[event.sourceNoteIndex + 1];
        if (!runs.empty() && event.sourceNoteIndex == previousSource + 1 && runs.back().endTick == sourceStart)
            runs.back().endTick = sourceEnd;
        else
            runs.push_back({sourceStart, sourceEnd, event.startTick});
        playbackTick += event.durationTicks;
        previousSource = event.sourceNoteIndex;
    }
    return !timeline.events.empty() && playbackTick == timeline.durationTicks;
}

} // namespace

bool AccompanimentPlan::valid() const
{
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic)
                        { return diagnostic.severity == DiagnosticSeverity::Error; });
}

AccompanimentPlan buildAccompanimentPlan(const Score &score, const Timeline &timeline,
                                         const AccompanimentArrangement &arrangement)
{
    AccompanimentPlan plan;
    plan.durationTicks = timeline.durationTicks;
    plan.diagnostics = validateAccompaniment(score, arrangement);
    if (!timeline.valid())
        plan.diagnostics.insert(plan.diagnostics.end(), timeline.diagnostics.begin(), timeline.diagnostics.end());
    if (!plan.valid())
        return plan;
    std::vector<SourceRun> runs;
    if (!sourceRuns(score, timeline, runs))
    {
        error(plan, "messages.accompaniment.timeline_mapping");
        return plan;
    }
    std::vector<AccompanimentEvent> source;
    if (!sourceEvents(score, arrangement, source))
    {
        error(plan, "messages.accompaniment.event_limit");
        return plan;
    }
    // Within a run, short melody notes do not restart a held chord. A source jump
    // ends that run, clipping every old voice before restoring the target passage.
    for (const auto &run : runs)
    {
        const auto first = std::lower_bound(source.begin(), source.end(), run.startTick - ticksPerBar(score),
                                            [](const AccompanimentEvent &event, std::int64_t tick)
                                            { return event.startTick < tick; });
        for (auto it = first; it != source.end() && it->startTick < run.endTick; ++it)
        {
            const auto start = std::max(it->startTick, run.startTick);
            const auto end = std::min(it->startTick + it->durationTicks, run.endTick);
            if (start >= end)
                continue;
            auto event = *it;
            event.startTick = run.playbackStartTick + start - run.startTick;
            event.durationTicks = end - start;
            if (!appendEvent(plan.events, event))
            {
                plan.events.clear();
                error(plan, "messages.accompaniment.event_limit");
                return plan;
            }
        }
    }
    std::sort(plan.events.begin(), plan.events.end(),
              [](const AccompanimentEvent &left, const AccompanimentEvent &right)
              {
                  if (left.startTick != right.startTick)
                      return left.startTick < right.startTick;
                  if (left.role != right.role)
                      return left.role < right.role;
                  return left.midiPitch < right.midiPitch;
              });
    return plan;
}

} // namespace singlilt

// Repeat expansion and source-note playback occurrences.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "Timeline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace singlilt
{
namespace
{

constexpr std::size_t MaximumEvents = 100000;
constexpr std::size_t MaximumWrittenMeasures = 10000;
constexpr std::size_t MaximumMetronomeBeats = 1000000;
constexpr std::int64_t MaximumWrittenTicks = 1000000000;

void error(Timeline &timeline, std::string message, int noteIndex = -1)
{
    timeline.diagnostics.push_back({DiagnosticSeverity::Error, std::move(message), noteIndex});
}

void warning(Timeline &timeline, std::string message, int noteIndex = -1)
{
    timeline.diagnostics.push_back({DiagnosticSeverity::Warning, std::move(message), noteIndex});
}

bool validSourceRect(const SourceRect &source)
{
    return std::isfinite(source.x) && std::isfinite(source.y) && std::isfinite(source.width) &&
           std::isfinite(source.height) && source.x >= 0.0 && source.y >= 0.0 && source.width >= 0.0 &&
           source.height >= 0.0;
}

std::vector<int> sourceVelocities(const Score &score)
{
    std::vector<int> velocities(score.notes.size(), 0);
    if (!score.writtenMeasures.empty())
    {
        std::size_t measureIndex = 0;
        std::int64_t tick = 0;
        for (std::size_t i = 0; i < score.notes.size(); ++i)
        {
            while (measureIndex + 1 < score.writtenMeasures.size() &&
                   tick >= score.writtenMeasures[measureIndex].startTick +
                               score.writtenMeasures[measureIndex].durationTicks)
                ++measureIndex;
            const auto &measure = score.writtenMeasures[measureIndex];
            const auto &note = score.notes[i];
            if (note.degree != 0)
            {
                int adjustment = 0;
                if (score.accentBeats)
                {
                    const auto localTick = tick - measure.startTick;
                    const bool pickup = measureIndex == 0 && measure.durationTicks < ticksPerBar(measure);
                    if (localTick == 0)
                        adjustment = pickup ? 0 : 8;
                    else if (localTick % ticksPerMetronomeBeat(measure) != 0)
                        adjustment = -4;
                }
                velocities[i] = std::clamp(score.baseVelocity + adjustment, 1, 127);
            }
            tick += note.durationTicks;
        }
        return velocities;
    }
    const int beatTicks = ticksPerMetronomeBeat(score);
    const int barTicks = ticksPerBar(score);
    // Contiguous written-measure groups, not expanded playback ticks, determine
    // accents. Repeating a source note therefore never introduces random dynamics.
    for (std::size_t first = 0; first < score.notes.size();)
    {
        std::size_t end = first;
        std::int64_t measureTicks = 0;
        while (end < score.notes.size() && score.notes[end].measure == score.notes[first].measure)
        {
            measureTicks += score.notes[end].durationTicks;
            ++end;
        }
        // A shorter initial group is treated as a pickup, not a full downbeat.
        const bool pickup = first == 0 && measureTicks < barTicks;
        std::int64_t localTick = 0;
        for (auto i = first; i < end; ++i)
        {
            if (score.notes[i].degree != 0)
            {
                int adjustment = 0;
                if (score.accentBeats)
                {
                    if (localTick == 0)
                        adjustment = pickup ? 0 : 8;
                    else if (localTick % beatTicks != 0)
                        adjustment = -4;
                }
                velocities[i] = std::clamp(score.baseVelocity + adjustment, 1, 127);
            }
            localTick += score.notes[i].durationTicks;
        }
        first = end;
    }
    return velocities;
}

void validateWrittenMeasures(const Score &score, Timeline &timeline, std::int64_t sourceDuration)
{
    if (score.writtenMeasures.empty())
        return;
    if (score.writtenMeasures.size() > MaximumWrittenMeasures)
    {
        error(timeline, "messages.domain.written_measure_range");
        return;
    }
    std::int64_t nextTick = 0;
    int previousPage = 0;
    for (const auto &measure : score.writtenMeasures)
    {
        if (measure.startTick != nextTick || measure.startTick < 0 || measure.startTick > MaximumWrittenTicks ||
            measure.durationTicks <= 0 || measure.durationTicks > MaximumWrittenTicks - measure.startTick ||
            measure.number < -1 || measure.number > 1000000 || ticksPerBar(measure) == 0 ||
            (measure.number == -1 && (measure.startTick != 0 || measure.durationTicks >= ticksPerBar(measure))) ||
            measure.pageIndex < previousPage || measure.pageIndex > 9999)
        {
            error(timeline, "messages.domain.written_measure_range");
            return;
        }
        nextTick += measure.durationTicks;
        previousPage = measure.pageIndex;
    }
    if (nextTick != sourceDuration)
    {
        error(timeline, "messages.domain.written_measure_coverage");
        return;
    }
    std::size_t measureIndex = 0;
    std::int64_t tick = 0;
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        while (measureIndex + 1 < score.writtenMeasures.size() &&
               tick >= score.writtenMeasures[measureIndex].startTick +
                           score.writtenMeasures[measureIndex].durationTicks)
            ++measureIndex;
        const auto &measure = score.writtenMeasures[measureIndex];
        if (tick < measure.startTick || score.notes[i].durationTicks <= 0 ||
            tick + score.notes[i].durationTicks > measure.startTick + measure.durationTicks ||
            score.notes[i].pageIndex != measure.pageIndex)
        {
            error(timeline, "messages.domain.written_measure_coverage", static_cast<int>(i));
            return;
        }
        tick += score.notes[i].durationTicks;
    }
}

void appendWrittenMetronomeBeats(const Score &score, Timeline &timeline)
{
    if (score.writtenMeasures.empty())
        return;
    std::vector<MetronomeBeat> sourceBeats;
    for (std::size_t i = 0; i < score.writtenMeasures.size(); ++i)
    {
        const auto &measure = score.writtenMeasures[i];
        const int beatTicks = ticksPerMetronomeBeat(measure);
        const bool pickup = i == 0 && measure.durationTicks < ticksPerBar(measure);
        for (auto tick = measure.startTick; tick < measure.startTick + measure.durationTicks; tick += beatTicks)
        {
            if (sourceBeats.size() >= MaximumMetronomeBeats)
            {
                error(timeline, "messages.domain.metronome_limit");
                return;
            }
            sourceBeats.push_back({tick, tick == measure.startTick && !pickup});
        }
    }
    std::vector<std::int64_t> noteStarts{0};
    for (const auto &note : score.notes)
        noteStarts.push_back(noteStarts.back() + note.durationTicks);
    for (const auto &event : timeline.events)
    {
        const auto sourceStart = noteStarts[event.sourceNoteIndex];
        const auto sourceEnd = sourceStart + event.durationTicks;
        auto beat =
            std::lower_bound(sourceBeats.begin(), sourceBeats.end(), sourceStart,
                             [](const MetronomeBeat &value, std::int64_t tick) { return value.startTick < tick; });
        while (beat != sourceBeats.end() && beat->startTick < sourceEnd)
        {
            if (timeline.metronomeBeats.size() >= MaximumMetronomeBeats)
            {
                error(timeline, "messages.domain.metronome_limit");
                return;
            }
            timeline.metronomeBeats.push_back({event.startTick + beat->startTick - sourceStart, beat->downbeat});
            ++beat;
        }
    }
}

} // namespace

bool Timeline::valid() const
{
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic)
                        { return diagnostic.severity == DiagnosticSeverity::Error; });
}

double Timeline::durationSeconds() const
{
    return secondsAtTick(durationTicks);
}

double Timeline::secondsAtTick(std::int64_t tick) const
{
    if (!std::isfinite(bpm) || bpm <= 0.0)
        return 0.0;
    return static_cast<double>(clampTick(tick)) * 60.0 / (bpm * TicksPerQuarter);
}

std::int64_t Timeline::tickAtSeconds(double seconds) const
{
    if (std::isnan(seconds) || seconds <= 0.0 || !std::isfinite(bpm) || bpm <= 0.0)
        return 0;
    if (seconds >= durationSeconds())
        return durationTicks;
    return clampTick(static_cast<std::int64_t>(std::llround(seconds * bpm * TicksPerQuarter / 60.0)));
}

std::int64_t Timeline::clampTick(std::int64_t tick) const
{
    return std::clamp(tick, std::int64_t{0}, std::max(std::int64_t{0}, durationTicks));
}

std::optional<std::size_t> Timeline::eventIndexAtTick(std::int64_t tick) const
{
    if (tick < 0 || tick >= durationTicks || events.empty())
        return std::nullopt;
    const auto it =
        std::upper_bound(events.begin(), events.end(), tick, [](std::int64_t position, const TimelineEvent &event)
                         { return position < event.startTick; });
    if (it == events.begin())
        return std::nullopt;
    return static_cast<std::size_t>(std::distance(events.begin(), it) - 1);
}

Timeline buildTimeline(const Score &score, int transpose)
{
    Timeline timeline;
    timeline.bpm = score.bpm;
    if (!std::isfinite(score.bpm) || score.bpm < MinimumScoreBpm || score.bpm > MaximumScoreBpm)
        error(timeline, "messages.domain.tempo_range");
    if (score.tonic < 0 || score.tonic > 11)
        error(timeline, "messages.domain.key_range");
    if (ticksPerBar(score) == 0)
        error(timeline, "messages.domain.meter_range");
    if (score.notes.empty())
        error(timeline, "messages.domain.no_notes");
    if (score.notes.size() > MaximumScoreNotes)
        error(timeline, "messages.domain.note_limit");
    if (score.baseVelocity < 1 || score.baseVelocity > 127)
        error(timeline, "messages.domain.velocity_range");
    if (score.versePrograms.empty() || score.versePrograms.size() > 16)
        error(timeline, "messages.domain.program_count");
    if (std::any_of(score.versePrograms.begin(), score.versePrograms.end(),
                    [](int program) { return program < 0 || program > 127; }))
        error(timeline, "messages.domain.program_range");

    std::vector<int> pitches;
    pitches.reserve(std::min(score.notes.size(), MaximumScoreNotes));
    int tonic = score.tonic;
    std::int64_t sourceDuration = 0;
    for (std::size_t i = 0; i < score.notes.size() && i < MaximumScoreNotes; ++i)
    {
        const auto &note = score.notes[i];
        const auto index = static_cast<int>(i);
        sourceDuration += std::max(note.durationTicks, 0);
        if (note.pageIndex < 0 || note.pageIndex > 9999)
            error(timeline, "messages.domain.page_range", index);
        if (note.keyOverride < -1 || note.keyOverride > 11)
            error(timeline, "messages.domain.key_change_range", index);
        else if (note.keyOverride >= 0)
            tonic = note.keyOverride;
        if (note.degree < 0 || note.degree > 7)
            error(timeline, "messages.domain.degree_range", index);
        if (note.durationTicks <= 0 || note.durationTicks > MaximumNoteDurationTicks)
            error(timeline, "messages.domain.duration_range", index);
        if (note.accidental < -2 || note.accidental > 2)
            error(timeline, "messages.domain.accidental_range", index);
        if (!std::isfinite(note.confidence) || note.confidence < 0.0 || note.confidence > 1.0)
            error(timeline, "messages.domain.confidence_range", index);
        if (!validSourceRect(note.source))
            error(timeline, "messages.domain.rectangle_range", index);
        if (lyricVerses(note).size() > 16)
            error(timeline, "messages.domain.verse_limit", index);
        const int pitch = midiPitch(note, tonic, transpose);
        if (note.degree != 0 && pitch < 0)
            error(timeline, "messages.domain.pitch_range", index);
        pitches.push_back(pitch);
    }
    validateWrittenMeasures(score, timeline, sourceDuration);

    auto repeats = score.repeats;
    std::sort(repeats.begin(), repeats.end(), [](const RepeatSection &left, const RepeatSection &right)
              { return left.firstNote < right.firstNote; });
    std::size_t previousEnd = 0;
    std::size_t expandedSize = score.notes.size();
    for (const auto &repeat : repeats)
    {
        if (repeat.firstNote >= repeat.endNote || repeat.endNote > score.notes.size())
        {
            error(timeline, "messages.domain.repeat_interval");
            continue;
        }
        if (repeat.firstNote < previousEnd)
            error(timeline, "messages.domain.repeat_overlap");
        previousEnd = std::max(previousEnd, repeat.endNote);
        if (repeat.count < 2 || repeat.count > 16)
        {
            error(timeline, "messages.domain.repeat_count");
            continue;
        }
        if (repeat.firstEndingNote < -1 ||
            (repeat.firstEndingNote >= 0 &&
             (static_cast<std::size_t>(repeat.firstEndingNote) <= repeat.firstNote ||
              static_cast<std::size_t>(repeat.firstEndingNote) >= repeat.endNote)))
        {
            error(timeline, "messages.domain.first_ending");
            continue;
        }
        const auto laterPassEnd =
            repeat.firstEndingNote >= 0 ? static_cast<std::size_t>(repeat.firstEndingNote) : repeat.endNote;
        expandedSize += (laterPassEnd - repeat.firstNote) * static_cast<std::size_t>(repeat.count - 1);
        if (expandedSize > MaximumEvents)
            error(timeline, "messages.domain.expanded_limit");
    }
    if (!timeline.valid())
        return timeline;

    const auto velocities = sourceVelocities(score);
    struct SourceOccurrence
    {
        std::size_t sourceNoteIndex;
        int verseIndex;
    };
    std::vector<SourceOccurrence> sourceOrder;
    sourceOrder.reserve(expandedSize);
    auto appendRange = [&sourceOrder](std::size_t first, std::size_t end, int verseIndex)
    {
        for (auto i = first; i < end; ++i)
            sourceOrder.push_back({i, verseIndex});
    };
    std::size_t nextNote = 0;
    int currentVerse = 0;
    for (const auto &repeat : repeats)
    {
        appendRange(nextNote, repeat.firstNote, currentVerse);
        for (int pass = 0; pass < repeat.count; ++pass)
        {
            const auto end = pass > 0 && repeat.firstEndingNote >= 0
                                 ? static_cast<std::size_t>(repeat.firstEndingNote)
                                 : repeat.endNote;
            appendRange(repeat.firstNote, end, pass);
        }
        nextNote = repeat.endNote;
        currentVerse = repeat.count - 1;
    }
    appendRange(nextNote, score.notes.size(), currentVerse);
    timeline.events.reserve(sourceOrder.size());
    for (const auto &occurrence : sourceOrder)
    {
        const auto index = occurrence.sourceNoteIndex;
        const auto &note = score.notes[index];
        bool attack = pitches[index] >= 0;
        int program = programForVerse(score, occurrence.verseIndex);
        int velocity = velocities[index];
        if (!timeline.events.empty())
        {
            const auto &previous = timeline.events.back();
            if (score.notes[previous.sourceNoteIndex].tieToNext)
            {
                if (previous.midiPitch >= 0 && pitches[index] == previous.midiPitch)
                {
                    attack = false;
                    program = previous.program;   // Do not retimbre an already sounding tied note.
                    velocity = previous.velocity; // Nor change its attack strength halfway through the tie.
                }
                else
                    warning(timeline, "messages.domain.invalid_tie", static_cast<int>(previous.sourceNoteIndex));
            }
        }
        timeline.events.push_back({index, timeline.durationTicks, note.durationTicks, pitches[index], attack,
                                   occurrence.verseIndex, program, lyricForVerse(note, occurrence.verseIndex),
                                   velocity});
        timeline.durationTicks += note.durationTicks;
    }
    if (!sourceOrder.empty() && score.notes[sourceOrder.back().sourceNoteIndex].tieToNext)
        warning(timeline, "messages.domain.unfinished_tie", static_cast<int>(sourceOrder.back().sourceNoteIndex));
    appendWrittenMetronomeBeats(score, timeline);
    return timeline;
}

} // namespace singlilt

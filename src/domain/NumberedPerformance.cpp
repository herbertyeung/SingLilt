// Braced numbered-notation systems mapped to the existing polyphonic transport.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "NumberedPerformance.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <stdexcept>

namespace singlilt
{
namespace
{
struct NumberedColumn
{
    std::vector<std::size_t> indices;
    int durationTicks = 0;
};

std::vector<NumberedColumn> columns(const Score &score, int line, double left, double right)
{
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        const auto &note = score.notes[i];
        const double center = note.source.x + note.source.width / 2;
        if (note.line == line && center >= left && center < right)
            indices.push_back(i);
    }
    std::stable_sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b)
                     { return score.notes[a].source.x < score.notes[b].source.x; });
    std::vector<NumberedColumn> result;
    for (const auto index : indices)
    {
        const auto &note = score.notes[index];
        if (!result.empty())
        {
            auto &column = result.back();
            const auto &first = score.notes[column.indices.front()];
            const double distance =
                std::abs(note.source.x + note.source.width / 2 - first.source.x - first.source.width / 2);
            if (distance < std::min(note.source.width, first.source.width) * 0.6 &&
                std::abs(note.source.y - first.source.y) > std::min(note.source.height, first.source.height) * 0.6)
            {
                column.indices.push_back(index);
                // The lowest printed digit carries the chord's underline and extension marks.
                const auto lowest = *std::max_element(
                    column.indices.begin(), column.indices.end(), [&](std::size_t a, std::size_t b)
                    { return score.notes[a].source.y < score.notes[b].source.y; });
                column.durationTicks = score.notes[lowest].durationTicks;
                continue;
            }
        }
        result.push_back({{index}, note.durationTicks});
    }
    return result;
}
} // namespace

StaffPerformance buildNumberedPerformance(Score &score, const std::vector<NumberedSystem> &systems)
{
    Score guide = score;
    guide.notes.clear();
    guide.repeats.clear();
    guide.writtenMeasures.clear();
    StaffPerformance performance;
    performance.sourceTonic = score.tonic;
    performance.staffCount = 2;
    std::vector<std::size_t> guideIndices(score.notes.size() + 1, score.notes.size());
    std::vector<bool> covered(score.notes.size());
    std::vector<bool> requestedTies;
    std::int64_t measureStart = 0;
    int measure = 0;
    int tonic = score.tonic;
    std::map<std::int64_t, int> keyChanges{{0, tonic}};
    std::size_t comparisons = 0;
    for (const auto &system : systems)
    {
        comparisons += score.notes.size() * (system.barlines.size() + 1) * 2;
        if (comparisons > 20000000)
            throw std::invalid_argument("messages.staff.performance_limit");
        if (system.upperLine < 0 || system.lowerLine < -1 ||
            (system.lowerLine >= 0 && system.lowerLine <= system.upperLine) ||
            !std::is_sorted(system.barlines.begin(), system.barlines.end()) ||
            std::adjacent_find(system.barlines.begin(), system.barlines.end()) != system.barlines.end() ||
            std::any_of(
                system.barlines.begin(), system.barlines.end(),
                [](double x) { return !std::isfinite(x) || x <= 0; }))
            throw std::invalid_argument("messages.staff.invalid_performance");
        for (std::size_t bar = 0; bar <= system.barlines.size(); ++bar)
        {
            const double left = bar == 0 ? 0 : system.barlines[bar - 1];
            const double right = bar == system.barlines.size() ? 1e12 : system.barlines[bar];
            const auto upper = columns(score, system.upperLine, left, right);
            const auto lower = columns(score, system.lowerLine, left, right);
            std::int64_t durations[2]{};
            const std::vector<NumberedColumn> *hands[] = {&upper, &lower};
            for (int hand = 0; hand < 2; ++hand)
                for (const auto &column : *hands[hand])
                {
                    if (column.durationTicks <= 0 || column.durationTicks > MaximumNoteDurationTicks)
                        throw std::invalid_argument("messages.domain.duration_range");
                    const int guideIndex = hand == 0 ? int(guide.notes.size()) : -1;
                    if (hand == 0)
                    {
                        auto note = score.notes[column.indices.front()];
                        note.durationTicks = column.durationTicks;
                        note.measure = measure;
                        if (note.keyOverride >= 0)
                        {
                            tonic = note.keyOverride;
                            keyChanges[measureStart + durations[hand]] = tonic;
                        }
                        guide.notes.push_back(std::move(note));
                    }
                    for (const auto index : column.indices)
                    {
                        if (covered[index])
                            throw std::invalid_argument("messages.staff.invalid_performance");
                        covered[index] = true;
                        if (hand == 0)
                            guideIndices[index] = std::size_t(guideIndex);
                        const auto &note = score.notes[index];
                        const auto key = std::prev(keyChanges.upper_bound(measureStart + durations[hand]));
                        const int pitch = midiPitch(note, key->second);
                        if (note.degree == 0)
                            continue;
                        if (pitch < 0)
                            throw std::invalid_argument("messages.domain.pitch_range");
                        StaffPerformanceNote sounding;
                        sounding.startTick = measureStart + durations[hand];
                        sounding.durationTicks = column.durationTicks;
                        sounding.midiPitch = pitch;
                        sounding.staff = hand + 1;
                        sounding.sourceNoteIndex = hand == 0 && index == column.indices.front() ? guideIndex : -1;
                        sounding.source = note.source;
                        sounding.pageIndex = note.pageIndex;
                        sounding.velocity = score.baseVelocity;
                        performance.notes.push_back(std::move(sounding));
                        requestedTies.push_back(note.tieToNext);
                    }
                    durations[hand] += column.durationTicks;
                }
            const auto duration = std::max(durations[0], durations[1]);
            if (duration == 0)
                throw std::invalid_argument("messages.staff.invalid_performance");
            if (durations[0] < duration)
            {
                if (duration - durations[0] > MaximumNoteDurationTicks)
                    throw std::invalid_argument("messages.domain.duration_range");
                Note rest;
                rest.degree = 0;
                rest.durationTicks = int(duration - durations[0]);
                rest.measure = measure;
                rest.line = system.upperLine;
                rest.hasImageAnchor = false;
                guide.notes.push_back(rest);
            }
            guide.writtenMeasures.push_back(
                {measureStart, duration, measure, score.beatsPerBar, score.beatUnit, 0});
            measureStart += duration;
            ++measure;
        }
        // Upper-row edge markers point at the first lower-row source index.
        // Those are system boundaries, not positions inside the lower-hand clock.
        for (std::size_t index = 0; index < score.notes.size(); ++index)
            if (score.notes[index].line == system.lowerLine)
                guideIndices[index] = guide.notes.size();
    }
    if (std::find(covered.begin(), covered.end(), false) != covered.end() || performance.notes.empty())
        throw std::invalid_argument("messages.staff.invalid_performance");
    guideIndices.back() = guide.notes.size();
    for (const auto &repeat : score.repeats)
    {
        if (repeat.firstNote >= repeat.endNote || repeat.endNote > score.notes.size() ||
            repeat.firstEndingNote < -1 || repeat.firstEndingNote >= int(score.notes.size()))
            throw std::invalid_argument("messages.domain.repeat_interval");
        guide.repeats.push_back({guideIndices[repeat.firstNote], guideIndices[repeat.endNote], repeat.count,
                                 repeat.firstEndingNote < 0 ? -1 : int(guideIndices[repeat.firstEndingNote])});
    }
    for (std::size_t i = 0; i < guide.notes.size(); ++i)
        guide.notes[i].id = int(i);
    // Source traversal is already chronological within each hand, including across systems.
    std::map<std::pair<int, int>, std::size_t> previous;
    for (std::size_t i = 0; i < performance.notes.size(); ++i)
    {
        auto &note = performance.notes[i];
        const auto key = std::make_pair(note.staff, note.midiPitch);
        const auto found = previous.find(key);
        if (found != previous.end())
        {
            auto &before = performance.notes[found->second];
            if (requestedTies[found->second] && before.startTick + before.durationTicks == note.startTick)
            {
                before.tieStart = true;
                note.tieStop = true;
            }
        }
        previous[key] = i;
    }
    performance.durationTicks = measureStart;
    performance.timingFingerprint = staffTimingFingerprint(guide);
    const auto timeline = buildTimeline(guide);
    const auto plan = buildStaffPerformancePlan(guide, timeline, performance);
    if (!timeline.valid() || !plan.valid())
        throw std::invalid_argument("messages.staff.invalid_performance");
    score = std::move(guide);
    return performance;
}

NumberedGuideCorrection correctedNumberedGuide(const Score &score, const StaffPerformance &performance,
                                               std::size_t noteIndex, Note replacement)
{
    const auto originalTimeline = buildTimeline(score);
    if (noteIndex >= score.notes.size() || score.writtenMeasures.empty() ||
        !buildStaffPerformancePlan(score, originalTimeline, performance).valid())
        throw std::invalid_argument("messages.staff.invalid_performance");
    if (replacement.durationTicks <= 0 || replacement.durationTicks > MaximumNoteDurationTicks)
        throw std::invalid_argument("messages.domain.duration_range");
    NumberedGuideCorrection corrected{score, performance, 0};
    auto &guide = corrected.score;
    auto &parts = corrected.performance;
    guide.notes.clear();
    guide.writtenMeasures.clear();
    std::vector<std::int64_t> oldStarts{0};
    for (const auto &note : score.notes)
        oldStarts.push_back(oldStarts.back() + note.durationTicks);
    std::vector<std::size_t> guideIndices(score.notes.size() + 1);
    std::size_t nextNote = 0;
    std::int64_t measureStart = 0;
    std::size_t comparisons = 0;
    for (const auto &written : score.writtenMeasures)
    {
        const auto oldEnd = written.startTick + written.durationTicks;
        const auto first = guide.notes.size();
        std::int64_t upperDuration = 0, otherDuration = 0;
        std::optional<std::size_t> oldPadding;
        while (nextNote < score.notes.size() && oldStarts[nextNote] < oldEnd)
        {
            auto note = nextNote == noteIndex ? replacement : score.notes[nextNote];
            guideIndices[nextNote] = guide.notes.size();
            const bool padding = nextNote != noteIndex && nextNote + 1 < oldStarts.size() &&
                                 oldStarts[nextNote + 1] == oldEnd && note.degree == 0 && !note.hasImageAnchor &&
                                 note.lyric.empty() && note.verseLyrics.empty() && !note.tieToNext &&
                                 note.keyOverride < 0;
            if (padding)
                oldPadding = nextNote;
            else
            {
                note.measure = written.number;
                note.pageIndex = written.pageIndex;
                guide.notes.push_back(std::move(note));
                upperDuration += guide.notes.back().durationTicks;
            }
            ++nextNote;
        }
        for (const auto &note : performance.notes)
        {
            if (++comparisons > 20000000)
                throw std::invalid_argument("messages.staff.performance_limit");
            if (note.staff != performance.primaryStaff && note.startTick < oldEnd &&
                note.startTick + note.durationTicks > written.startTick)
                otherDuration = std::max(otherDuration, std::min(oldEnd, note.startTick + note.durationTicks) -
                                                            written.startTick);
        }
        auto duration = std::max(upperDuration, otherDuration);
        if (duration == 0)
            duration = written.durationTicks;
        if (duration > upperDuration)
        {
            if (duration - upperDuration > MaximumNoteDurationTicks)
                throw std::invalid_argument("messages.domain.duration_range");
            Note rest;
            rest.degree = 0;
            rest.durationTicks = int(duration - upperDuration);
            rest.measure = written.number;
            rest.pageIndex = written.pageIndex;
            rest.line = first < guide.notes.size() ? guide.notes[first].line : score.notes[nextNote - 1].line;
            rest.hasImageAnchor = false;
            guide.notes.push_back(rest);
        }
        if (oldPadding)
            guideIndices[*oldPadding] = duration > upperDuration ? guide.notes.size() - 1 : guide.notes.size();
        auto measure = written;
        measure.startTick = measureStart;
        measure.durationTicks = duration;
        guide.writtenMeasures.push_back(measure);
        measureStart += duration;
    }
    guideIndices.back() = guide.notes.size();
    for (auto &repeat : guide.repeats)
    {
        repeat.firstNote = guideIndices[repeat.firstNote];
        repeat.endNote = guideIndices[repeat.endNote];
        if (repeat.firstEndingNote >= 0)
            repeat.firstEndingNote = int(guideIndices[std::size_t(repeat.firstEndingNote)]);
    }
    std::vector<std::int64_t> newStarts{0};
    std::vector<int> oldKeys, newKeys;
    int oldKey = score.tonic, newKey = guide.tonic;
    for (const auto &note : score.notes)
    {
        if (note.keyOverride >= 0)
            oldKey = note.keyOverride;
        oldKeys.push_back(oldKey);
    }
    for (std::size_t index = 0; index < guide.notes.size(); ++index)
    {
        auto &note = guide.notes[index];
        note.id = int(index);
        newStarts.push_back(newStarts.back() + note.durationTicks);
        if (note.keyOverride >= 0)
            newKey = note.keyOverride;
        newKeys.push_back(newKey);
    }
    const auto guideAt = [](const std::vector<std::int64_t> &starts, std::int64_t tick)
    { return std::size_t(std::upper_bound(starts.begin(), starts.end(), tick) - starts.begin() - 1); };
    const auto measureTick = [&](std::int64_t tick)
    {
        if (tick == oldStarts.back())
            return newStarts.back();
        const auto after = std::upper_bound(score.writtenMeasures.begin(), score.writtenMeasures.end(), tick,
                                            [](std::int64_t position, const WrittenMeasure &measure)
                                            { return position < measure.startTick; });
        const auto index = std::size_t(after - score.writtenMeasures.begin() - 1);
        return guide.writtenMeasures[index].startTick + tick - score.writtenMeasures[index].startTick;
    };
    parts.notes.clear();
    std::vector<int> linked(guide.notes.size(), -1);
    for (auto note : performance.notes)
    {
        if (note.sourceNoteIndex >= int(score.notes.size()))
            throw std::invalid_argument("messages.staff.invalid_performance");
        const auto oldGuide = guideAt(oldStarts, note.startTick);
        const auto newGuide = guideIndices[oldGuide];
        if (note.staff == performance.primaryStaff)
        {
            if (newGuide >= guide.notes.size())
                throw std::invalid_argument("messages.staff.invalid_performance");
            note.startTick = newStarts[newGuide] + note.startTick - oldStarts[oldGuide];
            if (note.durationTicks == score.notes[oldGuide].durationTicks)
                note.durationTicks = guide.notes[newGuide].durationTicks;
        }
        else
        {
            const auto end = note.tieStart ? measureTick(note.startTick + note.durationTicks) : 0;
            note.startTick = measureTick(note.startTick);
            if (note.tieStart)
                note.durationTicks = end - note.startTick;
        }
        const auto pitchGuide = guideAt(newStarts, note.startTick);
        if (pitchGuide >= newKeys.size())
            throw std::invalid_argument("messages.staff.invalid_performance");
        note.midiPitch += score.tonic - performance.sourceTonic + newKeys[pitchGuide] - oldKeys[oldGuide];
        if (note.sourceNoteIndex >= 0)
        {
            const auto index = guideIndices[std::size_t(note.sourceNoteIndex)];
            const auto &source = guide.notes[index];
            if (source.degree == 0)
                continue;
            note.sourceNoteIndex = int(index);
            note.startTick = newStarts[index];
            note.durationTicks = source.durationTicks;
            note.midiPitch = midiPitch(source, newKeys[index]);
            note.tieStart = false;
            note.tieStop = false;
            linked[index] = int(parts.notes.size());
        }
        parts.notes.push_back(std::move(note));
    }
    const auto selected = guideIndices[noteIndex];
    const auto &edited = guide.notes[selected];
    if (edited.degree != 0 && linked[selected] < 0)
    {
        StaffPerformanceNote note;
        note.startTick = newStarts[selected];
        note.durationTicks = edited.durationTicks;
        note.midiPitch = midiPitch(edited, newKeys[selected]);
        note.staff = performance.primaryStaff;
        note.sourceNoteIndex = int(selected);
        note.velocity = guide.baseVelocity;
        note.source = edited.source;
        note.pageIndex = edited.pageIndex;
        note.hasImageAnchor = edited.hasImageAnchor;
        linked[selected] = int(parts.notes.size());
        parts.notes.push_back(note);
    }
    for (std::size_t index = 0; index + 1 < guide.notes.size(); ++index)
        if (guide.notes[index].tieToNext && linked[index] >= 0 && linked[index + 1] >= 0)
        {
            auto &before = parts.notes[std::size_t(linked[index])];
            auto &after = parts.notes[std::size_t(linked[index + 1])];
            if (before.midiPitch == after.midiPitch && before.staff == after.staff && before.voice == after.voice)
            {
                before.tieStart = true;
                after.tieStop = true;
            }
        }
    parts.durationTicks = newStarts.back();
    parts.sourceTonic = guide.tonic;
    parts.timingFingerprint = staffTimingFingerprint(guide);
    corrected.selectedNote = selected;
    if (!buildStaffPerformancePlan(guide, buildTimeline(guide), parts).valid())
        throw std::invalid_argument("messages.staff.invalid_performance");
    return corrected;
}
} // namespace singlilt

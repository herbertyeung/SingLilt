// Uniform-meter melody structure and bounded note entry.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "NumberedEditing.h"
#include <algorithm>
#include <numeric>

namespace singlilt
{
std::int64_t numberedDuration(const Score &score)
{
    return std::accumulate(score.notes.begin(), score.notes.end(), std::int64_t{0},
                           [](std::int64_t ticks, const Note &note) { return ticks + note.durationTicks; });
}

int numberedBarCount(const Score &score)
{
    const int ticks = ticksPerBar(score);
    if (ticks <= 0)
        return 0;
    return std::max({1, score.editorMinimumBars, int((numberedDuration(score) + ticks - 1) / ticks)});
}

void clearNumberedRestTextAndTies(Score &score, std::size_t index)
{
    auto &rest = score.notes[index];
    if (rest.degree != 0)
        return;
    rest.lyric.clear();
    rest.verseLyrics.clear();
    rest.tieToNext = false;
    if (index > 0)
        score.notes[index - 1].tieToNext = false;
}

bool canRestructureNumberedScore(const Score &score)
{
    return ticksPerBar(score) > 0 && score.repeats.empty() && score.writtenMeasures.empty() &&
           score.keyChanges.empty() &&
           std::none_of(score.notes.begin(), score.notes.end(),
                        [](const Note &note) { return note.keyOverride >= 0; });
}

bool appendNumberedNote(Score &score, std::int64_t tick, Note note, std::int64_t limit)
{
    const auto end = numberedDuration(score);
    const int barTicks = ticksPerBar(score);
    if (!canRestructureNumberedScore(score) || tick < end || note.durationTicks <= 0 ||
        note.durationTicks > MaximumNoteDurationTicks ||
        tick + note.durationTicks > (limit ? limit : std::int64_t(MaximumEditorBars) * barTicks))
        return false;
    const auto gap = tick - end;
    const auto rests = (gap + barTicks - 1) / barTicks;
    if (score.notes.size() + rests + 1 > MaximumScoreNotes)
        return false;
    for (auto remaining = gap; remaining > 0;)
    {
        Note rest;
        rest.degree = 0;
        rest.durationTicks = int(std::min<std::int64_t>(remaining, barTicks));
        rest.hasImageAnchor = false;
        score.notes.push_back(rest);
        remaining -= rest.durationTicks;
    }
    note.hasImageAnchor = false;
    score.notes.push_back(std::move(note));
    return true;
}

bool insertNumberedBar(Score &score, int bar)
{
    const int count = numberedBarCount(score);
    if (!canRestructureNumberedScore(score) || bar < 0 || bar > count || count >= MaximumEditorBars)
        return false;
    const int length = ticksPerBar(score);
    if (!insertNumberedTime(score, std::int64_t(bar) * length, length))
        return false;
    score.editorMinimumBars = count + 1;
    return true;
}

bool insertNumberedTime(Score &score, std::int64_t boundary, int length)
{
    std::int64_t tick = 0;
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        const auto end = tick + score.notes[i].durationTicks;
        if (boundary < end)
        {
            const bool split = boundary > tick;
            if (score.notes.size() + (split ? 2 : 1) > MaximumScoreNotes)
                return false;
            if (split)
            {
                auto tail = score.notes[i];
                tail.durationTicks = int(end - boundary);
                tail.lyric.clear();
                tail.verseLyrics.clear();
                tail.keyOverride = -1;
                tail.dynamicLevel = 0;
                tail.accent = false;
                tail.staccato = false;
                score.notes[i].durationTicks = int(boundary - tick);
                score.notes[i].tieToNext = false;
                ++i;
                score.notes.insert(score.notes.begin() + i, tail);
            }
            Note rest;
            rest.degree = 0;
            rest.durationTicks = length;
            rest.hasImageAnchor = false;
            if (i > 0)
                score.notes[i - 1].tieToNext = false;
            score.notes.insert(score.notes.begin() + i, rest);
            break;
        }
        tick = end;
    }
    return true;
}

bool replaceNumberedBarWithRest(Score &score, int bar)
{
    if (!canRestructureNumberedScore(score) || bar < 0 || bar >= numberedBarCount(score))
        return false;
    const int barTicks = ticksPerBar(score);
    return replaceNumberedTimeWithRest(score, std::int64_t(bar) * barTicks, barTicks);
}

bool replaceNumberedTimeWithRest(Score &score, std::int64_t first, int barTicks)
{
    const auto last = first + barTicks;
    Note rest;
    rest.degree = 0;
    rest.durationTicks = barTicks;
    rest.hasImageAnchor = false;
    std::int64_t tick = 0;
    for (const auto &note : score.notes)
    {
        if (tick == first && note.degree == 0 && note.durationTicks == barTicks)
            return true;
        // Keep the inherited dynamic after the replaced, now-silent interval.
        if (tick >= first && tick < last && note.dynamicLevel)
            rest.dynamicLevel = note.dynamicLevel;
        tick += note.durationTicks;
    }
    if (tick <= first)
    {
        auto candidate = score;
        if (!candidate.notes.empty())
            candidate.notes.back().tieToNext = false;
        if (!appendNumberedNote(candidate, first, rest, first + barTicks))
            return false;
        score = std::move(candidate);
        return true;
    }
    std::vector<Note> notes;
    bool inserted = false;
    tick = 0;
    for (const auto &note : score.notes)
    {
        const auto end = tick + note.durationTicks;
        if (end <= first)
        {
            auto prefix = note;
            if (end == first)
                prefix.tieToNext = false;
            notes.push_back(std::move(prefix));
        }
        else
        {
            if (tick < first)
            {
                auto prefix = note;
                prefix.durationTicks = int(first - tick);
                prefix.tieToNext = false;
                notes.push_back(std::move(prefix));
            }
            if (!inserted)
            {
                notes.push_back(rest);
                inserted = true;
            }
            if (tick >= last)
                notes.push_back(note);
            else if (end > last)
            {
                auto suffix = note;
                suffix.durationTicks = int(end - last);
                suffix.lyric.clear();
                suffix.verseLyrics.clear();
                suffix.dynamicLevel = 0;
                suffix.accent = suffix.staccato = false;
                notes.push_back(std::move(suffix));
            }
        }
        tick = end;
    }
    if (notes.size() > MaximumScoreNotes)
        return false;
    score.notes = std::move(notes);
    return true;
}

bool removeNumberedBar(Score &score, int bar)
{
    const int count = numberedBarCount(score);
    if (!canRestructureNumberedScore(score) || bar < 0 || bar >= count)
        return false;
    const int length = ticksPerBar(score);
    removeNumberedTime(score, std::int64_t(bar) * length, length);
    score.editorMinimumBars = std::max(1, count - 1);
    return true;
}

void removeNumberedTime(Score &score, std::int64_t first, int length)
{
    const auto last = first + length;
    std::vector<Note> retained;
    std::vector<std::size_t> originalIndices;
    std::size_t index = 0;
    std::int64_t tick = 0;
    for (auto note : score.notes)
    {
        const auto end = tick + note.durationTicks;
        const auto overlap = std::max<std::int64_t>(0, std::min(end, last) - std::max(tick, first));
        note.durationTicks -= int(overlap);
        if (tick >= first && tick < last && end > last)
        {
            note.lyric.clear();
            note.verseLyrics.clear();
        }
        if (note.durationTicks > 0)
        {
            retained.push_back(std::move(note));
            originalIndices.push_back(index);
        }
        ++index;
        tick = end;
    }
    for (std::size_t i = 0; i < retained.size(); ++i)
        if (i + 1 == retained.size() || originalIndices[i + 1] != originalIndices[i] + 1)
            retained[i].tieToNext = false;
    score.notes = std::move(retained);
}
} // namespace singlilt

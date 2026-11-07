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

bool canRestructureNumberedScore(const Score &score)
{
    return ticksPerBar(score) > 0 && score.repeats.empty() && score.writtenMeasures.empty() &&
           score.keyChanges.empty() &&
           std::none_of(score.notes.begin(), score.notes.end(),
                        [](const Note &note) { return note.keyOverride >= 0; });
}

bool appendNumberedNote(Score &score, std::int64_t tick, Note note)
{
    const auto end = numberedDuration(score);
    const int barTicks = ticksPerBar(score);
    if (!canRestructureNumberedScore(score) || tick < end || note.durationTicks <= 0 ||
        note.durationTicks > MaximumNoteDurationTicks ||
        tick + note.durationTicks > std::int64_t(MaximumEditorBars) * barTicks)
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
    if (!canRestructureNumberedScore(score) || bar < 0 || bar > count || count >= MaximumEditorBars ||
        score.notes.size() + 2 > MaximumScoreNotes)
        return false;
    const int barTicks = ticksPerBar(score);
    const auto boundary = std::int64_t(bar) * barTicks;
    std::int64_t tick = 0;
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        const auto end = tick + score.notes[i].durationTicks;
        if (boundary < end)
        {
            if (boundary > tick)
            {
                auto tail = score.notes[i];
                tail.durationTicks = int(end - boundary);
                tail.lyric.clear();
                tail.verseLyrics.clear();
                tail.keyOverride = -1;
                score.notes[i].durationTicks = int(boundary - tick);
                score.notes[i].tieToNext = false;
                ++i;
                score.notes.insert(score.notes.begin() + i, tail);
            }
            Note rest;
            rest.degree = 0;
            rest.durationTicks = barTicks;
            rest.hasImageAnchor = false;
            if (i > 0)
                score.notes[i - 1].tieToNext = false;
            score.notes.insert(score.notes.begin() + i, rest);
            break;
        }
        tick = end;
    }
    score.editorMinimumBars = count + 1;
    return true;
}

bool removeNumberedBar(Score &score, int bar)
{
    const int count = numberedBarCount(score);
    if (!canRestructureNumberedScore(score) || bar < 0 || bar >= count)
        return false;
    const auto first = std::int64_t(bar) * ticksPerBar(score);
    const auto last = first + ticksPerBar(score);
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
    score.editorMinimumBars = std::max(1, count - 1);
    return true;
}
} // namespace singlilt

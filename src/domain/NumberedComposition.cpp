// Authoritative authored voices and shared-instrument performance compilation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#include "NumberedComposition.h"
#include "NumberedEditing.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace singlilt
{
const NumberedVoice &numberedVoice(const NumberedComposition &composition, int id)
{
    for (const auto &voice : composition.voices)
        if (voice.id == id)
            return voice;
    throw std::invalid_argument("ui.editor.invalid_composition");
}
NumberedVoice &numberedVoice(NumberedComposition &composition, int id)
{
    for (auto &voice : composition.voices)
        if (voice.id == id)
            return voice;
    throw std::invalid_argument("ui.editor.invalid_composition");
}
std::int64_t compositionDuration(const NumberedComposition &composition)
{
    std::int64_t longest = 0;
    for (const auto &voice : composition.voices)
    {
        std::int64_t duration = 0;
        for (const auto &note : voice.notes)
            duration += note.durationTicks;
        longest = std::max(longest, duration);
    }
    return longest;
}
int compositionBars(const Score &metadata, const NumberedComposition &composition)
{
    return int(numberedMeasures(metadata, composition).size());
}
void validateNumberedComposition(const Score &metadata, const NumberedComposition &composition, int transpose)
{
    if (composition.voices.empty() || composition.voices.size() > MaximumNumberedVoices ||
        !canRestructureNumberedScore(metadata) || compositionBars(metadata, composition) > MaximumEditorBars)
        throw std::invalid_argument("ui.editor.invalid_composition");
    std::vector<NumberedMeasureSpan> measures;
    if (!composition.structure.measures.empty() || !composition.structure.repeats.empty())
        measures = performedScoreMap(metadata, composition).written;
    std::set<int> ids;
    std::size_t count = 0;
    for (const auto &voice : composition.voices)
    {
        count += voice.notes.size();
        if (voice.id <= 0 || voice.id >= composition.nextId || !ids.insert(voice.id).second ||
            voice.name.empty() || voice.name.size() > 256 || count > MaximumScoreNotes)
            throw std::invalid_argument("ui.editor.invalid_composition");
        auto score = metadata;
        score.notes = voice.notes;
        if (score.notes.empty())
            score.notes.push_back(Note{});
        if (!canRestructureNumberedScore(score))
            throw std::invalid_argument("ui.editor.invalid_composition");
        const auto validation = measures.empty()
                                    ? buildTimeline(score, transpose)
                                    : buildNumberedWrittenTimeline(std::move(score), measures, transpose);
        if (!validation.valid())
            throw std::invalid_argument("ui.editor.invalid_composition");
        for (const auto &note : voice.notes)
            if (note.dynamicLevel < 0 || note.dynamicLevel > 6 || note.keyOverride != -1)
                throw std::invalid_argument("ui.editor.invalid_composition");
    }
}
Score compositionPlaybackScore(const Score &metadata, const NumberedComposition &composition)
{
    validateNumberedComposition(metadata, composition);
    auto score = metadata;
    // The UI supplies the active voice as a guide. Only this temporary playback copy is padded.
    auto remaining = performedScoreMap(metadata, composition).writtenEnd - numberedDuration(score);
    while (remaining > 0)
    {
        Note rest;
        rest.degree = 0;
        rest.durationTicks = int(std::min<std::int64_t>(remaining, ticksPerBar(score)));
        score.notes.push_back(rest);
        remaining -= rest.durationTicks;
    }
    score.versePrograms = {programForVerse(metadata, 0)};
    return score;
}
AccompanimentPlan compositionPlaybackPlan(const Score &metadata, const NumberedComposition &composition)
{
    validateNumberedComposition(metadata, composition);
    AccompanimentPlan plan;
    const auto map = performedScoreMap(metadata, composition);
    const auto pitchAt = [&](const Note &note, std::int64_t tick)
    { return midiPitch(note, map.written[numberedMeasureAt(map.written, tick)].tonic); };
    plan.durationTicks = map.writtenEnd;
    constexpr int levels[] = {0, 32, 48, 64, 80, 96, 112};
    for (const auto &voice : composition.voices)
    {
        std::int64_t tick = 0;
        int velocity = metadata.baseVelocity;
        for (std::size_t i = 0; i < voice.notes.size(); ++i)
        {
            const auto &note = voice.notes[i];
            if (note.dynamicLevel)
                velocity = levels[note.dynamicLevel];
            const int attack = std::min(127, velocity + (note.accent ? 16 : 0));
            const int pitch = pitchAt(note, tick);
            if (note.degree != 0 && (pitch < 0 || pitch > 127))
                throw std::invalid_argument("messages.domain.pitch_range");
            std::int64_t written = note.durationTicks;
            std::int64_t gate = written;
            const Note *last = &note;
            while (pitch >= 0 && last->tieToNext && i + 1 < voice.notes.size() &&
                   pitchAt(voice.notes[i + 1], tick + written) == pitch)
            {
                last = &voice.notes[++i];
                written += last->durationTicks;
                if (last->dynamicLevel)
                    velocity = levels[last->dynamicLevel];
            }
            gate = written - (last->staccato ? last->durationTicks - std::max(1, last->durationTicks / 2) : 0);
            if (pitch >= 0)
                plan.events.push_back({tick, gate, pitch, attack, AccompanimentRole::Chord});
            tick += written;
        }
    }
    std::stable_sort(plan.events.begin(), plan.events.end(),
                     [](const auto &left, const auto &right) { return left.startTick < right.startTick; });
    if (composition.structure.repeats.empty())
        return plan;
    AccompanimentPlan expanded;
    expanded.durationTicks = map.duration;
    for (std::size_t first = 0; first < map.order.size();)
    {
        std::size_t next = first + 1;
        while (next < map.order.size() && map.order[next].measure == map.order[next - 1].measure + 1)
            ++next;
        const auto from = map.written[map.order[first].measure].start;
        const auto to = std::min(map.writtenEnd, map.written[map.order[next - 1].measure].end);
        for (const auto &event : plan.events)
        {
            if (event.startTick >= to)
                break;
            const auto start = std::max(from, event.startTick);
            const auto end = std::min(to, event.startTick + event.durationTicks);
            if (end > start)
            {
                if (expanded.events.size() >= 400000)
                    throw std::invalid_argument("ui.editor.invalid_structure");
                auto clipped = event;
                clipped.startTick = map.order[first].start + start - from;
                clipped.durationTicks = end - start;
                expanded.events.push_back(clipped);
            }
        }
        first = next;
    }
    std::stable_sort(expanded.events.begin(), expanded.events.end(),
                     [](const auto &a, const auto &b) { return a.startTick < b.startTick; });
    return expanded;
}
} // namespace singlilt

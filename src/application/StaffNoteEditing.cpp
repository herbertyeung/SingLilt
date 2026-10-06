// Add, remove, and update complete staff-note events.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffNoteEditing.h"

#include "StaffScoreCorrection.h"
#include "i18n/LanguageManager.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <map>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
void requireNoteEdit(bool valid, const char *key)
{
    if (!valid)
        throw std::runtime_error(trText(key).toStdString());
}

void validateOriginal(const Project &original)
{
    requireNoteEdit(original.staffImagePlayback && original.processing.value("local").toBool() &&
                        original.staffPerformance && !original.score.writtenMeasures.empty(),
                    "ui.staff_correction.local_only");
    projectToJson(original);
}

void markAffectedTieChain(std::vector<StaffPerformanceNote> &notes, std::size_t removedIndex)
{
    const auto &removed = notes[removedIndex];
    if (!removed.tieStart && !removed.tieStop)
        return;

    std::map<std::int64_t, std::vector<std::size_t>> incoming;
    std::map<std::int64_t, std::vector<std::size_t>> outgoing;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const auto &note = notes[index];
        if (note.staff != removed.staff || note.voice != removed.voice || note.midiPitch != removed.midiPitch)
            continue;
        if (note.tieStart)
            incoming[note.startTick + note.durationTicks].push_back(index);
        if (note.tieStop)
            outgoing[note.startTick].push_back(index);
    }

    std::vector<bool> affected(notes.size(), false);
    std::deque<std::size_t> pending{removedIndex};
    affected[removedIndex] = true;
    const auto enqueue = [&](const auto &byTick, std::int64_t tick)
    {
        const auto candidates = byTick.find(tick);
        if (candidates == byTick.end())
            return;
        for (const auto index : candidates->second)
            if (!affected[index])
            {
                affected[index] = true;
                pending.push_back(index);
            }
    };
    while (!pending.empty())
    {
        const auto index = pending.front();
        pending.pop_front();
        const auto &note = notes[index];
        if (note.tieStop)
            enqueue(incoming, note.startTick);
        if (note.tieStart)
            enqueue(outgoing, note.startTick + note.durationTicks);
    }
    // A removed tie segment leaves its connected chain for explicit review, not guessed repair.
    for (std::size_t index = 0; index < notes.size(); ++index)
        if (affected[index] && index != removedIndex)
            notes[index].unresolvedSoundTie = true;
}
} // namespace

Project addedStaffNote(const Project &original, StaffPerformanceNote note)
{
    validateOriginal(original);
    requireNoteEdit(note.startTick >= 0 && note.startTick < original.staffPerformance->durationTicks &&
                        note.durationTicks > 0,
                    "ui.staff_correction.invalid_note");
    const auto &measures = original.score.writtenMeasures;
    const auto after = std::upper_bound(measures.begin(), measures.end(), note.startTick,
                                        [](std::int64_t tick, const WrittenMeasure &measure)
                                        { return tick < measure.startTick; });
    requireNoteEdit(after != measures.begin(), "ui.staff_correction.invalid_position");
    const auto &measure = *std::prev(after);
    requireNoteEdit(note.startTick < measure.startTick + measure.durationTicks &&
                        note.durationTicks <= measure.startTick + measure.durationTicks - note.startTick,
                    "ui.staff_correction.bar_bound");
    requireNoteEdit(note.pageIndex == measure.pageIndex && note.pageIndex >= 0 &&
                        std::size_t(note.pageIndex) < original.staffPages.size(),
                    "messages.pages.invalid_project");
    const auto &source = note.source;
    if (note.hasImageAnchor)
    {
        const auto &image = original.staffPages[std::size_t(note.pageIndex)].sourceImage;
        requireNoteEdit(std::isfinite(source.x) && std::isfinite(source.y) && std::isfinite(source.width) &&
                            std::isfinite(source.height) && source.x >= 0 && source.y >= 0 && source.width > 0 &&
                            source.height > 0 && source.x + source.width <= image.width() &&
                            source.y + source.height <= image.height(),
                        "messages.storage.invalid_rectangle");
    }
    else
        requireNoteEdit(source.x == 0 && source.y == 0 && source.width == 0 && source.height == 0,
                        "messages.storage.invalid_rectangle");

    note.sourceNoteIndex = -1;
    note.tieStart = false;
    note.tieStop = false;
    note.unresolvedSoundTie = false;
    auto notes = original.staffPerformance->notes;
    notes.push_back(std::move(note));
    return correctedStaffProject(original, std::move(notes), original.score.writtenMeasures);
}

Project deletedStaffNote(const Project &original, int performanceIndex)
{
    validateOriginal(original);
    requireNoteEdit(performanceIndex >= 0 &&
                        std::size_t(performanceIndex) < original.staffPerformance->notes.size(),
                    "ui.staff_correction.invalid_note");
    auto notes = original.staffPerformance->notes;
    const auto index = std::size_t(performanceIndex);
    markAffectedTieChain(notes, index);
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(index));
    return correctedStaffProject(original, std::move(notes), original.score.writtenMeasures);
}

Project updatedStaffNote(const Project &original, int performanceIndex, StaffPerformanceNote note)
{
    validateOriginal(original);
    requireNoteEdit(performanceIndex >= 0 &&
                        std::size_t(performanceIndex) < original.staffPerformance->notes.size(),
                    "ui.staff_correction.invalid_note");
    auto notes = original.staffPerformance->notes;
    const auto index = std::size_t(performanceIndex);
    const auto &before = notes[index];
    const bool changedSound = before.midiPitch != note.midiPitch || before.startTick != note.startTick ||
                              before.durationTicks != note.durationTicks || before.staff != note.staff ||
                              before.voice != note.voice;
    if (changedSound)
    {
        markAffectedTieChain(notes, index);
        if (note.tieStart || note.tieStop)
            note.unresolvedSoundTie = true;
    }
    note.sourceNoteIndex = -1;
    notes[index] = std::move(note);
    return correctedStaffProject(original, std::move(notes), original.score.writtenMeasures);
}
} // namespace singlilt

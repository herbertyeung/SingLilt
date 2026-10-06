// Complete staff-note edit and undo/redo regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffNoteEditingCheck.h"

#include "application/StaffNoteEditing.h"
#include "application/StaffScoreCorrection.h"
#include "domain/Timeline.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace singlilt
{
namespace
{
Project fixtureProject()
{
    Project project;
    project.staffImagePlayback = true;
    project.notationStyle = NotationStyle::Staff;
    project.processing = {{"local", true}, {"recognitionAccuracyMeasured", false}};
    project.score.title = "Original-image note editing fixture";
    project.score.writtenMeasures = {{0, 1920, 0, 4, 4, 0}, {1920, 1920, 1, 4, 4, 1}};
    project.staffPerformance.emplace();
    auto &performance = *project.staffPerformance;
    performance.durationTicks = 3840;
    performance.primaryProgram = 40;
    performance.otherProgram = 35;
    performance.clefChanges = {{0, 1, false}, {0, 2, true}};
    for (int page = 0; page < 2; ++page)
    {
        QImage image(200, 140, QImage::Format_RGB32);
        image.fill(page == 0 ? 0xfffff8e8 : 0xffe8f0ff);
        project.staffPages.push_back(
            {QString("Original page %1").arg(page + 1), image, image, page * 1920, (page + 1) * 1920});
        Note guide;
        guide.id = page;
        guide.degree = page == 0 ? 1 : 2;
        guide.durationTicks = 1920;
        guide.measure = page;
        guide.pageIndex = page;
        guide.source = {20, 20, 12, 8};
        guide.lyric = page == 0 ? "First" : "Second";
        project.score.notes.push_back(guide);
        for (int staff = 1; staff <= 2; ++staff)
        {
            StaffPerformanceNote note;
            note.startTick = page * 1920;
            note.durationTicks = 1920;
            note.midiPitch = (staff == 1 ? 60 : 48) + page * 2;
            note.staff = staff;
            note.voice = staff == 1 ? "right" : "left";
            note.pageIndex = page;
            note.sourceNoteIndex = staff == 1 ? page : -1;
            note.source = {20, staff == 1 ? 20.0 : 90.0, 12, 8};
            performance.notes.push_back(note);
        }
    }
    project.image = project.staffPages.front().sourceImage;
    performance.timingFingerprint = staffTimingFingerprint(project.score);
    projectToJson(project);
    return project;
}

bool samePixels(const Project &left, const Project &right)
{
    if (left.image != right.image || left.staffPages.size() != right.staffPages.size())
        return false;
    for (std::size_t page = 0; page < left.staffPages.size(); ++page)
        if (left.staffPages[page].sourceImage != right.staffPages[page].sourceImage ||
            left.staffPages[page].renderedImage != right.staffPages[page].renderedImage)
            return false;
    return true;
}

AccompanimentPlan soundPlan(const Project &project)
{
    return buildStaffPerformancePlan(project.score, buildTimeline(project.score), *project.staffPerformance);
}

int noteIndex(const Project &project, int pitch, std::int64_t tick, int staff = 1)
{
    const auto &notes = project.staffPerformance->notes;
    const auto found =
        std::find_if(notes.begin(), notes.end(), [&](const StaffPerformanceNote &note)
                     { return note.midiPitch == pitch && note.startTick == tick && note.staff == staff; });
    if (found == notes.end())
        throw std::runtime_error("Expected editable performance note is missing");
    return static_cast<int>(std::distance(notes.begin(), found));
}
} // namespace

QJsonObject checkStaffNoteEditing()
{
    QJsonArray checks;
    bool passed = true;
    const auto check = [&](const QString &name, bool condition)
    {
        checks.append(QJsonObject{{"name", name}, {"passed", condition}});
        passed &= condition;
    };
    const auto rejects = [&](const QString &name, const std::function<void()> &operation)
    {
        bool rejected = false;
        try
        {
            operation();
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(name, rejected);
    };
    try
    {
        const auto original = fixtureProject();
        const auto before = projectToJson(original);
        StaffPerformanceNote missed;
        missed.startTick = 240;
        missed.durationTicks = 240;
        missed.midiPitch = 65;
        missed.velocity = 95;
        missed.staff = 1;
        missed.voice = "right";
        missed.pageIndex = 0;
        missed.source = {45, 30, 10, 8};
        missed.staffSpelling = StaffSpelling{'F', 0, 4};
        missed.sourceNoteIndex = 1;
        missed.tieStart = missed.tieStop = missed.unresolvedSoundTie = true;
        const auto added = addedStaffNote(original, missed);
        const auto &addedNote = added.staffPerformance->notes[std::size_t(noteIndex(added, 65, 240))];
        check("Adding a missed note preserves its explicit sound, source page and original-image rectangle",
              added.staffPerformance->notes.size() == original.staffPerformance->notes.size() + 1 &&
                  addedNote.durationTicks == 240 && addedNote.velocity == 95 && addedNote.pageIndex == 0 &&
                  addedNote.hasImageAnchor && addedNote.source.x == 45 && addedNote.source.y == 30 &&
                  addedNote.source.width == 10 && addedNote.source.height == 8);
        check("A fresh note never inherits stale guide lyrics or tie-chain identity",
              !addedNote.tieStart && !addedNote.tieStop && !addedNote.unresolvedSoundTie &&
                  addedNote.sourceNoteIndex >= 0 &&
                  added.score.notes[std::size_t(addedNote.sourceNoteIndex)].lyric.empty());
        bool retained = true;
        for (const auto &oldNote : original.staffPerformance->notes)
        {
            const auto &kept =
                added.staffPerformance
                    ->notes[std::size_t(noteIndex(added, oldNote.midiPitch, oldNote.startTick, oldNote.staff))];
            retained &= kept.durationTicks == oldNote.durationTicks && kept.voice == oldNote.voice &&
                        kept.source.x == oldNote.source.x && kept.source.y == oldNote.source.y &&
                        kept.pageIndex == oldNote.pageIndex;
        }
        check("Adding retains all existing voices and immutable page pixels",
              retained && samePixels(original, added));
        const auto addedPlan = soundPlan(added);
        check("The new note enters the real sounding plan and regenerates the guide fingerprint",
              addedPlan.valid() && addedPlan.durationTicks == 3840 &&
                  added.staffPerformance->timingFingerprint != original.staffPerformance->timingFingerprint &&
                  std::any_of(addedPlan.events.begin(), addedPlan.events.end(),
                              [](const AccompanimentEvent &event)
                              {
                                  return event.midiPitch == 65 && event.startTick == 240 &&
                                         event.durationTicks == 240 && event.velocity == 95;
                              }));
        const auto deleted = deletedStaffNote(added, noteIndex(added, 65, 240));
        const auto deletedPlan = soundPlan(deleted);
        check("Deleting removes actual sound rather than only the source overlay",
              deleted.staffPerformance->notes.size() == original.staffPerformance->notes.size() &&
                  deletedPlan.valid() &&
                  std::none_of(deletedPlan.events.begin(), deletedPlan.events.end(),
                               [](const AccompanimentEvent &event) { return event.midiPitch == 65; }) &&
                  samePixels(original, deleted));
        check("Deleting rebuilds the original guide clock without stale performance indexes",
              deleted.staffPerformance->timingFingerprint == original.staffPerformance->timingFingerprint &&
                  deleted.score.writtenMeasures.size() == original.score.writtenMeasures.size());

        QTemporaryDir packageDirectory;
        if (!packageDirectory.isValid())
            throw std::runtime_error("Note editing package fixture directory could not be created");
        const auto packagePath = packageDirectory.filePath("edited.jpp");
        saveProject(packagePath, added);
        const auto reopened = loadProject(packagePath);
        check("Saved schema-six editing reopens complete sounding events and exact original page pixels",
              samePixels(added, reopened) &&
                  staffPerformanceToJson(*added.staffPerformance) ==
                      staffPerformanceToJson(*reopened.staffPerformance) &&
                  projectToJson(reopened).value("schemaVersion").toInt() == 6);

        constexpr std::int64_t longestMeasure = std::int64_t(MaximumBeatsPerBar) * TicksPerQuarter * 4;
        constexpr int sustainedDuration = 65 * TicksPerQuarter;
        auto longBar = original;
        longBar.staffPages.resize(1);
        longBar.staffPages.front().endTick = longestMeasure;
        longBar.score.beatsPerBar = 64;
        longBar.score.beatUnit = 1;
        longBar.score.writtenMeasures = {{0, longestMeasure, 0, 64, 1, 0}};
        longBar.score.notes.resize(1);
        longBar.score.notes.front().durationTicks = sustainedDuration;
        Note longRest;
        longRest.id = 1;
        longRest.degree = 0;
        longRest.durationTicks = int(longestMeasure) - sustainedDuration;
        longRest.hasImageAnchor = false;
        longBar.score.notes.push_back(longRest);
        longBar.staffPerformance->notes.resize(2);
        longBar.staffPerformance->notes[0].durationTicks = sustainedDuration;
        longBar.staffPerformance->notes[1].durationTicks = longestMeasure;
        longBar.staffPerformance->durationTicks = longestMeasure;
        longBar.staffPerformance->timingFingerprint = staffTimingFingerprint(longBar.score);
        const auto correctedLong =
            correctedStaffProject(longBar, longBar.staffPerformance->notes, longBar.score.writtenMeasures);
        const auto longPath = packageDirectory.filePath("long-measure.jpp");
        saveProject(longPath, correctedLong);
        const auto reopenedLong = loadProject(longPath);
        const auto longPlan = soundPlan(reopenedLong);
        check("A 64/1 bar and unchanged 65-quarter note survive correction and JPP persistence without truncation",
              correctedLong.score.writtenMeasures.front().durationTicks == longestMeasure &&
                  correctedLong.score.beatsPerBar == 64 && correctedLong.score.beatUnit == 1 &&
                  correctedLong.staffPerformance->notes[0].durationTicks == sustainedDuration &&
                  correctedLong.staffPerformance->notes[1].durationTicks == longestMeasure && longPlan.valid() &&
                  longPlan.durationTicks == longestMeasure && samePixels(longBar, reopenedLong) &&
                  staffPerformanceToJson(*correctedLong.staffPerformance) ==
                      staffPerformanceToJson(*reopenedLong.staffPerformance));
        auto overlongMeasures = longBar.score.writtenMeasures;
        overlongMeasures.front().durationTicks = longestMeasure + 1;
        rejects("A correction exceeding the 256-quarter maximum written measure is rejected",
                [&] { correctedStaffProject(longBar, longBar.staffPerformance->notes, overlongMeasures); });

        constexpr int sequentialCount = 10001;
        auto sequential = original;
        sequential.staffPages.resize(1);
        sequential.staffPages.front().endTick = std::int64_t(sequentialCount) * TicksPerQuarter;
        sequential.score.beatsPerBar = 4;
        sequential.score.beatUnit = 4;
        sequential.score.notes.clear();
        sequential.score.writtenMeasures.clear();
        sequential.staffPerformance->notes.clear();
        for (int index = 0; index < sequentialCount; ++index)
        {
            const std::int64_t start = std::int64_t(index) * TicksPerQuarter;
            if (index % 4 == 0)
                sequential.score.writtenMeasures.push_back(
                    {start, std::min(4, sequentialCount - index) * TicksPerQuarter, index / 4, 4, 4, 0});
            auto guide = original.score.notes.front();
            guide.id = index;
            guide.measure = index / 4;
            guide.durationTicks = TicksPerQuarter;
            guide.lyric.clear();
            sequential.score.notes.push_back(guide);
            auto note = original.staffPerformance->notes.front();
            note.startTick = start;
            note.durationTicks = TicksPerQuarter;
            note.sourceNoteIndex = index;
            sequential.staffPerformance->notes.push_back(note);
        }
        sequential.staffPerformance->durationTicks = sequential.staffPages.front().endTick;
        sequential.staffPerformance->timingFingerprint = staffTimingFingerprint(sequential.score);
        const auto correctedSequential = correctedStaffProject(sequential, sequential.staffPerformance->notes,
                                                               sequential.score.writtenMeasures);
        check("An unchanged 10001-event score retains its complete guide and full staff clock after correction",
              correctedSequential.score.notes.size() == sequentialCount &&
                  correctedSequential.staffPerformance->notes.size() == sequentialCount &&
                  scoreToJson(correctedSequential.score).value("writtenMeasures") ==
                      scoreToJson(sequential.score).value("writtenMeasures") &&
                  staffPerformanceToJson(*correctedSequential.staffPerformance) ==
                      staffPerformanceToJson(*sequential.staffPerformance) &&
                  soundPlan(correctedSequential).valid() && samePixels(sequential, correctedSequential));

        auto invalid = missed;
        invalid.midiPitch = 128;
        rejects("An out-of-range pitch is rejected", [&] { addedStaffNote(original, invalid); });
        invalid = missed;
        invalid.durationTicks = 0;
        rejects("A nonpositive duration is rejected", [&] { addedStaffNote(original, invalid); });
        invalid = missed;
        invalid.startTick = 1800;
        rejects("A new note cannot cross its written measure boundary",
                [&] { addedStaffNote(original, invalid); });
        invalid = missed;
        invalid.pageIndex = 1;
        rejects("A source click on another page cannot silently rebind the note clock",
                [&] { addedStaffNote(original, invalid); });
        invalid = missed;
        invalid.source.x = 195;
        rejects("A source rectangle outside original pixels is rejected",
                [&] { addedStaffNote(original, invalid); });
        invalid = missed;
        invalid.source.width = std::numeric_limits<double>::quiet_NaN();
        rejects("Nonfinite source geometry is rejected", [&] { addedStaffNote(original, invalid); });
        rejects("An invalid deletion index is rejected", [&] { deletedStaffNote(original, -1); });
        const auto single = correctedStaffProject(original, {original.staffPerformance->notes.front()},
                                                  original.score.writtenMeasures);
        rejects("Deleting the final note preserves the nonempty-project invariant",
                [&] { deletedStaffNote(single, 0); });

        auto tiedNotes = original.staffPerformance->notes;
        tiedNotes.erase(std::remove_if(tiedNotes.begin(), tiedNotes.end(),
                                       [](const StaffPerformanceNote &note) { return note.staff == 1; }),
                        tiedNotes.end());
        for (int index = 0; index < 6; ++index)
        {
            auto note = original.staffPerformance->notes.front();
            note.startTick = index * 480;
            note.durationTicks = 480;
            note.midiPitch = index == 3 ? 62 : 60;
            note.pageIndex = index < 4 ? 0 : 1;
            note.source.x = 20 + index * 18;
            note.sourceNoteIndex = -1;
            note.tieStart = index == 0 || index == 1 || index == 4;
            note.tieStop = index == 1 || index == 2 || index == 5;
            tiedNotes.push_back(note);
        }
        const auto tied = correctedStaffProject(original, std::move(tiedNotes), original.score.writtenMeasures);
        const auto tiedBefore = projectToJson(tied);
        const auto removedTie = deletedStaffNote(tied, noteIndex(tied, 60, 480));
        const auto &first = removedTie.staffPerformance->notes[std::size_t(noteIndex(removedTie, 60, 0))];
        const auto &last = removedTie.staffPerformance->notes[std::size_t(noteIndex(removedTie, 60, 960))];
        const auto &unrelatedStart =
            removedTie.staffPerformance->notes[std::size_t(noteIndex(removedTie, 60, 1920))];
        const auto &unrelatedStop =
            removedTie.staffPerformance->notes[std::size_t(noteIndex(removedTie, 60, 2400))];
        check(
            "Deleting a tied segment retains neighboring durations and marks only its connected chain for review",
            first.tieStart && last.tieStop && first.durationTicks == 480 && last.durationTicks == 480 &&
                first.unresolvedSoundTie && last.unresolvedSoundTie && !unrelatedStart.unresolvedSoundTie &&
                !unrelatedStop.unresolvedSoundTie && unrelatedStart.tieStart && unrelatedStop.tieStop &&
                soundPlan(removedTie).valid() &&
                removedTie.staffPerformance->notes.size() + 1 == tied.staffPerformance->notes.size());
        const auto otherVoices = [](const Project &project)
        {
            QJsonArray notes;
            for (const auto &entry : staffPerformanceToJson(*project.staffPerformance).value("notes").toArray())
                if (entry.toObject().value("staff").toInt() != 1)
                    notes.append(entry);
            return notes;
        };
        const auto preservesConnectedReview = [&](const Project &updated)
        {
            const auto &preceding = updated.staffPerformance->notes[std::size_t(noteIndex(updated, 60, 0))];
            const auto &following = updated.staffPerformance->notes[std::size_t(noteIndex(updated, 60, 960))];
            const auto &separateStart = updated.staffPerformance->notes[std::size_t(noteIndex(updated, 60, 1920))];
            const auto &separateStop = updated.staffPerformance->notes[std::size_t(noteIndex(updated, 60, 2400))];
            return preceding.tieStart && following.tieStop && preceding.durationTicks == 480 &&
                   following.durationTicks == 480 && preceding.unresolvedSoundTie &&
                   following.unresolvedSoundTie && separateStart.tieStart && separateStop.tieStop &&
                   !separateStart.unresolvedSoundTie && !separateStop.unresolvedSoundTie &&
                   otherVoices(updated) == otherVoices(tied) && samePixels(updated, tied) &&
                   updated.staffPerformance->notes.size() == tied.staffPerformance->notes.size();
        };
        const int middleIndex = noteIndex(tied, 60, 480);
        auto replacement = tied.staffPerformance->notes[std::size_t(middleIndex)];
        replacement.midiPitch = 61;
        const auto pitchUpdated = updatedStaffNote(tied, middleIndex, replacement);
        const auto &newPitch = pitchUpdated.staffPerformance->notes[std::size_t(noteIndex(pitchUpdated, 61, 480))];
        const auto pitchPlan = soundPlan(pitchUpdated);
        check("Updating a tied segment's pitch changes actual sound and preserves only connected-chain review",
              newPitch.tieStart && newPitch.tieStop && newPitch.unresolvedSoundTie &&
                  newPitch.durationTicks == 480 && preservesConnectedReview(pitchUpdated) && pitchPlan.valid() &&
                  std::any_of(
                      pitchPlan.events.begin(), pitchPlan.events.end(), [](const AccompanimentEvent &event)
                      { return event.midiPitch == 61 && event.startTick == 480 && event.durationTicks == 480; }));
        replacement = tied.staffPerformance->notes[std::size_t(middleIndex)];
        replacement.startTick = 600;
        replacement.durationTicks = 240;
        const auto timingUpdated = updatedStaffNote(tied, middleIndex, replacement);
        const auto &newTiming =
            timingUpdated.staffPerformance->notes[std::size_t(noteIndex(timingUpdated, 60, 600))];
        const auto timingPlan = soundPlan(timingUpdated);
        check(
            "Updating a tied segment's timing keeps source pixels and other voices without inventing continuation",
            newTiming.tieStart && newTiming.tieStop && newTiming.unresolvedSoundTie &&
                newTiming.durationTicks == 240 && preservesConnectedReview(timingUpdated) && timingPlan.valid() &&
                std::any_of(
                    timingPlan.events.begin(), timingPlan.events.end(), [](const AccompanimentEvent &event)
                    { return event.midiPitch == 60 && event.startTick == 600 && event.durationTicks == 240; }));
        replacement.midiPitch = 128;
        rejects("Updating rejects an invalid MIDI pitch without mutating the original tied project",
                [&] { updatedStaffNote(tied, middleIndex, replacement); });
        check("Successful and rejected edits never mutate either input project",
              projectToJson(original) == before && projectToJson(tied) == tiedBefore &&
                  samePixels(tied, removedTie));
    }
    catch (const std::exception &error)
    {
        check(QString::fromUtf8(error.what()), false);
    }
    return {{"passed", passed},
            {"checks", checks},
            {"validationScope",
             "Native original-image note addition/deletion/update, sounding plan and JPP persistence"},
            {"audioHardwareTested", false}};
}
} // namespace singlilt

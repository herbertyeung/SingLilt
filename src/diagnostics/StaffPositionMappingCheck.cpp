// Staff-position pitch and timing suggestion regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffPositionMappingCheck.h"

#include "application/StaffNoteEditing.h"
#include "application/StaffPositionMapping.h"
#include "application/StaffScoreCorrection.h"
#include "application/StaffTimingMapping.h"
#include <QJsonArray>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
Project fixtureProject()
{
    Project project;
    project.staffImagePlayback = true;
    project.notationStyle = NotationStyle::Staff;
    project.processing = {{"local", true}};
    project.staffKeyFifths = 4;
    project.score.tonic = 4;
    project.score.writtenMeasures = {{0, 1920, 0, 4, 4, 0}, {1920, 1920, 1, 4, 4, 1}};
    project.staffPerformance.emplace();
    auto &performance = *project.staffPerformance;
    performance.durationTicks = 3840;
    performance.sourceTonic = 4;
    performance.clefChanges = {{0, 1, false}, {0, 2, true}};
    for (int page = 0; page < 2; ++page)
    {
        const int verticalOffset = page * 40;
        QImage image(600, 360, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(QPen(Qt::black, 1));
        for (int top : {60 + verticalOffset, 180 + verticalOffset})
            for (int line = 0; line < 5; ++line)
                painter.drawLine(40, top + line * 10, 560, top + line * 10);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        for (int staff = 1; staff <= 2; ++staff)
            for (int position = 0; position < 2; ++position)
            {
                StaffPerformanceNote note;
                note.staff = staff;
                note.voice = staff == 1 ? "upper" : "lower";
                note.startTick = page * 1920 + position * 960;
                note.durationTicks = 480;
                note.pageIndex = page;
                note.sourceNoteIndex = staff == 1 && position == 0 ? page : -1;
                if (staff == 1)
                {
                    note.midiPitch = position == 0 ? 64 : 69;
                    note.staffSpelling = position == 0 ? StaffSpelling{'E', 0, 4} : StaffSpelling{'A', 0, 4};
                    note.source = {95.0 + position * 260, 96.0 + verticalOffset - position * 15, 10, 8};
                }
                else
                {
                    note.midiPitch = position == 0 ? 44 : 51;
                    note.staffSpelling = position == 0 ? StaffSpelling{'G', 1, 2} : StaffSpelling{'D', 1, 3};
                    note.source = {95.0 + position * 260, 216.0 + verticalOffset - position * 20, 10, 8};
                }
                painter.drawEllipse(QRectF(note.source.x, note.source.y, note.source.width, note.source.height));
                performance.notes.push_back(note);
            }
        painter.end();
        project.staffPages.push_back(
            {QString("Measured line fixture %1").arg(page + 1), image, image, page * 1920, (page + 1) * 1920});
        Note guide;
        guide.id = page;
        guide.degree = 1;
        guide.durationTicks = 1920;
        guide.measure = page;
        guide.pageIndex = page;
        guide.source = {95, 96.0 + verticalOffset, 10, 8};
        project.score.notes.push_back(guide);
    }
    project.image = project.staffPages.front().sourceImage;
    performance.timingFingerprint = staffTimingFingerprint(project.score);
    auto notes = performance.notes;
    return correctedStaffProject(project, std::move(notes), project.score.writtenMeasures);
}

const StaffPerformanceNote &upperStart(const Project &project)
{
    const auto found =
        std::find_if(project.staffPerformance->notes.begin(), project.staffPerformance->notes.end(),
                     [](const StaffPerformanceNote &note) { return note.staff == 1 && note.startTick == 0; });
    if (found == project.staffPerformance->notes.end())
        throw std::runtime_error("The measured position fixture has no initial upper note");
    return *found;
}

QPointF center(const SourceRect &box)
{
    return {box.x + box.width / 2, box.y + box.height / 2};
}

Project timingFixture()
{
    auto project = fixtureProject();
    project.staffPages.resize(1);
    project.score.notes.clear();
    project.score.writtenMeasures = {{0, 1920, 0, 4, 4, 0}, {1920, 1440, 1, 3, 4, 0}, {3360, 960, 2, 2, 4, 0}};
    for (const auto &measure : project.score.writtenMeasures)
    {
        Note rest;
        rest.id = static_cast<int>(project.score.notes.size());
        rest.degree = 0;
        rest.measure = measure.number;
        rest.durationTicks = static_cast<int>(measure.durationTicks);
        rest.source = {};
        rest.hasImageAnchor = false;
        project.score.notes.push_back(rest);
    }
    auto &performance = *project.staffPerformance;
    performance.notes.clear();
    performance.durationTicks = 4320;
    project.staffPages[0].endTick = 4320;
    QPainter painter(&project.staffPages[0].sourceImage);
    painter.setPen(QPen(Qt::black, 2));
    for (int x : {270, 450})
        painter.drawLine(x, 60, x, 220);
    painter.end();
    project.staffPages[0].renderedImage = project.staffPages[0].sourceImage;
    project.image = project.staffPages[0].sourceImage;
    const auto append = [&](int staff, double x, std::int64_t tick, std::int64_t duration)
    {
        StaffPerformanceNote note;
        note.staff = staff;
        note.voice = staff == 1 ? "upper" : "lower";
        note.startTick = tick;
        note.durationTicks = duration;
        note.midiPitch = staff == 1 ? 64 : 44;
        note.source = {x - 5, staff == 1 ? 96.0 : 216.0, 10, 8};
        performance.notes.push_back(note);
    };
    append(1, 100, 0, 60);
    append(1, 180, 480, 480);
    append(1, 250, 960, 480);
    append(2, 100, 0, 240);
    append(2, 180, 480, 240);
    append(2, 250, 960, 240);
    append(1, 390, 2400, 480);
    append(2, 300, 1920, 240);
    append(2, 350, 2160, 240);
    append(2, 400, 2400, 240);
    append(2, 470, 3360, 240);
    append(2, 510, 3600, 240);
    append(2, 550, 4080, 240);
    performance.timingFingerprint = staffTimingFingerprint(project.score);
    auto notes = performance.notes;
    return correctedStaffProject(project, std::move(notes), project.score.writtenMeasures);
}

QJsonObject timingEdit(const StaffPerformanceNote &note, const QString &origin)
{
    return {{"pageIndex", note.pageIndex},
            {"staff", note.staff},
            {"voice", QString::fromStdString(note.voice)},
            {"startTick", double(note.startTick)},
            {"midiPitch", note.midiPitch},
            {"source", QJsonArray{note.source.x, note.source.y, note.source.width, note.source.height}},
            {"originalSource", QJsonArray{}},
            {"timingSource", origin}};
}
} // namespace

QJsonObject checkStaffPositionMapping()
{
    QJsonArray checks;
    bool passed = true;
    const auto check = [&](const QString &name, bool condition)
    {
        checks.append(QJsonObject{{"name", name}, {"passed", condition}});
        passed &= condition;
    };
    try
    {
        const auto original = fixtureProject();
        const auto before = projectToJson(original);
        const auto upper = suggestStaffNoteAt(original, 0, {100, 100});
        check("Measured treble bottom line maps to written E4 and returns its exact five image lines",
              upper && upper->note.midiPitch == 64 && upper->note.staff == 1 && upper->system == 0 &&
                  upper->staffLines == std::array<double, 5>{60, 70, 80, 90, 100});
        const auto half = suggestStaffNoteAt(original, 0, {100, 95.8});
        check("Half-line movement is one diatonic F step, with four-sharp key metadata supplying F sharp",
              half && half->note.midiPitch == 66 && half->note.staffSpelling &&
                  half->note.staffSpelling->step == 'F' && half->note.staffSpelling->alter == 1 &&
                  std::abs(center(half->note.source).y() - 95) < 0.01 && half->note.startTick == 0);
        const auto bass = suggestStaffNoteAt(original, 0, {100, 220});
        const auto bassSpace = suggestStaffNoteAt(original, 0, {100, 215});
        check("Measured lower staff uses F4-clef G2 bottom and A2 first space rather than treble pitches",
              bass && bass->note.staff == 2 && bass->note.midiPitch == 44 && bassSpace &&
                  bassSpace->note.midiPitch == 45);
        const auto ledger = suggestStaffNoteAt(original, 0, {100, 40});
        check("Ledger positions preserve the written C6 octave and key-signature accidental",
              ledger && ledger->note.midiPitch == 85 && ledger->note.staffSpelling &&
                  ledger->note.staffSpelling->step == 'C' && ledger->note.staffSpelling->octave == 6 &&
                  ledger->note.staffSpelling->alter == 1);
        const auto laterPage = suggestStaffNoteAt(original, 1, {100, 140});
        check("Page mapping uses that page's measured lines and existing written clock",
              laterPage && laterPage->note.pageIndex == 1 && laterPage->note.startTick == 1920 &&
                  laterPage->note.midiPitch == 64 && laterPage->staffLines.back() == 140);

        StaffPerformanceNote natural;
        natural.staff = 1;
        natural.voice = "upper";
        natural.midiPitch = 65;
        natural.staffSpelling = StaffSpelling{'F', 0, 4};
        natural.startTick = 480;
        natural.durationTicks = 480;
        natural.source = {215, 91, 10, 8};
        const auto localAccidental = addedStaffNote(original, natural);
        const auto beforeNatural = suggestStaffNoteAt(localAccidental, 0, {100, 95});
        const auto afterNatural = suggestStaffNoteAt(localAccidental, 0, {360, 95});
        const auto otherOctave = suggestStaffNoteAt(localAccidental, 0, {360, 60});
        check("A local natural affects later same-staff/same-octave candidates, never earlier notes or another "
              "octave",
              beforeNatural && beforeNatural->note.midiPitch == 66 && afterNatural &&
                  afterNatural->note.midiPitch == 65 && otherOctave && otherOctave->note.midiPitch == 78);

        auto changedClef = original;
        changedClef.staffPerformance->clefChanges.push_back({960, 1, true});
        changedClef.staffPerformance->clefChanges.push_back({1920, 1, false});
        const auto laterClef = suggestStaffNoteAt(changedClef, 0, {360, 100});
        const auto lockedDrag = suggestStaffNotePosition(changedClef, upperStart(changedClef), {360, 95});
        check(
            "Nearby-note suggestions follow clef changes, while dragging locks the selected event's clef and time",
            laterClef && laterClef->note.midiPitch == 44 && laterClef->note.startTick == 960 && lockedDrag &&
                lockedDrag->note.midiPitch == 66 && lockedDrag->note.startTick == 0 &&
                lockedDrag->note.durationTicks == upperStart(changedClef).durationTicks &&
                lockedDrag->note.staff == 1);
        check("Cross-staff clicks choose the physical lower staff, but a locked upper-note drag never silently "
              "switches hands",
              bass && bass->note.staff == 2 &&
                  !suggestStaffNotePosition(original, upperStart(original), {100, 220}));

        bool roundtrip = true;
        const auto &existing = upperStart(original);
        for (int midi : {61, 63, 66, 68, 69, 71, 73, 76, 78, 81, 85})
        {
            const auto spelling = staffSpellingForPitch(original, existing, midi);
            const auto box = staffAnchorForPitch(original, existing, midi, spelling);
            if (!spelling || !box)
            {
                roundtrip = false;
                continue;
            }
            auto preview = existing;
            preview.midiPitch = midi;
            preview.staffSpelling = spelling;
            preview.source = *box;
            const auto projected = suggestStaffNotePosition(original, preview, center(*box));
            roundtrip &= projected && projected->note.midiPitch == midi && box->x == existing.source.x &&
                         box->width == existing.source.width && box->height == existing.source.height &&
                         projected->note.startTick == existing.startTick &&
                         projected->staffLines == std::array<double, 5>{60, 70, 80, 90, 100};
        }
        check("Pitch/spelling inverse roundtrips through measured half-lines and ledgers without changing x/time",
              roundtrip);
        const auto chromatic = staffSpellingForPitch(original, existing, 67);
        const auto chromaticBox = staffAnchorForPitch(original, existing, 67, chromatic);
        auto chromaticPreview = existing;
        if (chromatic && chromaticBox)
        {
            chromaticPreview.midiPitch = 67;
            chromaticPreview.staffSpelling = chromatic;
            chromaticPreview.source = *chromaticBox;
        }
        const auto chromaticHint =
            chromaticBox ? suggestStaffNotePosition(original, chromaticPreview, center(*chromaticBox))
                         : std::nullopt;
        check("A numeric chromatic G-natural edit is not overwritten by the default G-sharp key during preview",
              chromatic && chromatic->step == 'G' && chromatic->alter == 0 && chromaticHint &&
                  chromaticHint->note.midiPitch == 67);
        auto flatKey = original;
        flatKey.staffKeyFifths = -2;
        const auto flat = suggestStaffNoteAt(flatKey, 0, {100, 80});
        check("Flat key metadata produces B-flat at the same diatonic staff position",
              flat && flat->note.midiPitch == 70 && flat->note.staffSpelling &&
                  flat->note.staffSpelling->alter == -1);

        check("Header whitespace, ambiguous middle gap and invalid pages do not receive fake precise pitches",
              !suggestStaffNoteAt(original, 0, {100, 10}) && !suggestStaffNoteAt(original, 0, {100, 140}) &&
                  !suggestStaffNoteAt(original, 2, {100, 100}));
        auto blank = original;
        blank.staffPages[0].sourceImage.fill(Qt::white);
        auto invalidClock = existing;
        invalidClock.startTick = -1;
        check("Missing real staff lines and out-of-range MIDI reject positioning instead of inventing geometry",
              !suggestStaffNoteAt(blank, 0, {100, 100}) && !staffAnchorForPitch(blank, existing, 66) &&
                  !staffSpellingForPitch(original, existing, 128) &&
                  !staffAnchorForPitch(original, existing, 66, StaffSpelling{'F', 0, 4}) &&
                  !staffAnchorForPitch(original, invalidClock, 66, StaffSpelling{'F', 1, 4}));
        check("Suggestions and inverse previews preserve source pixels and musical project state",
              projectToJson(original) == before && original.staffPages[0].sourceImage == original.image);

        const auto clock = timingFixture();
        const auto clockBefore = projectToJson(clock);
        const auto staves = crispStaffImageLines(clock.image, 2);
        if (staves.size() != 2)
            throw std::runtime_error("The timing fixture has no measured paired staff");
        const auto &staff = staves.front();
        const auto local = suggestStaffTimingAt(clock, 0, staff, staves, {140, 95}, 2);
        check("Local distance preserves zero-based origin and uses the preceding anchor, not a nearer future note",
              local && !local->sameBeat && local->startTick == 240 && local->referenceIndex == 0 &&
                  local->durationTicks == 240 && local->measureIndex == 0);
        const auto chordOne = suggestStaffTimingAt(clock, 0, staff, staves, {180, 70}, 0);
        const auto chordTwo = suggestStaffTimingAt(clock, 0, staff, staves, {180, 80}, 0);
        const auto chordThree = suggestStaffTimingAt(clock, 0, staff, staves, {180, 90}, 0);
        const auto explicitChord = suggestStaffTimingAt(clock, 0, staff, staves, {220, 90}, 0, 0);
        check("Three vertical pitches share one existing beat/column and explicit chord selection locks its "
              "reference",
              chordOne && chordTwo && chordThree && chordOne->sameBeat && chordTwo->sameBeat &&
                  chordThree->sameBeat && chordOne->startTick == 480 && chordTwo->startTick == 480 &&
                  chordThree->startTick == 480 && chordOne->alignedX == 180 && explicitChord &&
                  explicitChord->startTick == 0 && explicitChord->alignedX == 100);
        const auto fallback = suggestStaffTimingAt(clock, 0, staff, staves, {325, 95}, 2);
        check("A paired physical bar prevents cross-measure interpolation and lower-voice anchors supply only the "
              "local clock",
              fallback && fallback->measureIndex == 1 && fallback->startTick == 2040 &&
                  clock.staffPerformance->notes[std::size_t(fallback->referenceIndex)].staff == 2 &&
                  fallback->durationTicks == 240);
        auto separatedBar = clock;
        {
            QPainter painter(&separatedBar.staffPages[0].sourceImage);
            painter.fillRect(QRect(268, 101, 5, 79), Qt::white);
            painter.setPen(QPen(Qt::black, 1));
            painter.drawLine(276, 60, 276, 100);
            painter.drawLine(276, 180, 276, 220);
        }
        separatedBar.image = separatedBar.staffPages[0].sourceImage;
        const auto separatedStaves = crispStaffImageLines(separatedBar.image, 2);
        const auto separatedHint =
            separatedStaves.size() == 2
                ? suggestStaffTimingAt(separatedBar, 0, separatedStaves.front(), separatedStaves, {325, 95}, 2)
                : std::nullopt;
        check("A paired double bar remains a measure boundary when its two staff strokes do not join in the gap",
              separatedHint && separatedHint->measureIndex == 1 && separatedHint->startTick == 2040 &&
                  separatedHint->durationTicks == 240);
        auto legacy = clock;
        auto misleading = legacy.staffPerformance->notes.front();
        misleading.startTick = 1050;
        misleading.source.x = 175;
        misleading.midiPitch = 72;
        legacy.staffPerformance->notes.push_back(misleading);
        legacy.processing.insert("staffVisualEdits", QJsonArray{timingEdit(misleading, {})});
        const auto cleanColumn = suggestStaffTimingAt(legacy, 0, staff, staves, {180, 90}, 0);
        const auto legacyDistance = suggestStaffTimingAt(legacy, 0, staff, staves, {140, 95}, 2);
        check("Legacy unconfirmed added red notes never override a recognized same-x clock or calibrate distance",
              cleanColumn && cleanColumn->sameBeat && cleanColumn->startTick == 480 && legacyDistance &&
                  legacyDistance->startTick == 240);
        auto automatic = clock;
        auto estimated = automatic.staffPerformance->notes.front();
        estimated.startTick = 360;
        estimated.durationTicks = 120;
        estimated.source.x = 150;
        estimated.midiPitch = 72;
        automatic.staffPerformance->notes.push_back(estimated);
        automatic.processing.insert("staffVisualEdits", QJsonArray{timingEdit(estimated, "distance")});
        const auto estimatedChord = suggestStaffTimingAt(automatic, 0, staff, staves, {155, 90}, 0);
        const auto independentDistance = suggestStaffTimingAt(automatic, 0, staff, staves, {140, 95}, 2);
        check("New automatic notes can anchor a chord without becoming self-amplifying proportional timing "
              "references",
              estimatedChord && estimatedChord->sameBeat && estimatedChord->startTick == 360 &&
                  independentDistance && independentDistance->referenceIndex == 0 &&
                  independentDistance->startTick == 240);
        automatic.processing.insert("staffVisualEdits", QJsonArray{timingEdit(estimated, "manual")});
        const auto confirmed = suggestStaffTimingAt(automatic, 0, staff, staves, {165, 95}, 2);
        check("Explicitly confirmed manual timing may calibrate a later local candidate",
              confirmed && confirmed->referenceIndex == int(automatic.staffPerformance->notes.size()) - 1 &&
                  confirmed->startTick == 420);
        const auto finalBar = suggestStaffTimingAt(clock, 0, staff, staves, {490, 95}, 2);
        check("Variable written bar lengths bound estimates on their own measure clock",
              finalBar && finalBar->measureIndex == 2 && finalBar->startTick == 3480 &&
                  finalBar->durationTicks <= 4320 - finalBar->startTick);
        auto broken = clock;
        for (auto &note : broken.staffPerformance->notes)
            if (note.startTick == 960)
                note.startTick = 240;
        auto otherSystem = staff;
        otherSystem.systemIndex = 99;
        check("Conflicting/nonmonotone clocks, foreign systems and invalid geometry do not fabricate timing",
              !suggestStaffTimingAt(broken, 0, staff, staves, {140, 95}, 2) &&
                  !suggestStaffTimingAt(clock, 0, otherSystem, staves, {140, 95}, 2) &&
                  !suggestStaffTimingAt(clock, 9, staff, staves, {140, 95}, 2) &&
                  !suggestStaffTimingAt(clock, 0, staff, staves, {140, 95}, -1));
        auto sparse = clock;
        sparse.staffPerformance->notes.erase(
            std::remove_if(sparse.staffPerformance->notes.begin(), sparse.staffPerformance->notes.end(),
                           [](const StaffPerformanceNote &note)
                           { return note.startTick > 0 && note.startTick < 1920; }),
            sparse.staffPerformance->notes.end());
        check("A bar with only held-note evidence requires manual time instead of borrowing across its barline",
              !suggestStaffTimingAt(sparse, 0, staff, staves, {140, 95}, 0) &&
                  projectToJson(clock) == clockBefore);

        const auto moving = std::find_if(clock.staffPerformance->notes.begin(),
                                         clock.staffPerformance->notes.end(), [](const StaffPerformanceNote &note)
                                         { return note.staff == 1 && note.startTick == 480; });
        if (moving == clock.staffPerformance->notes.end())
            throw std::runtime_error("The timing fixture has no movable upper note");
        const int movingIndex = static_cast<int>(std::distance(clock.staffPerformance->notes.begin(), moving));
        const auto rightMove = suggestStaffTimingMove(clock, movingIndex, {200, 100});
        const auto leftMove = suggestStaffTimingMove(clock, movingIndex, {160, 100});
        check("Horizontal drag increases/decreases the existing onset while preserving its duration and measure",
              rightMove && leftMove && rightMove->startTick == 600 && leftMove->startTick == 360 &&
                  rightMove->durationTicks == moving->durationTicks &&
                  leftMove->durationTicks == moving->durationTicks && rightMove->measureIndex == 0 &&
                  leftMove->measureIndex == 0 && projectToJson(clock) == clockBefore);

        auto offsetClock = clock;
        auto &offsetNote = offsetClock.staffPerformance->notes[std::size_t(movingIndex)];
        offsetNote.startTick = 575;
        offsetClock.processing.insert("staffVisualEdits", QJsonArray{timingEdit(offsetNote, "manual")});
        const auto offsetMove = suggestStaffTimingMove(offsetClock, movingIndex, {200, 100});
        check("The moving note never calibrates itself and a non-grid manual onset retains its original offset",
              offsetMove && offsetMove->startTick == 695 && offsetMove->referenceIndex != movingIndex &&
                  offsetMove->durationTicks == offsetNote.durationTicks);

        auto secondSystem = clock;
        QImage taller(600, 720, QImage::Format_RGB32);
        taller.fill(Qt::white);
        {
            QPainter painter(&taller);
            painter.drawImage(0, 0, clock.image);
            painter.setPen(QPen(Qt::black, 1));
            for (int top : {420, 540})
                for (int line = 0; line < 5; ++line)
                    painter.drawLine(40, top + line * 10, 560, top + line * 10);
        }
        secondSystem.staffPages[0].sourceImage = taller;
        secondSystem.image = taller;
        auto missingPage = clock;
        missingPage.staffPerformance->notes[std::size_t(movingIndex)].pageIndex = 1;
        check("Dragging does not cross a physical measure, staff/system or unavailable source page",
              !suggestStaffTimingMove(clock, movingIndex, {280, 100}) &&
                  !suggestStaffTimingMove(clock, movingIndex, {200, 220}) &&
                  crispStaffImageLines(taller, 2).size() == 4 &&
                  !suggestStaffTimingMove(secondSystem, movingIndex, {200, 460}) &&
                  !suggestStaffTimingMove(missingPage, movingIndex, {200, 100}));

        const auto beginning = suggestStaffTimingMove(clock, movingIndex, {50, 100});
        offsetNote.startTick = 1300;
        const auto ending = suggestStaffTimingMove(offsetClock, movingIndex, {260, 100});
        check("Edge movement clamps to the measure start or end minus the unchanged duration, never to a new bar",
              beginning && beginning->startTick == 0 && ending && ending->startTick == 1440 &&
                  beginning->durationTicks == 480 && ending->durationTicks == 480);

        auto isolated = clock;
        isolated.staffPerformance->notes = {*moving};
        const auto unchanged = suggestStaffTimingMove(isolated, 0, {180, 90});
        check("An isolated note needs manual horizontal timing, while a zero-x move retains its clock",
              !suggestStaffTimingMove(isolated, 0, {200, 100}) && unchanged &&
                  unchanged->startTick == moving->startTick && unchanged->durationTicks == moving->durationTicks);

        auto invalidDuration = clock;
        invalidDuration.staffPerformance->notes[std::size_t(movingIndex)].durationTicks = 0;
        const bool zeroRejected = !suggestStaffTimingMove(invalidDuration, movingIndex, {180, 100});
        invalidDuration.staffPerformance->notes[std::size_t(movingIndex)].durationTicks = 1441;
        check("Malformed or over-bar durations and invalid performance indices never receive a timing preview",
              zeroRejected && !suggestStaffTimingMove(invalidDuration, movingIndex, {200, 100}) &&
                  !suggestStaffTimingMove(clock, -1, {200, 100}) &&
                  !suggestStaffTimingMove(clock, int(clock.staffPerformance->notes.size()), {200, 100}));
    }
    catch (const std::exception &error)
    {
        check(QString::fromUtf8(error.what()), false);
    }
    return {{"passed", passed},
            {"checks", checks},
            {"validationScope",
             "Measured original-image staff geometry, written pitch/key/clef mapping, inverse preview and local "
             "timing estimates"},
            {"recognitionAccuracyMeasured", false}};
}
} // namespace singlilt

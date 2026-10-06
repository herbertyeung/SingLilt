// Pitch suggestions from staff lines, clefs, and accidentals.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffPositionMapping.h"
#include "StaffTimingMapping.h"

#include "recognition/CrispStaffSourceAnchors.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace singlilt
{
namespace
{
constexpr std::string_view PitchLetters = "CDEFGAB";
constexpr std::array<int, 7> NaturalPitches{0, 2, 4, 5, 7, 9, 11};

const QImage *sourceImage(const Project &project, int page)
{
    if (!project.staffImagePlayback || !project.staffPerformance || page < 0 ||
        std::size_t(page) >= project.staffPages.size() || project.staffPerformance->staffCount < 1 ||
        project.staffPerformance->staffCount > 2 || project.staffKeyFifths < -7 || project.staffKeyFifths > 7)
        return nullptr;
    const auto &image = project.staffPages[std::size_t(page)].sourceImage;
    return image.isNull() ? nullptr : &image;
}

bool inImage(const QImage &image, QPointF point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y()) && point.x() >= 0 && point.y() >= 0 &&
           point.x() < image.width() && point.y() < image.height();
}

bool validAnchor(const QImage &image, const StaffPerformanceNote &note)
{
    const auto &box = note.source;
    return note.hasImageAnchor && std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) &&
           std::isfinite(box.height) && box.x >= 0 && box.y >= 0 && box.width > 0 && box.height > 0 &&
           box.x + box.width <= image.width() && box.y + box.height <= image.height();
}

QPointF center(const SourceRect &box)
{
    return {box.x + box.width / 2, box.y + box.height / 2};
}

std::optional<std::size_t> measureAt(const Project &project, int page, std::int64_t tick)
{
    const auto &measures = project.score.writtenMeasures;
    const auto after = std::upper_bound(measures.begin(), measures.end(), tick,
                                        [](std::int64_t position, const WrittenMeasure &measure)
                                        { return position < measure.startTick; });
    if (after == measures.begin())
        return {};
    const auto index = std::size_t(std::distance(measures.begin(), after) - 1);
    const auto &measure = measures[index];
    if (measure.pageIndex != page || measure.durationTicks <= 0 || tick < measure.startTick ||
        tick - measure.startTick >= measure.durationTicks)
        return {};
    return index;
}

std::optional<int> writtenIndex(const StaffSpelling &spelling)
{
    const auto step = PitchLetters.find(spelling.step);
    if (step == std::string_view::npos || spelling.octave < -1 || spelling.octave > 9 || spelling.alter < -2 ||
        spelling.alter > 2)
        return {};
    return spelling.octave * 7 + static_cast<int>(step);
}

std::optional<int> writtenMidi(const StaffSpelling &spelling)
{
    if (!writtenIndex(spelling))
        return {};
    const auto step = PitchLetters.find(spelling.step);
    const int midi = (spelling.octave + 1) * 12 + NaturalPitches[step] + spelling.alter;
    return midi >= 0 && midi <= 127 ? std::optional<int>(midi) : std::nullopt;
}

StaffSpelling spellingAt(int diatonic, int alter)
{
    int octave = diatonic / 7;
    int step = diatonic % 7;
    if (step < 0)
    {
        step += 7;
        --octave;
    }
    return {PitchLetters[std::size_t(step)], alter, octave};
}

int signatureAlter(int fifths, int step)
{
    constexpr std::array<int, 7> SharpOrder{3, 0, 4, 1, 5, 2, 6};
    constexpr std::array<int, 7> FlatOrder{6, 2, 5, 1, 4, 0, 3};
    const auto &order = fifths >= 0 ? SharpOrder : FlatOrder;
    for (int index = 0; index < std::abs(fifths); ++index)
        if (order[std::size_t(index)] == step)
            return fifths >= 0 ? 1 : -1;
    return 0;
}

std::optional<int> currentAlter(const Project &project, const StaffPerformanceNote &context, int diatonic)
{
    const auto measure = measureAt(project, context.pageIndex, context.startTick);
    if (!measure)
        return {};
    const auto natural = spellingAt(diatonic, 0);
    const auto index = writtenIndex(natural);
    if (!index)
        return {};
    int alter = signatureAlter(project.staffKeyFifths, static_cast<int>(PitchLetters.find(natural.step)));
    std::int64_t latest = -1;
    bool ambiguous = false;
    for (const auto &note : project.staffPerformance->notes)
    {
        if (note.staff != context.staff || note.pageIndex != context.pageIndex ||
            note.startTick > context.startTick ||
            note.startTick < project.score.writtenMeasures[*measure].startTick || note.tieStop ||
            !note.staffSpelling || writtenMidi(*note.staffSpelling) != note.midiPitch ||
            writtenIndex(*note.staffSpelling) != diatonic)
            continue;
        if (note.startTick > latest)
        {
            latest = note.startTick;
            alter = note.staffSpelling->alter;
            ambiguous = false;
        }
        else if (note.startTick == latest && alter != note.staffSpelling->alter)
            ambiguous = true;
    }
    // The selected event's explicit spelling also survives an uncommitted properties preview.
    if (context.staffSpelling && writtenMidi(*context.staffSpelling) == context.midiPitch &&
        writtenIndex(*context.staffSpelling) == diatonic)
        return context.staffSpelling->alter;
    return ambiguous ? std::nullopt : std::optional<int>(alter);
}

std::optional<int> bottomLine(const Project &project, int staff, std::int64_t tick)
{
    bool bass = project.staffBassClef;
    bool found = false;
    bool ambiguous = false;
    std::int64_t latest = -1;
    for (const auto &change : project.staffPerformance->clefChanges)
    {
        if (change.staff != staff || change.startTick > tick)
            continue;
        if (change.startTick > latest)
        {
            latest = change.startTick;
            bass = change.bassClef;
            found = true;
            ambiguous = false;
        }
        else if (change.startTick == latest && bass != change.bassClef)
            ambiguous = true;
    }
    if (ambiguous || (!found && staff != project.staffPerformance->primaryStaff))
        return {};
    return bass ? 2 * 7 + 4 : 4 * 7 + 2; // G2 and E4 are the standard bass/treble bottom lines.
}

bool contains(const StaffImageLines &staff, QPointF point, bool limitedLedger)
{
    if (!staff.bounds.contains(point) || staff.spacing <= 0)
        return false;
    if (!limitedLedger)
        return true;
    // A new click in header/tempo whitespace does not imply an arbitrarily high ledger note.
    return point.y() >= staff.linePositions.front() - staff.spacing * 4 &&
           point.y() <= staff.linePositions.back() + staff.spacing * 4;
}

const StaffImageLines *staffAt(const std::vector<StaffImageLines> &staves, QPointF point, int requiredStaff = 0,
                               bool limitedLedger = true)
{
    const StaffImageLines *selected = nullptr;
    double nearest = std::numeric_limits<double>::max();
    bool ambiguous = false;
    for (const auto &staff : staves)
    {
        if ((requiredStaff != 0 && staff.staff != requiredStaff) || !contains(staff, point, limitedLedger))
            continue;
        const double distance =
            std::max({staff.linePositions.front() - point.y(), point.y() - staff.linePositions.back(), 0.0});
        if (!selected || distance < nearest - staff.spacing * 0.1)
        {
            selected = &staff;
            nearest = distance;
            ambiguous = false;
        }
        else if (std::abs(distance - nearest) <= staff.spacing * 0.1)
            ambiguous = true;
    }
    return ambiguous ? nullptr : selected;
}

int nearestNote(const Project &project, int page, const QImage &image, const std::vector<StaffImageLines> &staves,
                const StaffImageLines &selected, QPointF point)
{
    int nearest = -1;
    bool sameStaff = false;
    double distance = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < project.staffPerformance->notes.size(); ++index)
    {
        const auto &note = project.staffPerformance->notes[index];
        if (note.pageIndex != page || note.durationTicks <= 0 || !validAnchor(image, note))
            continue;
        const auto anchor = center(note.source);
        const auto *physical = staffAt(staves, anchor, note.staff, false);
        if (!physical || physical->systemIndex != selected.systemIndex ||
            !measureAt(project, page, note.startTick))
            continue;
        const bool same = note.staff == selected.staff;
        const double x = anchor.x() - point.x();
        const double y = anchor.y() - point.y();
        const double candidate = x * x + y * y;
        if (nearest < 0 || (same && !sameStaff) || (same == sameStaff && candidate < distance))
        {
            nearest = static_cast<int>(index);
            sameStaff = same;
            distance = candidate;
        }
    }
    return nearest;
}

double yForOffset(const StaffImageLines &staff, int offset)
{
    const auto &lines = staff.linePositions;
    if (offset < 0)
        return lines.back() - offset * (lines[4] - lines[3]) / 2;
    if (offset > 8)
        return lines.front() - (offset - 8) * (lines[1] - lines[0]) / 2;
    const auto lower = std::size_t(4 - offset / 2);
    return offset % 2 == 0 ? lines[lower] : (lines[lower] + lines[lower - 1]) / 2;
}

int offsetForPosition(const StaffImageLines &staff, double y)
{
    const auto &lines = staff.linePositions;
    if (y >= lines.back())
        return static_cast<int>(std::lround((lines.back() - y) * 2 / (lines[4] - lines[3])));
    if (y <= lines.front())
        return 8 + static_cast<int>(std::lround((lines.front() - y) * 2 / (lines[1] - lines[0])));
    for (std::size_t lower = 1; lower < lines.size(); ++lower)
        if (y <= lines[lower])
            return static_cast<int>((4 - lower) * 2) +
                   static_cast<int>(std::lround((lines[lower] - y) * 2 / (lines[lower] - lines[lower - 1])));
    return 0;
}

std::optional<StaffPositionHint> positionHint(const Project &project, const QImage &image,
                                              const StaffImageLines &staff, StaffPerformanceNote context,
                                              QPointF position, int nearest, bool locked)
{
    if (!inImage(image, position) || !contains(staff, position, !locked) || context.durationTicks <= 0)
        return {};
    const auto measure = measureAt(project, context.pageIndex, context.startTick);
    const auto bottom = bottomLine(project, staff.staff, context.startTick);
    if (!measure || !bottom)
        return {};
    const int offset = offsetForPosition(staff, position.y());
    const auto alter = currentAlter(project, context, *bottom + offset);
    if (!alter)
        return {};
    const auto spelling = spellingAt(*bottom + offset, *alter);
    const auto midi = writtenMidi(spelling);
    if (!midi)
        return {};
    const double width = context.source.width;
    const double height = context.source.height;
    const double snappedY = yForOffset(staff, offset);
    SourceRect box{position.x() - width / 2, snappedY - height / 2, width, height};
    if (box.x < 0 || box.y < 0 || box.x + box.width > image.width() || box.y + box.height > image.height())
        return {};
    context.staff = staff.staff;
    context.midiPitch = *midi;
    context.staffSpelling = spelling;
    context.source = box;
    context.hasImageAnchor = true;
    StaffPositionHint hint;
    hint.note = std::move(context);
    hint.nearestNoteIndex = nearest;
    hint.measureIndex = static_cast<int>(*measure);
    hint.system = staff.systemIndex;
    hint.lineSpacing = staff.spacing;
    hint.staffLines = staff.linePositions;
    hint.explanation =
        locked ? "Measured staff position; the existing event's staff and timing are retained."
               : "Measured staff position; timing and voice are nearby-note suggestions requiring review.";
    return hint;
}
} // namespace

std::optional<StaffPositionHint> suggestStaffNoteAt(const Project &project, int page, QPointF position,
                                                    std::optional<int> sameBeatReference)
{
    const auto *image = sourceImage(project, page);
    if (!image || !inImage(*image, position))
        return {};
    const auto staves = crispStaffImageLines(*image, project.staffPerformance->staffCount);
    const auto *staff = staffAt(staves, position);
    if (!staff)
        return {};
    if (sameBeatReference &&
        (*sameBeatReference < 0 || std::size_t(*sameBeatReference) >= project.staffPerformance->notes.size() ||
         project.staffPerformance->notes[std::size_t(*sameBeatReference)].pageIndex != page ||
         project.staffPerformance->notes[std::size_t(*sameBeatReference)].staff != staff->staff))
        return {};
    const int nearest = nearestNote(project, page, *image, staves, *staff, position);
    if (nearest < 0)
        return {};
    const auto timing = suggestStaffTimingAt(project, page, *staff, staves, position, nearest, sameBeatReference);
    const int reference = timing ? timing->referenceIndex : sameBeatReference.value_or(nearest);
    auto context = project.staffPerformance->notes[std::size_t(nearest)];
    if (sameBeatReference ||
        (timing && project.staffPerformance->notes[std::size_t(reference)].staff == staff->staff &&
         project.staffPerformance->notes[std::size_t(reference)].voice == context.voice))
        context = project.staffPerformance->notes[std::size_t(reference)];
    if (context.staff != staff->staff)
        context.staffSpelling.reset();
    context.staff = staff->staff;
    context.sourceNoteIndex = -1;
    context.tieStart = context.tieStop = context.unresolvedSoundTie = false;
    if (timing)
    {
        context.startTick = timing->startTick;
        context.durationTicks = timing->durationTicks;
        position.setX(timing->alignedX);
    }
    auto hint = positionHint(project, *image, *staff, std::move(context), position, nearest, false);
    if (hint && timing)
    {
        hint->timingSuggested = true;
        hint->sameBeat = timing->sameBeat;
        hint->timingReferenceIndex = timing->referenceIndex;
        hint->explanation = timing->explanation;
    }
    return hint;
}

std::optional<StaffPositionHint> suggestStaffNotePosition(const Project &project,
                                                          const StaffPerformanceNote &existing, QPointF position)
{
    const auto *image = sourceImage(project, existing.pageIndex);
    if (!image || !validAnchor(*image, existing) || !measureAt(project, existing.pageIndex, existing.startTick))
        return {};
    const auto staves = crispStaffImageLines(*image, project.staffPerformance->staffCount);
    const auto *staff = staffAt(staves, center(existing.source), existing.staff, false);
    if (!staff)
        return {};
    int selected = -1;
    for (std::size_t index = 0; index < project.staffPerformance->notes.size(); ++index)
    {
        const auto &note = project.staffPerformance->notes[index];
        if (&note == &existing)
        {
            selected = static_cast<int>(index);
            break;
        }
    }
    return positionHint(project, *image, *staff, existing, position, selected, true);
}

std::optional<StaffSpelling> staffSpellingForPitch(const Project &project, const StaffPerformanceNote &existing,
                                                   int midi)
{
    if (!project.staffPerformance || existing.staff < 1 || existing.staff > project.staffPerformance->staffCount ||
        project.staffKeyFifths < -7 || project.staffKeyFifths > 7 || midi < 0 || midi > 127 ||
        !measureAt(project, existing.pageIndex, existing.startTick))
        return {};
    if (existing.staffSpelling && writtenMidi(*existing.staffSpelling) == midi)
        return existing.staffSpelling;
    std::optional<StaffSpelling> selected;
    int best = std::numeric_limits<int>::max();
    const int nominalOctave = midi / 12 - 1;
    for (int octave = std::max(-1, nominalOctave - 1); octave <= std::min(9, nominalOctave + 1); ++octave)
        for (std::size_t step = 0; step < PitchLetters.size(); ++step)
        {
            const int alter = midi - (octave + 1) * 12 - NaturalPitches[step];
            if (alter < -2 || alter > 2)
                continue;
            const auto expected = currentAlter(project, existing, octave * 7 + static_cast<int>(step));
            if (!expected)
                continue;
            const bool opposite = project.staffKeyFifths < 0 ? alter > 0 : alter < 0;
            const int score = (alter == *expected ? 0 : 20) + std::abs(alter) * 3 + (opposite ? 1 : 0);
            if (score < best)
            {
                best = score;
                selected = StaffSpelling{PitchLetters[step], alter, octave};
            }
        }
    return selected;
}

std::optional<SourceRect> staffAnchorForPitch(const Project &project, const StaffPerformanceNote &existing,
                                              int newMidi, std::optional<StaffSpelling> spelling)
{
    const auto *image = sourceImage(project, existing.pageIndex);
    if (!image || !validAnchor(*image, existing) || !measureAt(project, existing.pageIndex, existing.startTick))
        return {};
    if (!spelling)
        spelling = staffSpellingForPitch(project, existing, newMidi);
    if (!spelling || writtenMidi(*spelling) != newMidi)
        return {};
    const auto staves = crispStaffImageLines(*image, project.staffPerformance->staffCount);
    const auto *staff = staffAt(staves, center(existing.source), existing.staff, false);
    const auto bottom = bottomLine(project, existing.staff, existing.startTick);
    const auto diatonic = writtenIndex(*spelling);
    if (!staff || !bottom || !diatonic)
        return {};
    auto box = existing.source;
    const double y = yForOffset(*staff, *diatonic - *bottom);
    box.y = y - box.height / 2;
    if (!contains(*staff, {box.x + box.width / 2, y}, false) || box.y < 0 || box.y + box.height > image->height())
        return {};
    return box;
}
} // namespace singlilt

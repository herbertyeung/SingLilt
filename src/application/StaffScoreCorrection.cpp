// Full-voice score corrections and shared timeline updates.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffScoreCorrection.h"
#include "domain/Timeline.h"
#include "i18n/LanguageManager.h"
#include "ui/StaffRenderer.h"
#include <QDateTime>
#include <QJsonArray>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
constexpr std::int64_t MaximumTicks = 1000000000;
constexpr std::int64_t MaximumMeasureTicks = std::int64_t(MaximumBeatsPerBar) * TicksPerQuarter * 4;

void requireCorrection(bool valid, const char *key)
{
    if (!valid)
        throw std::runtime_error(trText(key).toStdString());
}

std::size_t measureAt(const std::vector<WrittenMeasure> &measures, std::int64_t tick)
{
    const auto after = std::upper_bound(measures.begin(), measures.end(), tick,
                                        [](std::int64_t position, const WrittenMeasure &measure)
                                        { return position < measure.startTick; });
    requireCorrection(after != measures.begin(), "ui.staff_correction.invalid_position");
    const auto index = std::size_t(std::distance(measures.begin(), after) - 1);
    requireCorrection(tick < measures[index].startTick + measures[index].durationTicks,
                      "ui.staff_correction.invalid_position");
    return index;
}

Note guideNote(const StaffPerformanceNote *event, const Score &oldScore, std::int64_t duration,
               const WrittenMeasure &measure, int id, int pitchShift, bool preserveImageAnchors)
{
    Note note;
    if (event && event->sourceNoteIndex >= 0 && event->sourceNoteIndex < int(oldScore.notes.size()))
        note = oldScore.notes[std::size_t(event->sourceNoteIndex)];
    note.id = id;
    note.measure = measure.number;
    note.pageIndex = measure.pageIndex;
    note.durationTicks = int(duration);
    note.source = preserveImageAnchors && event ? event->source : SourceRect{};
    note.hasImageAnchor = !preserveImageAnchors || (event && event->hasImageAnchor);
    if (!preserveImageAnchors)
        note.line = 0;
    note.keyOverride = -1;
    note.tieToNext = false;
    if (!event)
    {
        note.degree = 0;
        note.octave = 0;
        note.accidental = 0;
        note.staffSpelling.reset();
        return note;
    }
    const int pitch = event->midiPitch + pitchShift;
    const int relative = pitch - 60 - oldScore.tonic;
    note.octave = int(std::floor(relative / 12.0));
    const int pitchClass = (relative % 12 + 12) % 12;
    static constexpr std::array<int, 7> scale{0, 2, 4, 5, 7, 9, 11};
    note.degree = 1;
    for (int degree = 1; degree <= 7; ++degree)
        if (scale[std::size_t(degree - 1)] <= pitchClass)
            note.degree = degree;
    note.accidental = pitchClass - scale[std::size_t(note.degree - 1)];
    note.staffSpelling = pitchShift == 0 ? event->staffSpelling : std::nullopt;
    return note;
}

struct CorrectedRepeat
{
    std::int64_t firstTick = 0;
    std::int64_t endTick = 0;
    int count = 2;
    std::int64_t firstEndingTick = -1;
};

std::int64_t retimedTick(const std::vector<WrittenMeasure> &oldMeasures,
                         const std::vector<WrittenMeasure> &newMeasures, std::int64_t tick)
{
    const auto oldEnd = oldMeasures.back().startTick + oldMeasures.back().durationTicks;
    if (tick == oldEnd)
        return newMeasures.back().startTick + newMeasures.back().durationTicks;
    const auto index = measureAt(oldMeasures, tick);
    const auto offset = tick - oldMeasures[index].startTick;
    requireCorrection(offset < newMeasures[index].durationTicks, "ui.staff_correction.repeat_bound");
    return newMeasures[index].startTick + offset;
}

Score projectGuide(const Score &original, StaffPerformance &performance,
                   const std::vector<WrittenMeasure> &measures, const std::vector<CorrectedRepeat> &repeats,
                   QJsonArray &retainedLyrics, bool preserveImageAnchors)
{
    struct Boundary
    {
        std::int64_t tick;
        std::size_t note;
        bool starts;
    };
    std::vector<Boundary> boundaries;
    std::vector<std::int64_t> positions;
    for (std::size_t i = 0; i < performance.notes.size(); ++i)
    {
        const auto &note = performance.notes[i];
        if (note.staff != performance.primaryStaff)
            continue;
        boundaries.push_back({note.startTick, i, true});
        boundaries.push_back({note.startTick + note.durationTicks, i, false});
        positions.push_back(note.startTick);
        positions.push_back(note.startTick + note.durationTicks);
    }
    for (const auto &measure : measures)
    {
        positions.push_back(measure.startTick);
        positions.push_back(measure.startTick + measure.durationTicks);
    }
    std::set<std::int64_t> repeatBoundaries;
    for (const auto &repeat : repeats)
    {
        repeatBoundaries.insert(repeat.firstTick);
        repeatBoundaries.insert(repeat.endTick);
        if (repeat.firstEndingTick >= 0)
            repeatBoundaries.insert(repeat.firstEndingTick);
    }
    positions.insert(positions.end(), repeatBoundaries.begin(), repeatBoundaries.end());
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    std::stable_sort(boundaries.begin(), boundaries.end(),
                     [](const Boundary &left, const Boundary &right) { return left.tick < right.tick; });
    Score score = original;
    score.notes.clear();
    score.repeats.clear();
    score.writtenMeasures = measures;
    score.beatsPerBar = measures.front().beatsPerBar;
    score.beatUnit = measures.front().beatUnit;
    std::vector<int> origins;
    std::set<std::pair<int, std::size_t>> active;
    std::size_t boundaryIndex = 0;
    std::size_t lastMeasureIndex = 0;
    std::vector<bool> copiedLyrics(original.notes.size(), false);
    for (std::size_t i = 0; i + 1 < positions.size(); ++i)
    {
        const auto start = positions[i];
        const auto end = positions[i + 1];
        while (boundaryIndex < boundaries.size() && boundaries[boundaryIndex].tick <= start)
        {
            const auto &boundary = boundaries[boundaryIndex++];
            const auto key = std::make_pair(performance.notes[boundary.note].midiPitch, boundary.note);
            if (boundary.starts)
                active.insert(key);
            else
                active.erase(key);
        }
        const int origin = active.empty() ? -1 : int(active.rbegin()->second);
        const auto measureIndex = measureAt(measures, start);
        const auto &measure = measures[measureIndex];
        if (!score.notes.empty() && origins.back() == origin && lastMeasureIndex == measureIndex &&
            !repeatBoundaries.contains(start))
        {
            score.notes.back().durationTicks += int(end - start);
            continue;
        }
        const auto *event = origin >= 0 ? &performance.notes[std::size_t(origin)] : nullptr;
        auto note = guideNote(event, original, end - start, measure, int(score.notes.size()),
                              score.tonic - performance.sourceTonic, preserveImageAnchors);
        if (event && event->sourceNoteIndex >= 0 && event->sourceNoteIndex < int(copiedLyrics.size()))
        {
            if (copiedLyrics[std::size_t(event->sourceNoteIndex)])
            {
                note.lyric.clear();
                note.verseLyrics.clear();
            }
            copiedLyrics[std::size_t(event->sourceNoteIndex)] = true;
        }
        score.notes.push_back(std::move(note));
        origins.push_back(origin);
        lastMeasureIndex = measureIndex;
    }
    requireCorrection(!score.notes.empty() && score.notes.size() <= MaximumScoreNotes,
                      "ui.staff_correction.note_limit");
    for (std::size_t i = 1; i < score.notes.size(); ++i)
    {
        if (origins[i - 1] < 0 || origins[i] < 0)
            continue;
        const auto &previous = performance.notes[std::size_t(origins[i - 1])];
        const auto &current = performance.notes[std::size_t(origins[i])];
        if (!previous.unresolvedSoundTie && !current.unresolvedSoundTie &&
            previous.midiPitch == current.midiPitch &&
            (origins[i - 1] == origins[i] || (previous.tieStart && current.tieStop &&
                                              previous.startTick + previous.durationTicks == current.startTick)))
            score.notes[i - 1].tieToNext = true;
    }
    for (auto &note : performance.notes)
        note.sourceNoteIndex = -1;
    for (std::size_t i = 0; i < origins.size(); ++i)
        if (origins[i] >= 0 && performance.notes[std::size_t(origins[i])].sourceNoteIndex < 0)
            performance.notes[std::size_t(origins[i])].sourceNoteIndex = int(i);
    for (std::size_t i = 0; i < original.notes.size(); ++i)
    {
        const auto &note = original.notes[i];
        const auto originalVerses = lyricVerses(note);
        if (copiedLyrics[i] || std::all_of(originalVerses.begin(), originalVerses.end(),
                                           [](const std::string &text) { return text.empty(); }))
            continue;
        QJsonArray verses;
        for (const auto &text : originalVerses)
            verses.append(QString::fromStdString(text));
        retainedLyrics.append(QJsonObject{{"originalGuideIndex", int(i)}, {"verses", verses}});
    }
    std::vector<std::int64_t> starts{0};
    for (const auto &note : score.notes)
        starts.push_back(starts.back() + note.durationTicks);
    const auto indexAt = [&](std::int64_t tick)
    {
        const auto found = std::lower_bound(starts.begin(), starts.end(), tick);
        requireCorrection(found != starts.end() && *found == tick, "ui.staff_correction.repeat_bound");
        return std::size_t(std::distance(starts.begin(), found));
    };
    for (const auto &repeat : repeats)
        score.repeats.push_back({indexAt(repeat.firstTick), indexAt(repeat.endTick), repeat.count,
                                 repeat.firstEndingTick < 0 ? -1 : int(indexAt(repeat.firstEndingTick))});
    return score;
}
} // namespace

std::int64_t staffCorrectionTicks(double quarterBeats)
{
    const double scaled = quarterBeats * TicksPerQuarter;
    requireCorrection(std::isfinite(quarterBeats) && quarterBeats >= 0 && std::isfinite(scaled) &&
                          scaled <= MaximumTicks && std::abs(scaled - std::round(scaled)) <= 0.000000001,
                      "ui.staff_correction.invalid_beats");
    return std::int64_t(std::llround(scaled));
}

Project correctedStaffProject(const Project &original, std::vector<StaffPerformanceNote> notes,
                              std::vector<WrittenMeasure> measures)
{
    requireCorrection(original.processing.value("local").toBool() && original.staffPerformance &&
                          (original.generatedNotation || original.staffImagePlayback) &&
                          !original.score.writtenMeasures.empty(),
                      "ui.staff_correction.local_only");
    requireCorrection(!notes.empty() && notes.size() <= 100000 &&
                          measures.size() == original.score.writtenMeasures.size(),
                      "ui.staff_correction.note_limit");
    std::int64_t tick = 0;
    for (std::size_t i = 0; i < measures.size(); ++i)
    {
        auto &measure = measures[i];
        requireCorrection(measure.durationTicks > 0 && measure.durationTicks <= MaximumMeasureTicks &&
                              ticksPerBar(measure) > 0 && measure.number >= -1 && measure.number <= 1000000 &&
                              measure.pageIndex == original.score.writtenMeasures[i].pageIndex &&
                              measure.durationTicks <= MaximumTicks - tick,
                          "ui.staff_correction.invalid_measure");
        measure.startTick = tick;
        tick += measure.durationTicks;
    }
    auto performance = *original.staffPerformance;
    std::vector<CorrectedRepeat> repeats;
    std::vector<std::int64_t> oldStarts{0};
    for (const auto &note : original.score.notes)
        oldStarts.push_back(oldStarts.back() + note.durationTicks);
    for (const auto &repeat : original.score.repeats)
    {
        requireCorrection(repeat.firstNote < repeat.endNote && repeat.endNote < oldStarts.size() &&
                              repeat.firstEndingNote < int(original.score.notes.size()),
                          "ui.staff_correction.repeat_bound");
        repeats.push_back(
            {retimedTick(original.score.writtenMeasures, measures, oldStarts[repeat.firstNote]),
             retimedTick(original.score.writtenMeasures, measures, oldStarts[repeat.endNote]), repeat.count,
             repeat.firstEndingNote < 0 ? -1
                                        : retimedTick(original.score.writtenMeasures, measures,
                                                      oldStarts[std::size_t(repeat.firstEndingNote)])});
    }
    for (auto &note : notes)
    {
        requireCorrection(note.midiPitch >= 0 && note.midiPitch <= 127 && note.staff >= 1 &&
                              note.staff <= performance.staffCount && !note.voice.empty() &&
                              note.voice.size() <= 256 && note.startTick >= 0 && note.startTick < tick &&
                              note.durationTicks > 0,
                          "ui.staff_correction.invalid_note");
        const auto &measure = measures[measureAt(measures, note.startTick)];
        requireCorrection(note.durationTicks <= measure.startTick + measure.durationTicks - note.startTick,
                          "ui.staff_correction.bar_bound");
        if (!original.staffImagePlayback)
        {
            note.source = {};
            note.hasImageAnchor = true;
        }
        else if (note.pageIndex != measure.pageIndex || !note.hasImageAnchor ||
                 (note.source.x == 0 && note.source.y == 0 && note.source.width == 0 && note.source.height == 0))
        {
            note.source = {};
            note.hasImageAnchor = false;
        }
        note.pageIndex = measure.pageIndex;
        if (note.staffSpelling)
        {
            const auto &spelling = *note.staffSpelling;
            constexpr std::array<int, 7> pitches{0, 2, 4, 5, 7, 9, 11};
            const auto step = std::string("CDEFGAB").find(spelling.step);
            if (step >= pitches.size() ||
                12 * (spelling.octave + 1) + pitches[step] + spelling.alter != note.midiPitch)
                note.staffSpelling.reset();
        }
    }
    std::stable_sort(notes.begin(), notes.end(),
                     [](const StaffPerformanceNote &left, const StaffPerformanceNote &right)
                     { return left.startTick < right.startTick; });
    for (auto &change : performance.clefChanges)
    {
        const auto index = measureAt(original.score.writtenMeasures, change.startTick);
        const auto offset = change.startTick - original.score.writtenMeasures[index].startTick;
        requireCorrection(offset < measures[index].durationTicks, "ui.staff_correction.clef_bound");
        change.startTick = measures[index].startTick + offset;
    }
    performance.notes = std::move(notes);
    performance.durationTicks = tick;
    QJsonArray retainedLyrics;
    Project corrected = original;
    // Full correction reorders note indexes; its own record replaces indexed anchor provenance.
    corrected.processing.remove("manualImageAnchors");
    corrected.score =
        projectGuide(original.score, performance, measures, repeats, retainedLyrics, original.staffImagePlayback);
    performance.timingFingerprint = staffTimingFingerprint(corrected.score);
    const auto timeline = buildTimeline(corrected.score);
    requireCorrection(timeline.valid() &&
                          buildStaffPerformancePlan(corrected.score, timeline, performance).valid(),
                      "ui.staff_correction.invalid_performance");
    if (!corrected.staffImagePlayback)
    {
        const auto images =
            renderGrandStaffPages(corrected.score, performance,
                                  {corrected.staffBassClef, corrected.staffKeyFifths, corrected.staffMinor});
        corrected.image = images.front();
        if (!corrected.staffPages.empty())
        {
            requireCorrection(images.size() == corrected.staffPages.size(), "ui.staff_correction.invalid_measure");
            for (std::size_t page = 0; page < corrected.staffPages.size(); ++page)
                corrected.staffPages[page].renderedImage = images[page];
        }
    }
    corrected.staffPerformance = std::move(performance);
    if (!corrected.staffPages.empty())
    {
        for (std::size_t page = 0; page < corrected.staffPages.size(); ++page)
        {
            auto &stored = corrected.staffPages[page];
            bool first = true;
            for (const auto &measure : measures)
                if (measure.pageIndex == int(page))
                {
                    if (first)
                        stored.startTick = measure.startTick;
                    stored.endTick = measure.startTick + measure.durationTicks;
                    first = false;
                }
        }
    }
    requireCorrection(!corrected.audioSource && !corrected.accompaniment, "ui.staff_correction.media_binding");
    if (corrected.practiceSettings)
    {
        corrected.practiceSettings->loopStart =
            std::min(corrected.practiceSettings->loopStart, timeline.durationTicks);
        corrected.practiceSettings->loopEnd =
            std::min(corrected.practiceSettings->loopEnd, timeline.durationTicks);
        if (corrected.practiceSettings->loopEnd <= corrected.practiceSettings->loopStart)
            corrected.practiceSettings->loopEnabled = false;
    }
    QJsonObject record{{"method", "explicit-user-note-and-measure-edit"},
                       {"guideProjection", "highest-primary-staff"},
                       {"editedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                       {"originalNoteCount", int(original.staffPerformance->notes.size())},
                       {"correctedNoteCount", int(corrected.staffPerformance->notes.size())},
                       {"originalDurationTicks", double(original.staffPerformance->durationTicks)},
                       {"correctedDurationTicks", double(tick)},
                       {"originalWarnings", QJsonArray::fromStringList(original.warnings)},
                       {"retainedLyrics", retainedLyrics},
                       {"accuracyMeasured", false}};
    corrected.processing.insert("manualStaffCorrection", record);
    corrected.warnings = {QStringLiteral("ui.staff_correction.manual_notice")};
    for (const auto &warning : original.warnings)
        if (warning.contains("90 BPM practice default"))
            corrected.warnings.append(warning);
    std::set<int> writtenNumbers;
    for (const auto &measure : measures)
    {
        if (measure.durationTicks > ticksPerBar(measure))
            corrected.warnings.append(trText("ui.staff_correction.overfull")
                                          .arg(measure.number + 1)
                                          .arg(double(measure.durationTicks) / TicksPerQuarter)
                                          .arg(double(ticksPerBar(measure)) / TicksPerQuarter));
        if (!writtenNumbers.insert(measure.number).second)
            corrected.warnings.append(trText("ui.staff_correction.duplicate_number").arg(measure.number + 1));
    }
    if (!retainedLyrics.isEmpty())
        corrected.warnings.append("ui.staff_correction.lyrics_changed");
    if (std::any_of(corrected.staffPerformance->notes.begin(), corrected.staffPerformance->notes.end(),
                    [](const StaffPerformanceNote &note) { return note.unresolvedSoundTie; }))
        corrected.warnings.append("ui.staff_correction.unresolved_remaining");
    const auto json = projectToJson(corrected);
    projectFromJson(json, corrected.image, corrected.staffPages);
    return corrected;
}
} // namespace singlilt

// JSON serialization for complete staff performances.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ProjectStore.h"
#include "domain/Timeline.h"
#include "i18n/LanguageManager.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace singlilt
{
namespace
{
void requireStaff(bool valid)
{
    if (!valid)
        throw std::runtime_error(trText("messages.staff.invalid_performance").toStdString());
}
std::int64_t staffInteger(const QJsonObject &object, const char *key, std::int64_t fallback,
                          bool mandatory = false)
{
    if (!object.contains(key))
    {
        requireStaff(!mandatory);
        return fallback;
    }
    const auto value = object.value(key);
    const double number = value.toDouble();
    requireStaff(value.isDouble() && std::isfinite(number) && std::floor(number) == number &&
                 number >= -1000000000 && number <= 1000000000);
    return static_cast<std::int64_t>(number);
}
bool staffBoolean(const QJsonObject &object, const char *key)
{
    requireStaff(!object.contains(key) || object.value(key).isBool());
    return object.value(key).toBool();
}

QString beamKindText(StaffBeamKind kind)
{
    switch (kind)
    {
    case StaffBeamKind::Begin:
        return "begin";
    case StaffBeamKind::Continue:
        return "continue";
    case StaffBeamKind::End:
        return "end";
    case StaffBeamKind::ForwardHook:
        return "forward hook";
    case StaffBeamKind::BackwardHook:
        return "backward hook";
    }
    requireStaff(false);
    return {};
}

StaffBeamKind readBeamKind(const QString &text)
{
    if (text == "begin")
        return StaffBeamKind::Begin;
    if (text == "continue")
        return StaffBeamKind::Continue;
    if (text == "end")
        return StaffBeamKind::End;
    if (text == "forward hook")
        return StaffBeamKind::ForwardHook;
    if (text == "backward hook")
        return StaffBeamKind::BackwardHook;
    requireStaff(false);
    return StaffBeamKind::Begin;
}

QString stemDirectionText(StaffStemDirection direction)
{
    switch (direction)
    {
    case StaffStemDirection::Auto:
        return "auto";
    case StaffStemDirection::Up:
        return "up";
    case StaffStemDirection::Down:
        return "down";
    case StaffStemDirection::None:
        return "none";
    }
    requireStaff(false);
    return {};
}

StaffStemDirection readStemDirection(const QString &text)
{
    if (text == "auto")
        return StaffStemDirection::Auto;
    if (text == "up")
        return StaffStemDirection::Up;
    if (text == "down")
        return StaffStemDirection::Down;
    if (text == "none")
        return StaffStemDirection::None;
    requireStaff(false);
    return StaffStemDirection::Auto;
}
} // namespace

QJsonObject staffPerformanceToJson(const StaffPerformance &performance)
{
    QJsonArray notes;
    std::set<std::pair<int, std::string>> voices;
    for (const auto &note : performance.notes)
    {
        voices.emplace(note.staff, note.voice);
        QJsonObject object{
            {"startTick", double(note.startTick)},
            {"durationTicks", double(note.durationTicks)},
            {"midiPitch", note.midiPitch},
            {"staff", note.staff},
            {"voice", QString::fromStdString(note.voice)},
            {"velocity", note.velocity},
            {"sourceNoteIndex", note.sourceNoteIndex},
            {"tieStart", note.tieStart},
            {"tieStop", note.tieStop},
            {"pageIndex", note.pageIndex},
            {"hasImageAnchor", note.hasImageAnchor},
            {"unresolvedSoundTie", note.unresolvedSoundTie},
            {"bbox", QJsonArray{note.source.x, note.source.y, note.source.width, note.source.height}}};
        if (note.staffSpelling)
            object.insert("staffSpelling", QJsonObject{{"step", QString(QChar(note.staffSpelling->step))},
                                                       {"alter", note.staffSpelling->alter},
                                                       {"octave", note.staffSpelling->octave}});
        requireStaff(note.beams.size() <= 8);
        if (!note.beams.empty())
        {
            QJsonArray beams;
            std::set<int> levels;
            for (const auto &beam : note.beams)
            {
                requireStaff(beam.level >= 1 && beam.level <= 8 && levels.insert(beam.level).second);
                beams.append(QJsonObject{{"level", beam.level}, {"kind", beamKindText(beam.kind)}});
            }
            object.insert("beams", beams);
        }
        const auto stemText = stemDirectionText(note.stemDirection);
        if (note.stemDirection != StaffStemDirection::Auto)
            object.insert("stemDirection", stemText);
        notes.append(object);
    }
    QJsonArray clefChanges;
    for (const auto &change : performance.clefChanges)
        clefChanges.append(QJsonObject{
            {"startTick", double(change.startTick)}, {"staff", change.staff}, {"bassClef", change.bassClef}});
    return {{"staffCount", performance.staffCount},
            {"clefChanges", clefChanges},
            {"primaryProgram", performance.primaryProgram},
            {"otherProgram", performance.otherProgram},
            {"primaryStaff", performance.primaryStaff},
            {"sourceTonic", performance.sourceTonic},
            {"durationTicks", double(performance.durationTicks)},
            {"timingFingerprint", QString::fromStdString(performance.timingFingerprint)},
            {"voiceCount", int(voices.size())},
            {"noteCount", int(notes.size())},
            {"notes", notes}};
}

StaffPerformance staffPerformanceFromJson(const QJsonObject &json, const Score &score)
{
    StaffPerformance result;
    std::int64_t sourceDuration = 0;
    for (const auto &note : score.notes)
        sourceDuration += note.durationTicks;
    result.staffCount = int(staffInteger(json, "staffCount", 2, true));
    result.primaryStaff = int(staffInteger(json, "primaryStaff", 1));
    result.sourceTonic = int(staffInteger(json, "sourceTonic", score.tonic));
    result.primaryProgram = int(staffInteger(json, "primaryProgram", 0));
    result.otherProgram = int(staffInteger(json, "otherProgram", 0));
    requireStaff(result.primaryProgram >= 0 && result.primaryProgram <= 127 && result.otherProgram >= 0 &&
                 result.otherProgram <= 127);
    result.durationTicks = staffInteger(json, "durationTicks", sourceDuration);
    requireStaff(result.staffCount >= 1 && result.staffCount <= 2 && result.primaryStaff >= 1 &&
                 result.primaryStaff <= result.staffCount && result.sourceTonic >= 0 && result.sourceTonic <= 11 &&
                 result.durationTicks > 0 && result.durationTicks <= 1000000000 &&
                 result.durationTicks == sourceDuration && json.value("notes").isArray());
    const auto entries = json.value("notes").toArray();
    requireStaff(!entries.isEmpty() && entries.size() <= 100000);
    std::set<std::pair<int, std::string>> voices;
    for (const auto &entry : entries)
    {
        requireStaff(entry.isObject());
        const auto object = entry.toObject();
        StaffPerformanceNote note;
        note.startTick = staffInteger(object, "startTick", 0, true);
        note.durationTicks = staffInteger(object, "durationTicks", 0, true);
        note.midiPitch = int(staffInteger(object, "midiPitch", -1, true));
        note.staff = int(staffInteger(object, "staff", 1, true));
        requireStaff(object.value("voice").isString());
        note.voice = object.value("voice").toString().toStdString();
        note.velocity = int(staffInteger(object, "velocity", 88));
        note.sourceNoteIndex = int(staffInteger(object, "sourceNoteIndex", -1));
        note.tieStart = staffBoolean(object, "tieStart");
        note.tieStop = staffBoolean(object, "tieStop");
        note.pageIndex = int(staffInteger(object, "pageIndex", 0));
        requireStaff(!object.contains("hasImageAnchor") || object.value("hasImageAnchor").isBool());
        note.hasImageAnchor = object.value("hasImageAnchor").toBool(true);
        note.unresolvedSoundTie = staffBoolean(object, "unresolvedSoundTie");
        if (object.contains("stemDirection"))
        {
            requireStaff(object.value("stemDirection").isString());
            note.stemDirection = readStemDirection(object.value("stemDirection").toString());
        }
        if (object.contains("beams"))
        {
            requireStaff(object.value("beams").isArray());
            const auto beams = object.value("beams").toArray();
            requireStaff(beams.size() <= 8);
            std::set<int> levels;
            for (const auto &beamEntry : beams)
            {
                requireStaff(beamEntry.isObject());
                const auto beam = beamEntry.toObject();
                const int level = int(staffInteger(beam, "level", 1, true));
                requireStaff(level >= 1 && level <= 8 && levels.insert(level).second &&
                             beam.value("kind").isString());
                note.beams.push_back({level, readBeamKind(beam.value("kind").toString())});
            }
        }
        requireStaff(
            note.startTick >= 0 && note.startTick < sourceDuration && note.durationTicks > 0 &&
            note.durationTicks <= sourceDuration - note.startTick && note.midiPitch >= 0 &&
            note.midiPitch <= 127 && note.staff >= 1 && note.staff <= result.staffCount && !note.voice.empty() &&
            note.voice.size() <= 256 && note.velocity >= 1 && note.velocity <= 127 && note.sourceNoteIndex >= -1 &&
            note.sourceNoteIndex < int(score.notes.size()) && note.pageIndex >= 0 && note.pageIndex <= 31);
        const auto box = object.value("bbox").toArray();
        requireStaff(box.size() == 4);
        for (const auto &coordinate : box)
            requireStaff(coordinate.isDouble() && std::isfinite(coordinate.toDouble()) &&
                         coordinate.toDouble() >= 0);
        note.source = {box[0].toDouble(), box[1].toDouble(), box[2].toDouble(), box[3].toDouble()};
        requireStaff(note.hasImageAnchor || (note.source.x == 0 && note.source.y == 0 && note.source.width == 0 &&
                                             note.source.height == 0));
        if (object.contains("staffSpelling"))
        {
            requireStaff(object.value("staffSpelling").isObject());
            const auto spelling = object.value("staffSpelling").toObject();
            const auto step = spelling.value("step").toString();
            const int alter = int(staffInteger(spelling, "alter", 0, true));
            const int octave = int(staffInteger(spelling, "octave", 4, true));
            requireStaff(step.size() == 1 && QStringLiteral("ABCDEFG").contains(step) && alter >= -2 &&
                         alter <= 2 && octave >= -1 && octave <= 9);
            note.staffSpelling = StaffSpelling{step.front().toLatin1(), alter, octave};
        }
        voices.emplace(note.staff, note.voice);
        result.notes.push_back(std::move(note));
    }
    requireStaff(staffInteger(json, "noteCount", entries.size()) == entries.size() &&
                 staffInteger(json, "voiceCount", voices.size()) == std::int64_t(voices.size()));
    if (json.contains("clefChanges"))
    {
        requireStaff(json.value("clefChanges").isArray());
        const auto changes = json.value("clefChanges").toArray();
        requireStaff(changes.size() <= 4096);
        std::set<std::pair<std::int64_t, int>> locations;
        for (const auto &entry : changes)
        {
            requireStaff(entry.isObject());
            const auto object = entry.toObject();
            StaffClefChange change;
            change.startTick = staffInteger(object, "startTick", 0, true);
            change.staff = int(staffInteger(object, "staff", 1, true));
            requireStaff(object.value("bassClef").isBool());
            change.bassClef = object.value("bassClef").toBool();
            requireStaff(change.startTick >= 0 && change.startTick < sourceDuration && change.staff >= 1 &&
                         change.staff <= result.staffCount &&
                         locations.emplace(change.startTick, change.staff).second);
            result.clefChanges.push_back(change);
        }
    }
    result.timingFingerprint = json.contains("timingFingerprint")
                                   ? json.value("timingFingerprint").toString().toStdString()
                                   : staffTimingFingerprint(score);
    requireStaff(result.timingFingerprint == staffTimingFingerprint(score));
    const int pitchShift = score.tonic - result.sourceTonic;
    std::map<int, std::vector<std::pair<std::int64_t, std::int64_t>>> pitchIntervals;
    for (const auto &note : result.notes)
        if (note.staff == result.primaryStaff)
            pitchIntervals[note.midiPitch + pitchShift].emplace_back(note.startTick,
                                                                     note.startTick + note.durationTicks);
    for (auto &entry : pitchIntervals)
    {
        auto &intervals = entry.second;
        std::sort(intervals.begin(), intervals.end());
        // Retain a single note's longest end, not the union of adjacent notes.
        for (std::size_t i = 1; i < intervals.size(); ++i)
            intervals[i].second = std::max(intervals[i].second, intervals[i - 1].second);
    }
    std::int64_t tick = 0;
    int tonic = score.tonic;
    std::size_t nextKey = 0;
    std::size_t comparisons = 0;
    for (const auto &guide : score.notes)
    {
        while (nextKey < score.keyChanges.size() && score.keyChanges[nextKey].startTick <= tick)
            tonic = score.keyChanges[nextKey++].tonic;
        if (guide.keyOverride >= 0)
            tonic = guide.keyOverride;
        if (guide.degree != 0)
        {
            const int pitch = midiPitch(guide, tonic);
            const auto matching = pitchIntervals.find(pitch);
            requireStaff(matching != pitchIntervals.end());
            const auto &intervals = matching->second;
            const auto afterStart = std::upper_bound(intervals.begin(), intervals.end(), tick,
                                                     [&](std::int64_t start, const auto &interval)
                                                     {
                                                         requireStaff(++comparisons <= 20000000);
                                                         return start < interval.first;
                                                     });
            requireStaff(afterStart != intervals.begin() &&
                         std::prev(afterStart)->second >= tick + guide.durationTicks);
        }
        tick += guide.durationTicks;
    }
    requireStaff(buildStaffPerformancePlan(score, buildTimeline(score), result).valid());
    return result;
}
} // namespace singlilt

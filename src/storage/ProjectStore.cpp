// Project data, JSON conversion, and legacy-format loading.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ProjectStore.h"
#include "ProjectPackage.h"
#include "StaffPagesStore.h"
#include "domain/Timeline.h"
#include "i18n/LanguageManager.h"
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}
void require(bool condition, const char *text)
{
    if (!condition)
        throw std::runtime_error(trText(text).toStdString());
}
double number(const QJsonObject &object, const char *key, double fallback, bool mandatory = false)
{
    if (!object.contains(key))
    {
        require(!mandatory, "messages.storage.missing_number");
        return fallback;
    }
    const auto value = object.value(key);
    require(value.isDouble() && std::isfinite(value.toDouble()), "messages.storage.invalid_number");
    return value.toDouble();
}
int integer(const QJsonObject &object, const char *key, int fallback, bool mandatory = false)
{
    double v = number(object, key, fallback, mandatory);
    require(std::floor(v) == v && v >= INT_MIN && v <= INT_MAX, "messages.storage.invalid_integer");
    return int(v);
}
bool boolean(const QJsonObject &object, const char *key, bool fallback,
             const char *errorKey = "messages.storage.invalid_practice_fields")
{
    require(!object.contains(key) || object.value(key).isBool(), errorKey);
    return object.value(key).toBool(fallback);
}
std::int64_t tick(const QJsonObject &object, const char *key)
{
    const double value = number(object, key, 0, true);
    require(value >= 0 && value <= 1000000000 && std::floor(value) == value,
            "messages.storage.invalid_accompaniment_fields");
    return static_cast<std::int64_t>(value);
}
void validateArrangementFields(const Score &score, const AccompanimentArrangement &arrangement)
{
    const auto &settings = arrangement.settings;
    require((settings.pattern == AccompanimentPattern::BlockChords ||
             settings.pattern == AccompanimentPattern::Arpeggio ||
             settings.pattern == AccompanimentPattern::Sparse) &&
                (settings.mode == HarmonicMode::Automatic || settings.mode == HarmonicMode::Major ||
                 settings.mode == HarmonicMode::Minor) &&
                settings.harmonicTonic >= -1 && settings.harmonicTonic <= 11 && settings.chordProgram >= 0 &&
                settings.chordProgram <= 127 && settings.bassProgram >= 0 && settings.bassProgram <= 127 &&
                settings.chordVelocity >= 1 && settings.chordVelocity <= 127 && settings.bassVelocity >= 1 &&
                settings.bassVelocity <= 127 && arrangement.generatorVersion == 1 &&
                !arrangement.melodyFingerprint.empty() && arrangement.melodyFingerprint.size() <= 128 &&
                arrangement.chords.size() <= 100000 && arrangement.name.size() <= 512,
            "messages.storage.invalid_accompaniment_fields");
    std::int64_t sourceDuration = 0;
    for (const auto &note : score.notes)
        sourceDuration += note.durationTicks;
    const bool current = arrangement.melodyFingerprint == accompanimentFingerprint(score);
    std::int64_t previousEnd = 0;
    for (const auto &chord : arrangement.chords)
    {
        require(chord.startTick >= previousEnd && chord.endTick > chord.startTick && chord.endTick <= 1000000000 &&
                    (!current || chord.endTick <= sourceDuration) && chord.rootPitchClass >= 0 &&
                    chord.rootPitchClass <= 11 &&
                    (chord.quality == ChordQuality::Major || chord.quality == ChordQuality::Minor ||
                     chord.quality == ChordQuality::Diminished || chord.quality == ChordQuality::None) &&
                    chord.inversion >= 0 && chord.inversion <= 2 &&
                    (chord.quality != ChordQuality::None || chord.inversion == 0) &&
                    (!chord.patternOverride || *chord.patternOverride == AccompanimentPattern::BlockChords ||
                     *chord.patternOverride == AccompanimentPattern::Arpeggio ||
                     *chord.patternOverride == AccompanimentPattern::Sparse),
                "messages.storage.invalid_accompaniment_fields");
        previousEnd = chord.endTick;
    }
}
QJsonObject arrangementToJson(const AccompanimentArrangement &arrangement)
{
    const auto &s = arrangement.settings;
    QJsonArray chords;
    for (const auto &chord : arrangement.chords)
    {
        QJsonObject object{{"startTick", double(chord.startTick)},   {"endTick", double(chord.endTick)},
                           {"rootPitchClass", chord.rootPitchClass}, {"quality", int(chord.quality)},
                           {"inversion", chord.inversion},           {"userEdited", chord.userEdited}};
        if (chord.patternOverride)
            object.insert("patternOverride", int(*chord.patternOverride));
        chords.append(object);
    }
    return {{"settings", QJsonObject{{"pattern", int(s.pattern)},
                                     {"mode", int(s.mode)},
                                     {"harmonicTonic", s.harmonicTonic},
                                     {"chordProgram", s.chordProgram},
                                     {"bassProgram", s.bassProgram},
                                     {"chordVelocity", s.chordVelocity},
                                     {"bassVelocity", s.bassVelocity}}},
            {"chords", chords},
            {"melodyFingerprint", qs(arrangement.melodyFingerprint)},
            {"generatorVersion", arrangement.generatorVersion},
            {"name", qs(arrangement.name)},
            {"userAuthored", arrangement.userAuthored}};
}
AccompanimentArrangement arrangementFromJson(const QJsonObject &object, const Score &score)
{
    require(object.value("settings").isObject() && object.value("chords").isArray() &&
                object.value("melodyFingerprint").isString(),
            "messages.storage.invalid_accompaniment_fields");
    AccompanimentArrangement arrangement;
    const auto s = object.value("settings").toObject();
    arrangement.settings.pattern = AccompanimentPattern(integer(s, "pattern", 0, true));
    arrangement.settings.mode = HarmonicMode(integer(s, "mode", 0, true));
    arrangement.settings.harmonicTonic = integer(s, "harmonicTonic", -1, true);
    arrangement.settings.chordProgram = integer(s, "chordProgram", 0, true);
    arrangement.settings.bassProgram = integer(s, "bassProgram", 0, true);
    arrangement.settings.chordVelocity = integer(s, "chordVelocity", 62, true);
    arrangement.settings.bassVelocity = integer(s, "bassVelocity", 68, true);
    arrangement.generatorVersion = integer(object, "generatorVersion", 1, true);
    arrangement.melodyFingerprint = object.value("melodyFingerprint").toString().toStdString();
    require(!object.contains("name") || object.value("name").isString(),
            "messages.storage.invalid_accompaniment_fields");
    arrangement.name = object.value("name").toString().toStdString();
    arrangement.userAuthored =
        boolean(object, "userAuthored", false, "messages.storage.invalid_accompaniment_fields");
    const auto chords = object.value("chords").toArray();
    require(chords.size() <= 100000, "messages.storage.invalid_accompaniment_fields");
    for (const auto &entry : chords)
    {
        require(entry.isObject(), "messages.storage.invalid_accompaniment_fields");
        const auto chord = entry.toObject();
        ChordSpan span{tick(chord, "startTick"),
                       tick(chord, "endTick"),
                       integer(chord, "rootPitchClass", 0, true),
                       ChordQuality(integer(chord, "quality", 0, true)),
                       integer(chord, "inversion", 0, true),
                       boolean(chord, "userEdited", false, "messages.storage.invalid_accompaniment_fields")};
        if (chord.contains("patternOverride"))
            span.patternOverride = AccompanimentPattern(integer(chord, "patternOverride", 0, true));
        arrangement.chords.push_back(span);
    }
    validateArrangementFields(score, arrangement);
    return arrangement;
}
void validatePracticeMix(const PracticeMix &mix)
{
    require(std::isfinite(mix.melodyVolume) && mix.melodyVolume >= 0 && mix.melodyVolume <= 1 &&
                std::isfinite(mix.accompanimentVolume) && mix.accompanimentVolume >= 0 &&
                mix.accompanimentVolume <= 1,
            "messages.storage.invalid_practice_fields");
}
void validateAudioSource(const Score &score, const AudioSourceInfo &source)
{
    require(!source.path.empty() && source.path.size() <= 32768 && std::isfinite(source.durationSeconds) &&
                source.durationSeconds > 0 && source.durationSeconds <= 1200 &&
                std::isfinite(source.selectedStartSeconds) && std::isfinite(source.selectedEndSeconds) &&
                source.selectedStartSeconds >= 0 && source.selectedEndSeconds > source.selectedStartSeconds &&
                source.selectedEndSeconds <= source.durationSeconds &&
                source.selectedEndSeconds - source.selectedStartSeconds <= 1200.0 + 1e-9 &&
                !source.timingFingerprint.empty() && source.timingFingerprint.size() <= 128 &&
                source.timings.size() <= 100000 && source.lyricTimings.size() <= 100000,
            "messages.storage.invalid_audio_source");
    require(source.vocalsPath.size() <= 32768 && source.instrumentalPath.size() <= 32768 &&
                source.separationModel.size() <= 512 &&
                (!source.vocalsSeparated || (!source.vocalsPath.empty() && !source.instrumentalPath.empty() &&
                                             !source.separationModel.empty())),
            "messages.storage.invalid_separation_fields");
    const bool current = source.timingFingerprint == audioTimingFingerprint(score);
    std::int64_t scoreDuration = 0;
    for (const auto &note : score.notes)
        scoreDuration += note.durationTicks;
    double previousTime = source.selectedStartSeconds;
    std::int64_t previousTick = 0;
    int previousIndex = -1;
    for (const auto &timing : source.timings)
    {
        require(timing.sourceNoteIndex >= 0 && timing.sourceNoteIndex < static_cast<int>(MaximumScoreNotes) &&
                    (!current || timing.sourceNoteIndex < int(score.notes.size())) &&
                    timing.sourceNoteIndex > previousIndex && std::isfinite(timing.startSeconds) &&
                    std::isfinite(timing.endSeconds) && timing.startSeconds >= previousTime &&
                    timing.endSeconds > timing.startSeconds && timing.endSeconds <= source.selectedEndSeconds &&
                    timing.startTick >= previousTick && timing.endTick > timing.startTick &&
                    timing.endTick <= 1000000000 && (!current || timing.endTick <= scoreDuration),
                "messages.storage.invalid_audio_source");
        previousTime = timing.endSeconds;
        previousTick = timing.endTick;
        previousIndex = timing.sourceNoteIndex;
    }
    double previousLyricStart = source.selectedStartSeconds, previousLyricEnd = source.selectedStartSeconds;
    size_t textBytes = 0;
    for (const auto &lyric : source.lyricTimings)
    {
        textBytes += lyric.text.size();
        require(!lyric.text.empty() && lyric.text.size() <= 4096 && textBytes <= 1024 * 1024 &&
                    std::isfinite(lyric.startSeconds) && std::isfinite(lyric.endSeconds) &&
                    lyric.startSeconds >= previousLyricStart && lyric.endSeconds >= previousLyricEnd &&
                    lyric.endSeconds > lyric.startSeconds && lyric.endSeconds <= source.selectedEndSeconds &&
                    lyric.sourceNoteIndex >= -1 && lyric.sourceNoteIndex < static_cast<int>(MaximumScoreNotes) &&
                    (!current || lyric.sourceNoteIndex < int(score.notes.size())),
                "messages.storage.invalid_audio_source");
        previousLyricStart = lyric.startSeconds;
        previousLyricEnd = lyric.endSeconds;
    }
}
QJsonObject audioSourceToJson(const AudioSourceInfo &source)
{
    QJsonArray timings, lyrics;
    for (const auto &timing : source.timings)
        timings.append(QJsonObject{{"sourceNoteIndex", timing.sourceNoteIndex},
                                   {"startSeconds", timing.startSeconds},
                                   {"endSeconds", timing.endSeconds},
                                   {"startTick", double(timing.startTick)},
                                   {"endTick", double(timing.endTick)}});
    for (const auto &lyric : source.lyricTimings)
        lyrics.append(QJsonObject{{"text", qs(lyric.text)},
                                  {"startSeconds", lyric.startSeconds},
                                  {"endSeconds", lyric.endSeconds},
                                  {"sourceNoteIndex", lyric.sourceNoteIndex}});
    return {{"path", qs(source.path)},
            {"durationSeconds", source.durationSeconds},
            {"selectedStartSeconds", source.selectedStartSeconds},
            {"selectedEndSeconds", source.selectedEndSeconds},
            {"timings", timings},
            {"lyricTimings", lyrics},
            {"timingFingerprint", qs(source.timingFingerprint)},
            {"vocalsPath", qs(source.vocalsPath)},
            {"instrumentalPath", qs(source.instrumentalPath)},
            {"separationModel", qs(source.separationModel)},
            {"vocalsSeparated", source.vocalsSeparated}};
}
AudioSourceInfo audioSourceFromJson(const QJsonObject &object, const Score &score)
{
    require(object.value("path").isString() && object.value("timingFingerprint").isString() &&
                object.value("timings").isArray() &&
                (!object.contains("lyricTimings") || object.value("lyricTimings").isArray()),
            "messages.storage.invalid_audio_source");
    AudioSourceInfo source;
    source.path = object.value("path").toString().toStdString();
    for (const char *key : {"vocalsPath", "instrumentalPath", "separationModel"})
        require(!object.contains(key) || object.value(key).isString(),
                "messages.storage.invalid_separation_fields");
    source.vocalsPath = object.value("vocalsPath").toString().toStdString();
    source.instrumentalPath = object.value("instrumentalPath").toString().toStdString();
    source.separationModel = object.value("separationModel").toString().toStdString();
    source.vocalsSeparated =
        boolean(object, "vocalsSeparated", false, "messages.storage.invalid_separation_fields");
    source.timingFingerprint = object.value("timingFingerprint").toString().toStdString();
    if (source.timingFingerprint == accompanimentFingerprint(score))
        source.timingFingerprint = audioTimingFingerprint(score);
    source.durationSeconds = number(object, "durationSeconds", 0, true);
    source.selectedStartSeconds = number(object, "selectedStartSeconds", 0, true);
    source.selectedEndSeconds = number(object, "selectedEndSeconds", 0, true);
    const auto timings = object.value("timings").toArray();
    const auto lyrics = object.value("lyricTimings").toArray();
    require(timings.size() <= 100000 && lyrics.size() <= 100000, "messages.storage.invalid_audio_source");
    for (const auto &entry : timings)
    {
        require(entry.isObject(), "messages.storage.invalid_audio_source");
        const auto timing = entry.toObject();
        source.timings.push_back({integer(timing, "sourceNoteIndex", -1, true),
                                  number(timing, "startSeconds", 0, true), number(timing, "endSeconds", 0, true),
                                  tick(timing, "startTick"), tick(timing, "endTick")});
    }
    for (const auto &entry : lyrics)
    {
        require(entry.isObject() && entry.toObject().value("text").isString(),
                "messages.storage.invalid_audio_source");
        const auto lyric = entry.toObject();
        source.lyricTimings.push_back(
            {lyric.value("text").toString().toStdString(), number(lyric, "startSeconds", 0, true),
             number(lyric, "endSeconds", 0, true), integer(lyric, "sourceNoteIndex", -1, true)});
    }
    validateAudioSource(score, source);
    return source;
}
} // namespace
QJsonObject scoreToJson(const Score &s)
{
    QJsonArray notes, repeats, programs, writtenMeasures;
    for (const int program : s.versePrograms)
        programs.append(program);
    for (const auto &n : s.notes)
    {
        QJsonArray verses;
        QStringList legacyLines;
        for (const auto &text : lyricVerses(n))
        {
            verses.append(qs(text));
            legacyLines.append(qs(text));
        }
        QJsonObject object{{"id", n.id},
                           {"degree", n.degree},
                           {"octave", n.octave},
                           {"accidental", n.accidental},
                           {"durationTicks", n.durationTicks},
                           {"measure", n.measure},
                           {"pageIndex", n.pageIndex},
                           {"hasImageAnchor", n.hasImageAnchor},
                           {"line", n.line},
                           {"lyric", legacyLines.join('\n')},
                           {"verseLyrics", verses},
                           {"confidence", n.confidence},
                           {"keyOverride", n.keyOverride},
                           {"tieToNext", n.tieToNext},
                           {"bbox", QJsonArray{n.source.x, n.source.y, n.source.width, n.source.height}}};
        if (n.staffSpelling)
            object.insert("staffSpelling", QJsonObject{{"step", QString(QChar(n.staffSpelling->step))},
                                                       {"alter", n.staffSpelling->alter},
                                                       {"octave", n.staffSpelling->octave}});
        notes.append(object);
    }
    for (const auto &r : s.repeats)
        repeats.append(QJsonObject{{"firstNote", int(r.firstNote)},
                                   {"endNote", int(r.endNote)},
                                   {"count", r.count},
                                   {"firstEndingNote", r.firstEndingNote}});
    QJsonObject result{
        {"title", qs(s.title)},         {"tonic", s.tonic},          {"bpm", s.bpm},
        {"beatsPerBar", s.beatsPerBar}, {"beatUnit", s.beatUnit},    {"notes", notes},
        {"repeats", repeats},           {"versePrograms", programs}, {"baseVelocity", s.baseVelocity},
        {"accentBeats", s.accentBeats}};
    if (!s.writtenMeasures.empty())
    {
        for (const auto &measure : s.writtenMeasures)
            writtenMeasures.append(QJsonObject{{"startTick", double(measure.startTick)},
                                               {"durationTicks", double(measure.durationTicks)},
                                               {"number", measure.number},
                                               {"beatsPerBar", measure.beatsPerBar},
                                               {"beatUnit", measure.beatUnit},
                                               {"pageIndex", measure.pageIndex}});
        result.insert("writtenMeasures", writtenMeasures);
    }
    if (!s.keyChanges.empty())
    {
        QJsonArray changes;
        for (const auto &change : s.keyChanges)
            changes.append(QJsonObject{{"startTick", double(change.startTick)},
                                       {"tonic", change.tonic},
                                       {"sourceNoteIndex", change.sourceNoteIndex}});
        result.insert("keyChanges", changes);
    }
    return result;
}
Score scoreFromJson(const QJsonObject &o)
{
    require(o.value("notes").isArray(), "messages.storage.missing_notes");
    Score s;
    s.title = o.value("title").toString(trText("messages.storage.untitled")).toStdString();
    s.tonic = integer(o, "tonic", 0, true);
    s.bpm = number(o, "bpm", 90, true);
    s.beatsPerBar = integer(o, "beatsPerBar", 4, true);
    s.beatUnit = integer(o, "beatUnit", 4, true);
    s.baseVelocity = integer(o, "baseVelocity", 88);
    require(!o.contains("accentBeats") || o.value("accentBeats").isBool(), "messages.storage.accent_bool");
    s.accentBeats = o.value("accentBeats").toBool(true);
    if (o.contains("versePrograms"))
    {
        require(o.value("versePrograms").isArray(), "messages.storage.program_array");
        const auto programs = o.value("versePrograms").toArray();
        require(!programs.isEmpty() && programs.size() <= 16, "messages.storage.program_count");
        s.versePrograms.clear();
        for (const auto &program : programs)
        {
            require(program.isDouble() && program.toDouble() == std::floor(program.toDouble()) &&
                        program.toDouble() >= 0 && program.toDouble() <= 127,
                    "messages.storage.program_integer");
            s.versePrograms.push_back(program.toInt());
        }
    }
    const auto a = o.value("notes").toArray();
    require(a.size() <= static_cast<qsizetype>(MaximumScoreNotes), "messages.storage.note_limit");
    for (const auto &v : a)
    {
        require(v.isObject(), "messages.storage.invalid_note");
        const auto n = v.toObject();
        Note note;
        note.id = integer(n, "id", int(s.notes.size()));
        note.degree = integer(n, "degree", -1, true);
        note.octave = integer(n, "octave", 0);
        note.accidental = integer(n, "accidental", 0);
        note.durationTicks = integer(n, "durationTicks", 480, true);
        note.measure = integer(n, "measure", 0);
        note.pageIndex = integer(n, "pageIndex", 0);
        note.hasImageAnchor = boolean(n, "hasImageAnchor", true, "messages.storage.invalid_rectangle");
        require(note.pageIndex >= 0 && note.pageIndex <= 9999, "messages.domain.page_range");
        note.line = integer(n, "line", 0);
        require(!n.contains("lyric") || n.value("lyric").isString(), "messages.storage.lyric_text");
        require(!n.contains("tieToNext") || n.value("tieToNext").isBool(), "messages.storage.tie_bool");
        note.lyric = n.value("lyric").toString().toStdString();
        if (n.contains("verseLyrics"))
        {
            require(n.value("verseLyrics").isArray(), "messages.storage.verse_array");
            const auto verses = n.value("verseLyrics").toArray();
            require(verses.size() <= 16, "messages.storage.verse_limit");
            QStringList legacyLines;
            for (const auto &verse : verses)
            {
                require(verse.isString() && verse.toString().size() <= 4096, "messages.storage.verse_short_text");
                note.verseLyrics.push_back(verse.toString().toStdString());
                legacyLines.append(verse.toString());
            }
            note.lyric = legacyLines.join('\n').toStdString();
        }
        else
            note.verseLyrics = lyricVerses(note);
        note.confidence = number(n, "confidence", 0.5);
        require(note.confidence >= 0 && note.confidence <= 1, "messages.storage.confidence_range");
        note.keyOverride = integer(n, "keyOverride", -1);
        note.tieToNext = n.value("tieToNext").toBool();
        if (n.contains("staffSpelling"))
        {
            require(n.value("staffSpelling").isObject(), "messages.staff.invalid_spelling");
            const auto spelling = n.value("staffSpelling").toObject();
            const auto step = spelling.value("step").toString();
            const int alter = integer(spelling, "alter", 0, true);
            const int octave = integer(spelling, "octave", 4, true);
            require(step.size() == 1 && QStringLiteral("ABCDEFG").contains(step) && alter >= -2 && alter <= 2 &&
                        octave >= -1 && octave <= 9 && note.degree != 0,
                    "messages.staff.invalid_spelling");
            note.staffSpelling = StaffSpelling{step.front().toLatin1(), alter, octave};
        }
        const auto b = n.value("bbox").toArray();
        require(b.size() == 4, "messages.storage.bbox_required");
        for (const auto &coordinate : b)
            require(coordinate.isDouble() && std::isfinite(coordinate.toDouble()),
                    "messages.storage.bbox_numbers");
        note.source = {b[0].toDouble(-1), b[1].toDouble(-1), b[2].toDouble(-1), b[3].toDouble(-1)};
        require(note.hasImageAnchor
                    ? note.source.x >= 0 && note.source.y >= 0 && note.source.width > 0 && note.source.height > 0
                    : note.source.x == 0 && note.source.y == 0 && note.source.width == 0 &&
                          note.source.height == 0,
                "messages.storage.invalid_rectangle");
        require(note.degree >= 0 && note.degree <= 7 && note.octave >= -4 && note.octave <= 4,
                "messages.storage.invalid_pitch");
        require(note.durationTicks > 0 && note.durationTicks <= MaximumNoteDurationTicks,
                "messages.storage.invalid_duration");
        require(note.accidental >= -2 && note.accidental <= 2 && note.keyOverride >= -1 && note.keyOverride <= 11,
                "messages.storage.invalid_accidental");
        s.notes.push_back(note);
    }
    if (o.contains("writtenMeasures"))
    {
        require(o.value("writtenMeasures").isArray(), "messages.domain.written_measure_range");
        const auto measures = o.value("writtenMeasures").toArray();
        require(!measures.isEmpty() && measures.size() <= 10000, "messages.domain.written_measure_range");
        for (const auto &entry : measures)
        {
            require(entry.isObject(), "messages.domain.written_measure_range");
            const auto object = entry.toObject();
            const double start = number(object, "startTick", 0, true);
            const double duration = number(object, "durationTicks", 0, true);
            require(start >= 0 && start <= 1000000000 && duration > 0 && duration <= 1000000000 - start &&
                        start == std::floor(start) && duration == std::floor(duration),
                    "messages.domain.written_measure_range");
            WrittenMeasure measure;
            measure.startTick = static_cast<std::int64_t>(start);
            measure.durationTicks = static_cast<std::int64_t>(duration);
            measure.number = integer(object, "number", 0, true);
            measure.beatsPerBar = integer(object, "beatsPerBar", 4, true);
            measure.beatUnit = integer(object, "beatUnit", 4, true);
            measure.pageIndex = integer(object, "pageIndex", 0);
            s.writtenMeasures.push_back(measure);
        }
    }
    if (o.contains("keyChanges"))
    {
        require(o.value("keyChanges").isArray(), "messages.domain.key_change_range");
        const auto changes = o.value("keyChanges").toArray();
        require(changes.size() <= int(MaximumScoreNotes), "messages.domain.key_change_range");
        for (const auto &entry : changes)
        {
            require(entry.isObject(), "messages.domain.key_change_range");
            const auto change = entry.toObject();
            const auto start = change.value("startTick");
            require(start.isDouble() && std::isfinite(start.toDouble()) && start.toDouble() >= 0 &&
                        start.toDouble() <= 1000000000 && std::floor(start.toDouble()) == start.toDouble(),
                    "messages.domain.key_change_range");
            s.keyChanges.push_back({static_cast<std::int64_t>(start.toDouble()), integer(change, "tonic", -1, true),
                                    integer(change, "sourceNoteIndex", -1)});
        }
    }
    for (const auto &v : o.value("repeats").toArray())
    {
        require(v.isObject(), "messages.storage.repeat_object");
        const auto r = v.toObject();
        int begin = integer(r, "firstNote", -1, true), end = integer(r, "endNote", -1, true);
        require(begin >= 0 && end > begin && end <= int(s.notes.size()), "messages.storage.repeat_range");
        s.repeats.push_back(
            {size_t(begin), size_t(end), integer(r, "count", 2), integer(r, "firstEndingNote", -1)});
    }
    require(s.tonic >= 0 && s.tonic <= 11 && std::isfinite(s.bpm) && s.bpm >= MinimumScoreBpm &&
                s.bpm <= MaximumScoreBpm,
            "messages.storage.invalid_key_tempo");
    require(ticksPerBar(s) > 0, "messages.storage.invalid_meter");
    const auto timeline = s.notes.empty() && s.repeats.empty() && s.writtenMeasures.empty() && s.keyChanges.empty()
                              ? Timeline{}
                              : buildTimeline(s);
    for (const auto &diagnostic : timeline.diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::Error)
            throw std::runtime_error(localizeMessage(QString::fromStdString(diagnostic.message)).toStdString());
    return s;
}
QJsonObject projectToJson(const Project &p)
{
    require(p.notationStyle == NotationStyle::Numbered || p.notationStyle == NotationStyle::Staff,
            "messages.staff.invalid_view");
    auto object = scoreToJson(p.score);
    object.insert("schemaVersion", p.staffImagePlayback ? 6 : p.staffPages.empty() ? 4 : 5);
    if (p.staffImagePlayback)
        object.insert("staffImagePlayback", true);
    if (!p.staffPages.empty())
        object.insert("staffPages", staffPagesToJson(p));
    validatePracticeMix(p.practiceMix);
    object.insert("practiceMix", QJsonObject{{"melodyEnabled", p.practiceMix.melodyEnabled},
                                             {"accompanimentEnabled", p.practiceMix.accompanimentEnabled},
                                             {"melodyVolume", p.practiceMix.melodyVolume},
                                             {"accompanimentVolume", p.practiceMix.accompanimentVolume}});
    if (p.accompaniment)
    {
        validateArrangementFields(p.score, *p.accompaniment);
        object.insert("accompaniment", arrangementToJson(*p.accompaniment));
    }
    object.insert("generatedNotation", p.generatedNotation);
    if (p.staffPerformance)
        object.insert("staffPerformance", staffPerformanceToJson(*p.staffPerformance));
    if (p.notationStyle == NotationStyle::Staff || p.staffBassClef || p.staffKeyFifths != 0 || p.staffMinor)
        object.insert("notationView",
                      QJsonObject{{"style", p.notationStyle == NotationStyle::Staff ? "staff" : "numbered"},
                                  {"bassClef", p.staffBassClef},
                                  {"keyFifths", p.staffKeyFifths},
                                  {"minor", p.staffMinor}});
    if (p.audioSource)
    {
        validateAudioSource(p.score, *p.audioSource);
        object.insert("audioSource", audioSourceToJson(*p.audioSource));
    }
    if (p.practiceSettings)
    {
        const auto &s = *p.practiceSettings;
        object.insert("practiceSettings", QJsonObject{{"transpose", s.transpose},
                                                      {"speed", s.speed},
                                                      {"metronome", s.metronome},
                                                      {"originalSpeed", s.originalSpeed},
                                                      {"originalVolume", s.originalVolume},
                                                      {"playbackSource", s.playbackSource},
                                                      {"loopEnabled", s.loopEnabled},
                                                      {"loopStart", double(s.loopStart)},
                                                      {"loopEnd", double(s.loopEnd)}});
    }
    object.insert("processing", p.processing);
    object.insert("warnings", QJsonArray::fromStringList(p.warnings));
    // Apply the same field validation on writes and reads.
    projectFromJson(object, p.image, p.staffPages);
    return object;
}
Project projectFromJson(const QJsonObject &o, const QImage &image, const std::vector<StaffPage> &pages)
{
    const int schema = integer(o, "schemaVersion", 0, true);
    require(schema >= 1 && schema <= 6, "messages.storage.project_version");
    Project p;
    p.staffImagePlayback = boolean(o, "staffImagePlayback", false, "messages.pages.invalid_project");
    require(p.staffImagePlayback == (schema == 6), "messages.pages.invalid_project");
    if (p.staffImagePlayback)
    {
        require(o.value("notes").isArray() && o.value("staffPerformance").isObject(),
                "messages.pages.invalid_project");
        const auto performance = o.value("staffPerformance").toObject();
        require(performance.value("notes").isArray(), "messages.pages.invalid_project");
        for (const auto &notes : {o.value("notes").toArray(), performance.value("notes").toArray()})
            for (const auto &entry : notes)
                require(entry.isObject() && entry.toObject().value("hasImageAnchor").isBool(),
                        "messages.pages.invalid_project");
    }
    if (schema >= 5)
    {
        require(o.value("staffPages").isArray() && !pages.empty(), "messages.pages.invalid_project");
        validateStaffPageMetadata(o.value("staffPages").toArray(), pages);
        p.staffPages = pages;
    }
    else
        require(pages.empty() && !o.contains("staffPages"), "messages.pages.invalid_project");
    p.score = scoreFromJson(o);
    if (o.contains("staffPerformance"))
    {
        require(o.value("staffPerformance").isObject(), "messages.staff.invalid_performance");
        p.staffPerformance = staffPerformanceFromJson(o.value("staffPerformance").toObject(), p.score);
    }
    if (o.contains("notationView"))
    {
        require(o.value("notationView").isObject(), "messages.staff.invalid_view");
        const auto notation = o.value("notationView").toObject();
        const auto style = notation.value("style").toString();
        require(style == "numbered" || style == "staff", "messages.staff.invalid_view");
        p.notationStyle = style == "staff" ? NotationStyle::Staff : NotationStyle::Numbered;
        p.staffBassClef = boolean(notation, "bassClef", false, "messages.staff.invalid_view");
        p.staffKeyFifths = integer(notation, "keyFifths", 0, true);
        p.staffMinor = boolean(notation, "minor", false, "messages.staff.invalid_view");
        require(p.staffKeyFifths >= -7 && p.staffKeyFifths <= 7, "messages.staff.invalid_view");
    }
    if (schema >= 3)
    {
        require(o.value("practiceMix").isObject(), "messages.storage.invalid_practice_fields");
        const auto mix = o.value("practiceMix").toObject();
        p.practiceMix = {boolean(mix, "melodyEnabled", true), boolean(mix, "accompanimentEnabled", false),
                         number(mix, "melodyVolume", .9, true), number(mix, "accompanimentVolume", .55, true)};
        validatePracticeMix(p.practiceMix);
        if (o.contains("accompaniment"))
        {
            require(o.value("accompaniment").isObject(), "messages.storage.invalid_accompaniment_fields");
            p.accompaniment = arrangementFromJson(o.value("accompaniment").toObject(), p.score);
        }
        p.generatedNotation =
            boolean(o, "generatedNotation", false, "messages.storage.invalid_generated_notation");
        if (o.contains("audioSource"))
        {
            require(o.value("audioSource").isObject(), "messages.storage.invalid_audio_source");
            p.audioSource = audioSourceFromJson(o.value("audioSource").toObject(), p.score);
        }
    }
    p.image = image;
    require(!p.image.isNull() && p.image.width() <= 12000 && p.image.height() <= 20000 &&
                qint64(p.image.width()) * p.image.height() <= 50000000,
            "messages.storage.invalid_image");
    if (p.staffPages.empty())
        for (const auto &n : p.score.notes)
            require(n.source.x + n.source.width <= p.image.width() + 2 &&
                        n.source.y + n.source.height <= p.image.height() + 2,
                    "messages.storage.note_outside");
    if (p.staffPerformance && p.staffPages.empty())
        for (const auto &note : p.staffPerformance->notes)
            require(note.source.x + note.source.width <= p.image.width() + 2 &&
                        note.source.y + note.source.height <= p.image.height() + 2,
                    "messages.storage.note_outside");
    for (const auto &w : o.value("warnings").toArray())
        p.warnings.append(w.toString());
    if (schema == 1 && std::any_of(p.score.notes.begin(), p.score.notes.end(),
                                   [](const Note &note) { return note.verseLyrics.size() > 1; }))
        p.warnings.append(trText("messages.storage.legacy_lyrics"));
    validateStaffPages(p);
    if (schema >= 4)
    {
        require(o.value("processing").isObject() &&
                    QJsonDocument(o.value("processing").toObject()).toJson(QJsonDocument::Compact).size() <=
                        1024 * 1024,
                "messages.package.invalid_metadata");
        p.processing = o.value("processing").toObject();
        if (o.contains("practiceSettings"))
        {
            require(o.value("practiceSettings").isObject(), "messages.package.invalid_metadata");
            const auto settings = o.value("practiceSettings").toObject();
            ProjectPracticeSettings s;
            s.transpose = integer(settings, "transpose", 0, true);
            s.speed = number(settings, "speed", 1, true);
            s.metronome = boolean(settings, "metronome", true);
            s.originalSpeed = number(settings, "originalSpeed", 1, true);
            s.originalVolume = number(settings, "originalVolume", .9, true);
            s.playbackSource = integer(settings, "playbackSource", 0, true);
            s.loopEnabled = boolean(settings, "loopEnabled", false);
            s.loopStart = tick(settings, "loopStart");
            s.loopEnd = tick(settings, "loopEnd");
            require(s.transpose >= -24 && s.transpose <= 24 && s.speed >= .25 && s.speed <= 2 &&
                        s.originalSpeed >= .25 && s.originalSpeed <= 2 && s.originalVolume >= 0 &&
                        s.originalVolume <= 1 && s.playbackSource >= 0 && s.playbackSource <= 3 &&
                        s.loopStart >= 0 && s.loopEnd >= s.loopStart &&
                        s.loopEnd <= (p.score.notes.empty() ? 0 : buildTimeline(p.score).durationTicks),
                    "messages.package.invalid_metadata");
            require(s.playbackSource == 0 ||
                        (p.audioSource &&
                         (s.playbackSource == 1 || (s.playbackSource == 2 && !p.audioSource->vocalsPath.empty()) ||
                          (s.playbackSource == 3 && !p.audioSource->instrumentalPath.empty()))),
                    "messages.package.invalid_metadata");
            require(p.score.notes.empty() || buildTimeline(p.score, s.transpose).valid(),
                    "messages.package.invalid_metadata");
            p.practiceSettings = s;
        }
    }
    return p;
}

void saveProject(const QString &path, const Project &project)
{
    saveProjectPackage(path, project);
}
Project loadProject(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "messages.storage.open_project");
    if (file.peek(8) == projectPackageMagic())
        return loadProjectPackage(path);
    require(file.size() <= 64 * 1024 * 1024, "messages.storage.project_limit");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "messages.storage.invalid_json");
    const auto object = document.object();
    require(integer(object, "schemaVersion", 0, true) <= 3, "messages.storage.project_version");
    QImage image;
    image.loadFromData(QByteArray::fromBase64(object.value("imagePng").toString().toLatin1()), "PNG");
    auto project = projectFromJson(object, image);
    project.score.imagePath = path.toStdString();
    return project;
}

} // namespace singlilt

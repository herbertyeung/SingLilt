// MusicXML parsing into written scores and complete staff voices.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MusicXmlImporter.h"
#include "domain/Timeline.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QXmlStreamReader>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <iterator>
#include <map>
#include <set>
#include <utility>

namespace singlilt
{
namespace
{
constexpr qint64 FileLimit = 16 * 1024 * 1024;
constexpr std::size_t NoteLimit = 100000;
constexpr std::size_t MeasureLimit = 10000;
constexpr std::int64_t TickLimit = 1000000000;

void warn(QStringList &warnings, const QString &message)
{
    if (!warnings.contains(message))
        warnings.append(message);
}

bool validateXml(const QByteArray &bytes, QString &error)
{
    if (bytes.isEmpty() || bytes.size() > FileLimit)
    {
        error = "MusicXML must contain between 1 byte and 16 MiB.";
        return false;
    }
    QXmlStreamReader xml(bytes);
    xml.setEntityExpansionLimit(1024);
    int depth = 0;
    int tokens = 0;
    while (!xml.atEnd())
    {
        const auto token = xml.readNext();
        if (++tokens > 2000000 || (token == QXmlStreamReader::StartElement && ++depth > 64))
        {
            error = "MusicXML exceeds the element count or nesting limit.";
            return false;
        }
        if (token == QXmlStreamReader::EndElement)
            --depth;
        if (token == QXmlStreamReader::DTD && !xml.entityDeclarations().isEmpty())
        {
            error = "MusicXML custom entity declarations are not supported.";
            return false;
        }
    }
    if (xml.hasError())
    {
        error = QString("MusicXML line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
        return false;
    }
    return true;
}

struct MusicXmlAttributeChange
{
    MusicXmlAttributes attributes;
    bool key = false;
    bool time = false;
};

class MusicXmlReader
{
  public:
    MusicXmlReader(const QByteArray &bytes, MusicXmlImportResult &result) : xml_(bytes), result_(result) {}

    void read()
    {
        if (!xml_.readNextStartElement() || xml_.name() != "score-partwise")
        {
            fail("Only score-partwise MusicXML is supported; export a partwise .musicxml file.");
            finish();
            return;
        }
        while (xml_.readNextStartElement())
        {
            const auto name = xml_.name();
            if (name == "part-list")
                readPartList();
            else if (name == "part")
                readPart();
            else if (name == "work")
            {
                while (xml_.readNextStartElement())
                {
                    if (xml_.name() == "work-title")
                        result_.title = text();
                    else
                        xml_.skipCurrentElement();
                }
            }
            else if (name == "movement-title")
            {
                const auto title = text();
                if (result_.title.isEmpty())
                    result_.title = title;
            }
            else
                xml_.skipCurrentElement();
        }
        if (!xml_.hasError() && result_.tracks.empty())
            fail("MusicXML has no pitched-note or rest tracks.");
        if (!xml_.hasError() && usedParts_.size() != partNames_.size())
            fail("MusicXML omits a part declared in the part list.");
        std::stable_sort(result_.tempos.begin(), result_.tempos.end(),
                         [](const MusicXmlTempo &left, const MusicXmlTempo &right)
                         { return left.startTick < right.startTick; });
        for (std::size_t i = 1; i < result_.tempos.size(); ++i)
            if (result_.tempos[i - 1].startTick == result_.tempos[i].startTick &&
                std::abs(result_.tempos[i - 1].quarterBpm - result_.tempos[i].quarterBpm) > .000001)
                fail("MusicXML parts contain conflicting tempos at the same tick.");
        if (!xml_.hasError() && result_.tempos.empty())
            warn(result_.warnings,
                 "MusicXML contains no numeric tempo; import initially uses the 90 BPM practice default. "
                 "Verify the printed tempo before practicing.");
        finish();
    }

  private:
    void fail(const QString &message)
    {
        if (!xml_.hasError())
            xml_.raiseError(message);
    }

    void finish()
    {
        if (xml_.hasError())
            result_.error = QString("MusicXML line %1: %2").arg(xml_.lineNumber()).arg(xml_.errorString());
    }

    QString text()
    {
        const auto value = xml_.readElementText().trimmed();
        if (value.size() > 8000)
            fail("MusicXML text exceeds 8000 characters.");
        return value;
    }

    std::int64_t integer(const QString &value, std::int64_t minimum, std::int64_t maximum, const QString &field)
    {
        bool valid = false;
        const auto number = value.toLongLong(&valid);
        if (!valid || number < minimum || number > maximum)
        {
            fail(QString("Invalid MusicXML %1: %2.").arg(field, value));
            return minimum;
        }
        return number;
    }

    int number(int minimum, int maximum, const QString &field)
    {
        return static_cast<int>(integer(text(), minimum, maximum, field));
    }

    double tempo(const QString &value)
    {
        bool valid = false;
        const double bpm = value.toDouble(&valid);
        if (!valid || !std::isfinite(bpm) || bpm < MinimumScoreBpm || bpm > MaximumScoreBpm)
        {
            fail("MusicXML tempo must be between 1 and 1000 quarter notes per minute.");
            return 90;
        }
        return bpm;
    }

    std::int64_t ticks(std::int64_t duration, std::int64_t divisions, bool signedOffset = false)
    {
        if (divisions <= 0)
        {
            fail("MusicXML divisions must be set before notes, backup, forward, or offsets.");
            return 0;
        }
        if ((!signedOffset && duration <= 0) || duration < -TickLimit || duration > TickLimit ||
            duration * TicksPerQuarter % divisions != 0)
        {
            fail("MusicXML duration cannot be represented exactly at 480 ticks per quarter note.");
            return 0;
        }
        const std::int64_t converted = duration * TicksPerQuarter / divisions;
        if (converted < -TickLimit || converted > TickLimit)
            fail("MusicXML duration exceeds the tick limit.");
        return converted;
    }

    void readPartList()
    {
        while (xml_.readNextStartElement())
        {
            if (xml_.name() != "score-part")
            {
                xml_.skipCurrentElement();
                continue;
            }
            const auto id = xml_.attributes().value("id").toString();
            if (id.isEmpty() || id.size() > 256 || partNames_.contains(id) || partNames_.size() >= 64)
                fail("MusicXML part IDs must be unique and the part count must not exceed 64.");
            QString name = id;
            while (xml_.readNextStartElement())
            {
                if (xml_.name() == "part-name")
                    name = text();
                else
                {
                    if (xml_.name() == "midi-instrument")
                        warn(result_.warnings,
                             "MusicXML instrument assignments are replaced by the selected practice instruments.");
                    xml_.skipCurrentElement();
                }
            }
            partNames_.insert(id, name);
        }
    }

    MusicXmlClef readClef(MusicXmlAttributes &attributes)
    {
        MusicXmlClef clef;
        const auto staff = xml_.attributes().value("number");
        if (!staff.isEmpty())
            clef.staff = static_cast<int>(integer(staff.toString(), 1, 16, "clef staff"));
        while (xml_.readNextStartElement())
        {
            if (xml_.name() == "sign")
                clef.sign = text();
            else if (xml_.name() == "line")
                clef.line = number(1, 5, "clef line");
            else if (xml_.name() == "clef-octave-change")
                clef.octaveChange = number(-3, 3, "clef octave change");
            else
                xml_.skipCurrentElement();
        }
        if (clef.sign != "G" && clef.sign != "F" && clef.sign != "C")
            fail("Only pitched G, F, and C clefs are supported.");
        const auto previous = std::find_if(attributes.clefs.begin(), attributes.clefs.end(),
                                           [&](const MusicXmlClef &entry) { return entry.staff == clef.staff; });
        if (previous == attributes.clefs.end())
            attributes.clefs.push_back(clef);
        else
            *previous = clef;
        return clef;
    }

    void readAttributes(MusicXmlPart &part, MusicXmlAttributes &attributes, std::int64_t &divisions,
                        std::int64_t cursor, int measure)
    {
        attributes.startTick = part.measures.back().startTick + cursor;
        attributes.measure = measure;
        MusicXmlAttributeChange change;
        change.attributes.startTick = attributes.startTick;
        change.attributes.measure = measure;
        change.attributes.clefs.clear();
        while (xml_.readNextStartElement())
        {
            const auto name = xml_.name();
            if (name == "divisions")
            {
                const auto updated = integer(text(), 1, 1000000, "divisions");
                if (cursor != 0 && divisions != updated)
                    fail("Mid-measure divisions changes are not supported.");
                divisions = updated;
            }
            else if (name == "key")
            {
                if (xml_.attributes().hasAttribute("number"))
                    fail("Staff-specific key signatures require a multi-staff score model.");
                bool fifths = false;
                attributes.mode = "major";
                while (xml_.readNextStartElement())
                {
                    if (xml_.name() == "fifths")
                    {
                        attributes.fifths = number(-7, 7, "key fifths");
                        fifths = true;
                    }
                    else if (xml_.name() == "mode")
                        attributes.mode = text();
                    else if (xml_.name() == "cancel")
                        xml_.skipCurrentElement();
                    else
                        fail("Non-traditional MusicXML key signatures are not supported.");
                }
                if (!fifths)
                    fail("MusicXML key requires a traditional fifths value.");
                change.key = true;
                change.attributes.fifths = attributes.fifths;
                change.attributes.mode = attributes.mode;
            }
            else if (name == "time")
            {
                int fields = 0;
                while (xml_.readNextStartElement())
                {
                    if (xml_.name() == "beats")
                    {
                        attributes.beats = number(1, MaximumBeatsPerBar, "beats");
                        ++fields;
                    }
                    else if (xml_.name() == "beat-type")
                    {
                        attributes.beatUnit = number(1, MaximumBeatUnit, "beat type");
                        ++fields;
                    }
                    else
                        fail("Only a single beats/beat-type time signature is supported.");
                }
                if (fields != 2 || (attributes.beatUnit & (attributes.beatUnit - 1)) != 0)
                    fail("MusicXML time signature must use one power-of-two denominator.");
                change.time = true;
                change.attributes.beats = attributes.beats;
                change.attributes.beatUnit = attributes.beatUnit;
            }
            else if (name == "clef")
                change.attributes.clefs.push_back(readClef(attributes));
            else if (name == "transpose")
                fail("Transposing instruments require sounding-pitch conversion and are not yet supported.");
            else if (name == "staves")
                part.staffCount = number(1, 16, "staves");
            else
                xml_.skipCurrentElement();
        }
        if (change.key || change.time || !change.attributes.clefs.empty())
            attributeChanges_.push_back(std::move(change));
        for (const auto &clef : attributes.clefs)
            part.staffCount = std::max(part.staffCount, clef.staff);
    }

    void readPitch(MusicXmlNoteEvent &note)
    {
        bool octave = false;
        while (xml_.readNextStartElement())
        {
            if (xml_.name() == "step")
                note.step = text();
            else if (xml_.name() == "alter")
                note.alter = number(-2, 2, "pitch alter (microtones are not supported)");
            else if (xml_.name() == "octave")
            {
                note.octave = number(0, 9, "pitch octave");
                octave = true;
            }
            else
                xml_.skipCurrentElement();
        }
        const int step = QString("CDEFGAB").indexOf(note.step);
        if (note.step.size() != 1 || step < 0 || !octave)
        {
            fail("MusicXML pitch requires a C..B step and an octave.");
            return;
        }
        static constexpr std::array<int, 7> semitones{0, 2, 4, 5, 7, 9, 11};
        // MusicXML alter is the complete written alteration, not an addition to the key signature.
        note.midiPitch = 12 * (note.octave + 1) + semitones[static_cast<std::size_t>(step)] + note.alter;
        if (note.midiPitch < 0 || note.midiPitch > 127)
            fail("MusicXML pitch lies outside MIDI 0..127.");
    }

    void readTie(bool &start, bool &stop)
    {
        const auto type = xml_.attributes().value("type");
        if (type == "start")
            start = true;
        else if (type == "stop")
            stop = true;
        else if (type == "continue")
            start = stop = true;
        else
            fail("MusicXML tie type must be start, stop, or continue.");
        xml_.skipCurrentElement();
    }

    void readLyric(MusicXmlNoteEvent &note)
    {
        MusicXmlLyric lyric;
        const auto number = xml_.attributes().value("number").toString();
        if (!number.isEmpty())
            lyric.number = number;
        if (lyric.number.size() > 128 || note.lyrics.size() >= 16)
            fail("MusicXML lyrics exceed the 16-verse or label length limit.");
        while (xml_.readNextStartElement())
        {
            if (xml_.name() == "text" || xml_.name() == "elision")
                lyric.text += text();
            else if (xml_.name() == "syllabic")
                lyric.syllabic = text();
            else
                xml_.skipCurrentElement();
        }
        if (lyric.text.size() > 8000 ||
            std::any_of(note.lyrics.begin(), note.lyrics.end(),
                        [&](const MusicXmlLyric &existing) { return existing.number == lyric.number; }))
            fail("MusicXML lyric text exceeds 8000 characters or duplicates a verse label on one note.");
        note.lyrics.push_back(std::move(lyric));
    }

    void readNotations(MusicXmlNoteEvent &note)
    {
        while (xml_.readNextStartElement())
        {
            if (xml_.name() == "tied")
                readTie(note.tiedStart, note.tiedStop);
            else if (xml_.name() == "tuplet")
                xml_.skipCurrentElement();
            else
            {
                warn(
                    result_.warnings,
                    "Slurs, articulations, ornaments, and other expressive markings are not applied to playback.");
                xml_.skipCurrentElement();
            }
        }
    }

    void readBeam(MusicXmlNoteEvent &note)
    {
        StaffBeam beam;
        const auto level = xml_.attributes().value("number");
        if (!level.isEmpty())
            beam.level = static_cast<int>(integer(level.toString(), 1, 8, "beam level"));
        const auto kind = text();
        if (kind == "begin")
            beam.kind = StaffBeamKind::Begin;
        else if (kind == "continue")
            beam.kind = StaffBeamKind::Continue;
        else if (kind == "end")
            beam.kind = StaffBeamKind::End;
        else if (kind == "forward hook")
            beam.kind = StaffBeamKind::ForwardHook;
        else if (kind == "backward hook")
            beam.kind = StaffBeamKind::BackwardHook;
        else
        {
            fail("MusicXML beam value must be begin, continue, end, forward hook, or backward hook.");
            return;
        }
        if (note.beams.size() >= 8 || std::any_of(note.beams.begin(), note.beams.end(), [&](const StaffBeam &entry)
                                                  { return entry.level == beam.level; }))
        {
            fail("MusicXML beam levels must be unique within one note and lie in 1..8.");
            return;
        }
        note.beams.push_back(beam);
    }

    StaffStemDirection readStem()
    {
        const auto direction = text();
        if (direction == "up")
            return StaffStemDirection::Up;
        if (direction == "down")
            return StaffStemDirection::Down;
        if (direction == "none")
            return StaffStemDirection::None;
        if (direction == "auto")
            return StaffStemDirection::Auto;
        fail("MusicXML stem must be up, down, none, or auto; double stems require additional display support.");
        return StaffStemDirection::Auto;
    }

    void readNote(std::size_t partIndex, std::int64_t divisions, int measure, std::int64_t &cursor,
                  std::int64_t &extent, std::optional<MusicXmlNoteEvent> &previous, int &previousStaff,
                  QString &previousVoice)
    {
        MusicXmlNoteEvent note;
        note.measure = measure;
        QString voice = "1";
        int staff = 1;
        std::int64_t duration = 0;
        bool pitch = false;
        bool stemSpecified = false;
        for (const auto &attribute : xml_.attributes())
            if ((attribute.name() == "attack" || attribute.name() == "release") && attribute.value() != "0")
                fail("MusicXML attack/release timing offsets are not supported.");
        while (xml_.readNextStartElement())
        {
            const auto name = xml_.name();
            if (name == "pitch")
            {
                readPitch(note);
                pitch = true;
            }
            else if (name == "rest")
            {
                note.rest = true;
                xml_.skipCurrentElement();
            }
            else if (name == "chord")
            {
                note.chord = true;
                xml_.skipCurrentElement();
            }
            else if (name == "duration")
                duration = integer(text(), 1, TickLimit, "note duration");
            else if (name == "voice")
                voice = text();
            else if (name == "staff")
                staff = number(1, 16, "note staff");
            else if (name == "type")
                note.type = text();
            else if (name == "beam")
                readBeam(note);
            else if (name == "stem")
            {
                if (stemSpecified)
                    fail("MusicXML note contains more than one stem element.");
                else
                    note.stemDirection = readStem();
                stemSpecified = true;
            }
            else if (name == "dot")
            {
                if (++note.dots > 4)
                    fail("MusicXML supports at most four augmentation dots.");
                xml_.skipCurrentElement();
            }
            else if (name == "tie")
                readTie(note.tieStart, note.tieStop);
            else if (name == "notations")
                readNotations(note);
            else if (name == "lyric")
                readLyric(note);
            else if (name == "time-modification")
            {
                while (xml_.readNextStartElement())
                {
                    if (xml_.name() == "actual-notes")
                        note.actualNotes = number(1, 64, "actual notes");
                    else if (xml_.name() == "normal-notes")
                        note.normalNotes = number(1, 64, "normal notes");
                    else
                        xml_.skipCurrentElement();
                }
            }
            else if (name == "grace" || name == "cue" || name == "unpitched")
                fail("Grace, cue, and unpitched notes require playback rules outside the current melody model.");
            else
                xml_.skipCurrentElement();
        }
        if (xml_.hasError())
            return;
        if (pitch == note.rest || duration <= 0 || voice.isEmpty() || voice.size() > 128)
        {
            fail("Each MusicXML note requires one pitch or rest, a positive duration, and a valid voice.");
            return;
        }
        if (note.rest && (note.tieStart || note.tieStop || note.tiedStart || note.tiedStop))
        {
            fail("MusicXML rests cannot carry pitched sound or graphical ties.");
            return;
        }
        const auto durationTicks = ticks(duration, divisions);
        auto &part = result_.parts[partIndex];
        part.staffCount = std::max(part.staffCount, staff);
        note.pageIndex = part.measures.back().pageIndex;
        note.startTick = part.measures.back().startTick + cursor;
        if (note.chord)
        {
            if (!previous || previousStaff != staff || previousVoice != voice || previous->rest || note.rest)
            {
                fail("MusicXML chord must follow a pitched note in the same staff and voice.");
                return;
            }
            note.startTick = previous->startTick;
        }
        note.endTick = note.startTick + durationTicks;
        if (note.endTick > TickLimit || ++noteCount_ > NoteLimit)
        {
            fail("MusicXML exceeds 100000 notes or 1000000000 ticks.");
            return;
        }
        if (!note.chord)
            cursor += durationTicks;
        extent = std::max(extent, note.endTick - part.measures.back().startTick);
        auto track =
            std::find_if(result_.tracks.begin(), result_.tracks.end(), [&](const MusicXmlTrack &entry)
                         { return entry.partIndex == partIndex && entry.staff == staff && entry.voice == voice; });
        if (track == result_.tracks.end())
        {
            if (result_.tracks.size() >= 256)
            {
                fail("MusicXML exceeds the 256-track limit.");
                return;
            }
            result_.tracks.push_back({partIndex, part.id, part.name, staff, voice, {}});
            track = std::prev(result_.tracks.end());
        }
        track->events.push_back(note);
        previous = std::move(note);
        previousStaff = staff;
        previousVoice = voice;
    }

    std::int64_t readMove(std::int64_t divisions)
    {
        std::int64_t duration = 0;
        while (xml_.readNextStartElement())
        {
            if (xml_.name() == "duration")
                duration = integer(text(), 1, TickLimit, "backup/forward duration");
            else
                xml_.skipCurrentElement();
        }
        return ticks(duration, divisions);
    }

    std::optional<double> readSound()
    {
        std::optional<double> bpm;
        for (const auto &attribute : xml_.attributes())
        {
            const auto name = attribute.name();
            if (name == "tempo")
                bpm = tempo(attribute.value().toString());
            else if (name == "dacapo" || name == "dalsegno" || name == "tocoda" || name == "fine")
                fail("Da capo, dal segno, coda, and fine navigation are not supported.");
            else if (name == "dynamics" || name == "damper-pedal")
                warn(result_.warnings, "Expressive dynamics and pedal data are not applied to playback.");
        }
        xml_.skipCurrentElement();
        return bpm;
    }

    std::optional<double> readMetronome()
    {
        QString unit;
        int dots = 0;
        std::optional<double> perMinute;
        while (xml_.readNextStartElement())
        {
            if (xml_.name() == "beat-unit")
                unit = text();
            else if (xml_.name() == "beat-unit-dot")
            {
                ++dots;
                xml_.skipCurrentElement();
            }
            else if (xml_.name() == "per-minute")
            {
                bool valid = false;
                const double rate = text().toDouble(&valid);
                if (!valid || !std::isfinite(rate) || rate <= 0)
                    fail("MusicXML metronome per-minute must be a positive finite number.");
                else
                    perMinute = rate;
            }
            else
                fail("Metric modulation requires tempo-change support outside the current melody model.");
        }
        const QMap<QString, double> quarters{{"whole", 4},  {"half", 2},    {"quarter", 1}, {"eighth", .5},
                                             {"16th", .25}, {"32nd", .125}, {"64th", .0625}};
        if (!perMinute || !quarters.contains(unit) || dots > 4)
        {
            fail("MusicXML metronome requires a supported beat unit and numeric per-minute value.");
            return {};
        }
        const double bpm = *perMinute * quarters.value(unit) * (2.0 - std::pow(.5, dots));
        if (bpm < MinimumScoreBpm || bpm > MaximumScoreBpm)
            fail("MusicXML metronome falls outside 1..1000 quarter notes per minute.");
        return bpm;
    }

    void readDirection(std::int64_t divisions, std::int64_t currentTick)
    {
        std::optional<double> metronome;
        std::optional<double> sound;
        std::int64_t offset = 0;
        while (xml_.readNextStartElement())
        {
            if (xml_.name() == "sound")
                sound = readSound();
            else if (xml_.name() == "offset")
            {
                const bool affectsSound = xml_.attributes().value("sound") == "yes";
                const auto duration = integer(text(), -TickLimit, TickLimit, "direction offset");
                if (affectsSound)
                    offset = ticks(duration, divisions, true);
            }
            else if (xml_.name() == "direction-type")
            {
                while (xml_.readNextStartElement())
                {
                    if (xml_.name() == "metronome")
                        metronome = readMetronome();
                    else if (xml_.name() == "segno" || xml_.name() == "coda" || xml_.name() == "octave-shift")
                        fail("Segno, coda, and octave-shift directions require additional playback support.");
                    else
                    {
                        if (xml_.name() == "dynamics" || xml_.name() == "wedge" || xml_.name() == "pedal")
                            warn(result_.warnings,
                                 "Expressive dynamics and pedal markings are not applied to playback.");
                        xml_.skipCurrentElement();
                    }
                }
            }
            else
                xml_.skipCurrentElement();
        }
        const auto bpm = sound ? sound : metronome;
        if (bpm)
        {
            if (currentTick + offset < 0 || currentTick + offset > TickLimit)
                fail("MusicXML tempo offset lies outside the score.");
            else
                result_.tempos.push_back({currentTick + offset, *bpm});
        }
    }

    void readBarline(MusicXmlPart &part, int measure, std::optional<int> &repeatStart)
    {
        const auto location = xml_.attributes().value("location").toString();
        while (xml_.readNextStartElement())
        {
            if (xml_.name() == "repeat")
            {
                const auto direction = xml_.attributes().value("direction");
                if (direction == "forward")
                {
                    if (repeatStart)
                        fail("Nested MusicXML repeats are not supported.");
                    repeatStart = measure + (location == "right" ? 1 : 0);
                }
                else if (direction == "backward")
                {
                    int count = 2;
                    const auto times = xml_.attributes().value("times");
                    if (!times.isEmpty())
                        count = static_cast<int>(integer(times.toString(), 2, 16, "repeat times"));
                    const int end = measure + (location == "left" ? 0 : 1);
                    const int first = repeatStart.value_or(0);
                    if (end <= first || (!part.repeats.empty() && first < part.repeats.back().endMeasure))
                        fail("MusicXML repeat boundaries overlap or are empty.");
                    part.repeats.push_back({first, end, count});
                    repeatStart.reset();
                }
                else
                    fail("MusicXML repeat direction must be forward or backward.");
                xml_.skipCurrentElement();
            }
            else if (xml_.name() == "ending")
            {
                warn(part.conversionErrors, "Volta/alternate endings require a score model with ending ranges.");
                xml_.skipCurrentElement();
            }
            else
                xml_.skipCurrentElement();
        }
    }

    void readPart()
    {
        const auto id = xml_.attributes().value("id").toString();
        if (!partNames_.contains(id) || usedParts_.contains(id) || result_.parts.size() >= 64)
        {
            fail("MusicXML part must refer to a unique declared part-list ID.");
            return;
        }
        usedParts_.insert(id);
        const std::size_t partIndex = result_.parts.size();
        result_.parts.push_back({id, partNames_.value(id), {}, {}, {}, {}});
        auto &part = result_.parts.back();
        attributeChanges_.clear();
        MusicXmlAttributes attributes;
        part.attributes.push_back(attributes);
        std::int64_t divisions = 0;
        std::int64_t startTick = 0;
        int pageIndex = 0;
        std::optional<int> repeatStart;
        while (xml_.readNextStartElement())
        {
            if (xml_.name() != "measure")
            {
                fail("MusicXML part may contain only measures.");
                break;
            }
            if (part.measures.size() >= MeasureLimit)
            {
                fail("MusicXML exceeds the 10000-measure limit.");
                break;
            }
            const int measure = static_cast<int>(part.measures.size());
            const auto number = xml_.attributes().value("number").toString();
            const bool implicit = xml_.attributes().value("implicit") == "yes";
            part.measures.push_back({number, startTick, startTick, implicit, pageIndex});
            std::int64_t cursor = 0;
            std::int64_t extent = 0;
            std::optional<MusicXmlNoteEvent> previous;
            int previousStaff = 1;
            QString previousVoice = "1";
            bool pageBreak = false;
            while (xml_.readNextStartElement())
            {
                const auto name = xml_.name();
                if (name == "attributes")
                    readAttributes(part, attributes, divisions, cursor, measure);
                else if (name == "note")
                    readNote(partIndex, divisions, measure, cursor, extent, previous, previousStaff,
                             previousVoice);
                else if (name == "backup" || name == "forward")
                {
                    const bool backward = name == "backup";
                    const auto duration = readMove(divisions);
                    cursor += backward ? -duration : duration;
                    if (cursor < 0 || cursor > TickLimit)
                        fail("MusicXML backup/forward crosses the measure start or tick limit.");
                    extent = std::max(extent, cursor);
                    previous.reset();
                }
                else if (name == "direction")
                    readDirection(divisions, startTick + cursor);
                else if (name == "sound")
                {
                    if (const auto bpm = readSound())
                        result_.tempos.push_back({startTick + cursor, *bpm});
                }
                else if (name == "barline")
                    readBarline(part, measure, repeatStart);
                else if (name == "print")
                {
                    if (!pageBreak && measure > 0 && xml_.attributes().value("new-page") == "yes")
                    {
                        pageBreak = true;
                        part.measures.back().pageIndex = ++pageIndex;
                    }
                    xml_.skipCurrentElement();
                }
                else
                {
                    if (name == "harmony" || name == "figured-bass")
                        warn(result_.warnings,
                             "Chord symbols and figured bass are display metadata, not generated accompaniment.");
                    xml_.skipCurrentElement();
                }
            }
            const std::int64_t nominal = attributes.beats * (TicksPerQuarter * 4LL / attributes.beatUnit);
            if (!implicit && extent > nominal)
                warn(result_.warnings,
                     QString("Part %1, measure %2 contains %3 ticks but its %4/%5 signature expects %6. "
                             "All events and excess time are retained and require review.")
                         .arg(part.id)
                         .arg(number.left(128))
                         .arg(extent)
                         .arg(attributes.beats)
                         .arg(attributes.beatUnit)
                         .arg(nominal));
            const auto length = implicit ? extent : std::max(extent, nominal);
            if (length <= 0 || startTick + length > TickLimit)
                fail("MusicXML measure is empty or exceeds the total tick limit.");
            startTick += length;
            part.measures.back().endTick = startTick;
            for (auto &track : result_.tracks)
            {
                if (track.partIndex != partIndex)
                    continue;
                for (auto event = track.events.rbegin(); event != track.events.rend() && event->measure == measure;
                     ++event)
                    event->pageIndex = pageIndex;
            }
        }
        if (repeatStart)
            fail("MusicXML forward repeat has no matching backward repeat.");
        std::stable_sort(attributeChanges_.begin(), attributeChanges_.end(),
                         [](const MusicXmlAttributeChange &left, const MusicXmlAttributeChange &right)
                         { return left.attributes.startTick < right.attributes.startTick; });
        part.attributes.clear();
        MusicXmlAttributes active;
        part.attributes.push_back(active);
        for (const auto &change : attributeChanges_)
        {
            active.startTick = change.attributes.startTick;
            active.measure = change.attributes.measure;
            if (change.key)
            {
                active.fifths = change.attributes.fifths;
                active.mode = change.attributes.mode;
            }
            if (change.time)
            {
                active.beats = change.attributes.beats;
                active.beatUnit = change.attributes.beatUnit;
            }
            for (const auto &clef : change.attributes.clefs)
            {
                const auto previous =
                    std::find_if(active.clefs.begin(), active.clefs.end(),
                                 [&](const MusicXmlClef &entry) { return entry.staff == clef.staff; });
                if (previous == active.clefs.end())
                    active.clefs.push_back(clef);
                else
                    *previous = clef;
            }
            if (part.attributes.back().startTick == active.startTick)
                part.attributes.back() = active;
            else
                part.attributes.push_back(active);
        }
    }

    QXmlStreamReader xml_;
    MusicXmlImportResult &result_;
    QMap<QString, QString> partNames_;
    QSet<QString> usedParts_;
    std::size_t noteCount_ = 0;
    std::vector<MusicXmlAttributeChange> attributeChanges_;
};

int positiveModulo(int value, int divisor)
{
    return (value % divisor + divisor) % divisor;
}

const MusicXmlAttributes &attributesAt(const MusicXmlPart &part, std::int64_t tick)
{
    const auto after = std::upper_bound(part.attributes.begin(), part.attributes.end(), tick,
                                        [](std::int64_t position, const MusicXmlAttributes &attributes)
                                        { return position < attributes.startTick; });
    return after == part.attributes.begin() ? part.attributes.front() : *std::prev(after);
}

int keyTonic(const MusicXmlAttributes &attributes)
{
    return positiveModulo(attributes.fifths * 7 + (attributes.mode == "minor" ? 9 : 0), 12);
}

Note convertedNote(const MusicXmlNoteEvent &event, const MusicXmlAttributes &attributes, int id)
{
    Note note;
    note.id = id;
    note.measure = event.measure;
    note.pageIndex = event.pageIndex;
    note.durationTicks = static_cast<int>(event.endTick - event.startTick);
    note.degree = 0;
    note.keyOverride = keyTonic(attributes);
    if (!event.rest)
    {
        const int tonicStep = positiveModulo(attributes.fifths * 4 + (attributes.mode == "minor" ? 5 : 0), 7);
        note.degree = positiveModulo(QString("CDEFGAB").indexOf(event.step) - tonicStep, 7) + 1;
        static constexpr std::array<int, 7> scale{0, 2, 4, 5, 7, 9, 11};
        const int difference = event.midiPitch - (60 + note.keyOverride + scale[note.degree - 1]);
        note.octave = static_cast<int>(std::round(difference / 12.0));
        note.accidental = difference - note.octave * 12;
        if (note.accidental < -2 || note.accidental > 2)
        {
            for (int degree = 1; degree <= 7; ++degree)
            {
                const int distance = event.midiPitch - (60 + note.keyOverride + scale[degree - 1]);
                const int octave = static_cast<int>(std::round(distance / 12.0));
                const int accidental = distance - octave * 12;
                if (accidental >= -2 && accidental <= 2)
                {
                    note.degree = degree;
                    note.octave = octave;
                    note.accidental = accidental;
                    break;
                }
            }
        }
        note.staffSpelling = StaffSpelling{event.step.front().toLatin1(), event.alter, event.octave};
        note.tieToNext = event.tieStart;
    }
    return note;
}
} // namespace

bool MusicXmlImportResult::valid() const
{
    return error.isEmpty() && !tracks.empty();
}

bool MusicXmlScoreResult::valid() const
{
    return error.isEmpty() && score.has_value();
}

MusicXmlImportResult parseMusicXml(const QByteArray &xml, const QString &sourceName)
{
    MusicXmlImportResult result;
    result.sourcePath = sourceName;
    if (!validateXml(xml, result.error))
        return result;
    result.sourceSha256 = QCryptographicHash::hash(xml, QCryptographicHash::Sha256).toHex();
    MusicXmlReader reader(xml, result);
    reader.read();
    if (result.title.isEmpty())
        result.title = QFileInfo(sourceName).completeBaseName();
    if (result.title.isEmpty())
        result.title = "Imported MusicXML";
    return result;
}

MusicXmlImportResult importMusicXml(const QString &path)
{
    MusicXmlImportResult result;
    result.sourcePath = path;
    if (QFileInfo(path).suffix().compare("mxl", Qt::CaseInsensitive) == 0)
    {
        result.error = "Compressed .mxl requires ZIP container support; export uncompressed .musicxml or .xml.";
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = "MusicXML could not be opened: " + file.errorString();
        return result;
    }
    if (file.size() <= 0 || file.size() > FileLimit)
    {
        result.error = "MusicXML must contain between 1 byte and 16 MiB.";
        return result;
    }
    const auto bytes = file.read(FileLimit + 1);
    if (file.error() != QFileDevice::NoError)
    {
        result.error = "MusicXML read failed: " + file.errorString();
        return result;
    }
    return parseMusicXml(bytes, QFileInfo(path).absoluteFilePath());
}

MusicXmlScoreResult musicXmlTrackToScore(const MusicXmlImportResult &imported, std::size_t trackIndex)
{
    MusicXmlScoreResult result;
    result.warnings = imported.warnings;
    if (!imported.valid() || trackIndex >= imported.tracks.size())
    {
        result.error =
            imported.error.isEmpty() ? "Choose an existing MusicXML part/staff/voice track." : imported.error;
        return result;
    }
    const auto &track = imported.tracks[trackIndex];
    const auto &part = imported.parts[track.partIndex];
    if (!part.conversionErrors.isEmpty())
    {
        result.error = part.conversionErrors.join('\n');
        return result;
    }
    Score score;
    score.title = imported.title.toStdString();
    const auto &initial = attributesAt(part, 0);
    result.keyFifths = initial.fifths;
    result.minorKey = initial.mode == "minor";
    const auto clef = std::find_if(initial.clefs.begin(), initial.clefs.end(),
                                   [&](const MusicXmlClef &entry) { return entry.staff == track.staff; });
    if (clef != initial.clefs.end())
        result.clef = *clef;
    if (result.clef.sign != "G" && result.clef.sign != "F")
    {
        result.error = "Selected C clef requires a staff renderer with alto/tenor clef support.";
        return result;
    }
    if ((result.clef.sign == "G" && result.clef.line != 2) || (result.clef.sign == "F" && result.clef.line != 4) ||
        result.clef.octaveChange != 0)
    {
        result.error = "Selected clef requires non-standard placement or octave-clef display support.";
        return result;
    }
    score.tonic = keyTonic(initial);
    score.beatsPerBar = initial.beats;
    score.beatUnit = initial.beatUnit;
    for (std::size_t i = 0; i < part.measures.size(); ++i)
    {
        const auto &measure = part.measures[i];
        const auto &attributes = attributesAt(part, measure.startTick);
        bool numeric = false;
        const int writtenNumber = measure.number.toInt(&numeric);
        const bool pickupZero =
            i == 0 && measure.implicit && writtenNumber == 0 &&
            measure.endTick - measure.startTick < attributes.beats * (TicksPerQuarter * 4LL / attributes.beatUnit);
        const int number = numeric && (writtenNumber >= 1 || pickupZero) && writtenNumber <= 1000001
                               ? writtenNumber - 1
                               : static_cast<int>(i);
        if (!numeric || (!pickupZero && writtenNumber < 1) || writtenNumber > 1000001)
            warn(result.warnings, "Non-positive or non-numeric MusicXML measure labels use ordinal display "
                                  "numbers; the original labels remain in the source.");
        score.writtenMeasures.push_back({measure.startTick, measure.endTick - measure.startTick, number,
                                         attributes.beats, attributes.beatUnit, measure.pageIndex});
    }
    for (const auto &attributes : part.attributes)
    {
        if ((attributes.mode != "major" && attributes.mode != "minor") || attributes.fifths != initial.fifths ||
            attributes.mode != initial.mode)
        {
            result.error =
                "The selected melody requires key or mode changes not supported by the current staff view.";
            return result;
        }
        const auto measure = std::upper_bound(part.measures.begin(), part.measures.end(), attributes.startTick,
                                              [](std::int64_t tick, const MusicXmlMeasure &entry)
                                              { return tick < entry.startTick; });
        const auto &containing = measure == part.measures.begin() ? part.measures.front() : *std::prev(measure);
        const auto &atStart = attributesAt(part, containing.startTick);
        if (attributes.startTick != containing.startTick &&
            (attributes.beats != atStart.beats || attributes.beatUnit != atStart.beatUnit))
        {
            result.error = "Time-signature changes inside a measure require separate written-measure spans.";
            return result;
        }
        for (const auto &entry : attributes.clefs)
            if (entry.staff == track.staff)
            {
                if ((entry.sign != "G" && entry.sign != "F") || (entry.sign == "G" && entry.line != 2) ||
                    (entry.sign == "F" && entry.line != 4) || entry.octaveChange != 0)
                {
                    result.error = "Selected clef changes require non-standard or octave-clef display support.";
                    return result;
                }
                if (entry.sign != result.clef.sign)
                    warn(result.warnings, "This practice melody changes clef; the full staff performance retains "
                                          "the written clef changes.");
            }
    }
    for (const auto &tempo : imported.tempos)
        if (tempo.startTick == 0)
            score.bpm = tempo.quarterBpm;
    for (const auto &tempo : imported.tempos)
        if (std::abs(tempo.quarterBpm - score.bpm) > .000001)
        {
            result.error =
                "MusicXML tempo changes require a tempo-map playback model; the source file is unchanged.";
            return result;
        }
    if (imported.tracks.size() > 1)
        warn(
            result.warnings,
            QString("Only selected part %1, staff %2, voice %3 is imported; %4 other tracks remain in the source.")
                .arg(track.partName)
                .arg(track.staff)
                .arg(track.voice)
                .arg(imported.tracks.size() - 1));
    auto events = track.events;
    std::stable_sort(events.begin(), events.end(),
                     [](const MusicXmlNoteEvent &left, const MusicXmlNoteEvent &right)
                     { return left.startTick < right.startTick; });
    QStringList lyricNumbers;
    for (const auto &event : events)
        for (const auto &lyric : event.lyrics)
            if (!lyricNumbers.contains(lyric.number))
                lyricNumbers.append(lyric.number);
    if (lyricNumbers.size() > 16)
    {
        result.error = "The selected track exceeds the 16-verse limit.";
        return result;
    }
    bool numericVerses = true;
    int verseSlots = 0;
    for (const auto &label : lyricNumbers)
    {
        bool numeric = false;
        const int number = label.toInt(&numeric);
        numericVerses &= numeric && number >= 1;
        if (numeric && number > 16)
        {
            result.error = "Numeric MusicXML lyric verses must fit slots 1..16.";
            return result;
        }
        if (numeric)
            verseSlots = std::max(verseSlots, number);
    }
    if (!numericVerses)
    {
        verseSlots = lyricNumbers.size();
        warn(result.warnings, "Non-numeric lyric verse labels are mapped to ordered practice-verse slots.");
    }
    std::sort(lyricNumbers.begin(), lyricNumbers.end(),
              [](const QString &left, const QString &right)
              {
                  bool leftNumeric = false, rightNumeric = false;
                  const int leftNumber = left.toInt(&leftNumeric), rightNumber = right.toInt(&rightNumeric);
                  if (leftNumeric != rightNumeric)
                      return leftNumeric;
                  return leftNumeric ? leftNumber < rightNumber : left < right;
              });
    std::vector<std::size_t> measureNoteStarts;
    std::size_t eventIndex = 0;
    std::int64_t cursor = 0;
    for (std::size_t measureIndex = 0; measureIndex < part.measures.size(); ++measureIndex)
    {
        const auto &measure = part.measures[measureIndex];
        measureNoteStarts.push_back(score.notes.size());
        const auto appendRestUntil = [&](std::int64_t tick)
        {
            while (cursor < tick)
            {
                Note rest;
                rest.id = static_cast<int>(score.notes.size());
                rest.degree = 0;
                rest.measure = score.writtenMeasures[measureIndex].number;
                rest.pageIndex = score.writtenMeasures[measureIndex].pageIndex;
                rest.durationTicks =
                    static_cast<int>(std::min<std::int64_t>(tick - cursor, MaximumNoteDurationTicks));
                score.notes.push_back(rest);
                cursor += rest.durationTicks;
            }
        };
        while (eventIndex < events.size() && events[eventIndex].measure == static_cast<int>(measureIndex))
        {
            const auto &event = events[eventIndex];
            if (event.chord || event.startTick < cursor)
            {
                result.error = "The selected part/staff/voice contains simultaneous notes; choose a monophonic "
                               "voice rather than flattening chords.";
                return result;
            }
            if (event.endTick > measure.endTick || event.endTick - event.startTick > MaximumNoteDurationTicks)
            {
                result.error = "A selected note crosses a measure boundary or exceeds the melody duration limit.";
                return result;
            }
            if (event.tieStop && (eventIndex == 0 || !events[eventIndex - 1].tieStart ||
                                  events[eventIndex - 1].endTick != event.startTick ||
                                  events[eventIndex - 1].midiPitch != event.midiPitch))
            {
                result.error = "MusicXML sound tie stop has no adjacent matching tie start in the selected voice.";
                return result;
            }
            if (event.tieStart && (eventIndex + 1 == events.size() || !events[eventIndex + 1].tieStop ||
                                   events[eventIndex + 1].startTick != event.endTick ||
                                   events[eventIndex + 1].midiPitch != event.midiPitch))
            {
                result.error = "MusicXML sound tie start has no adjacent matching tie stop in the selected voice.";
                return result;
            }
            appendRestUntil(event.startTick);
            auto note =
                convertedNote(event, attributesAt(part, event.startTick), static_cast<int>(score.notes.size()));
            note.measure = score.writtenMeasures[measureIndex].number;
            note.pageIndex = score.writtenMeasures[measureIndex].pageIndex;
            if (!lyricNumbers.isEmpty())
            {
                note.verseLyrics.resize(static_cast<std::size_t>(verseSlots));
                for (const auto &lyric : event.lyrics)
                    note.verseLyrics[static_cast<std::size_t>(
                        numericVerses ? lyric.number.toInt() - 1 : lyricNumbers.indexOf(lyric.number))] =
                        lyric.text.toStdString();
                note.lyric = note.verseLyrics.front();
            }
            if ((event.tiedStart && !event.tieStart) || (event.tiedStop && !event.tieStop))
                warn(result.warnings, "A graphical tied marking without a sound tie is not merged in playback.");
            score.notes.push_back(std::move(note));
            cursor = event.endTick;
            ++eventIndex;
        }
        appendRestUntil(measure.endTick);
        if (score.notes.size() > MaximumScoreNotes)
        {
            result.error = "The selected melody and required silent gaps exceed 100000 notes.";
            return result;
        }
    }
    measureNoteStarts.push_back(score.notes.size());
    for (const auto &repeat : part.repeats)
        score.repeats.push_back({measureNoteStarts[static_cast<std::size_t>(repeat.firstMeasure)],
                                 measureNoteStarts[static_cast<std::size_t>(repeat.endMeasure)], repeat.count,
                                 -1});
    const auto timeline = buildTimeline(score);
    if (!timeline.valid())
    {
        result.error = "The selected melody cannot be represented by the current Score playback limits.";
        return result;
    }
    result.score = std::move(score);
    return result;
}

namespace
{
struct PracticeGuide
{
    std::vector<MusicXmlNoteEvent> events;
    std::vector<int> sourceEvents;
};

PracticeGuide highestNoteGuide(const MusicXmlTrack &track, const MusicXmlPart &part)
{
    struct Change
    {
        std::int64_t tick;
        std::size_t source;
        bool starts;
    };
    std::vector<Change> changes;
    std::vector<std::int64_t> boundaries;
    for (std::size_t i = 0; i < track.events.size(); ++i)
    {
        const auto &event = track.events[i];
        changes.push_back({event.startTick, i, true});
        changes.push_back({event.endTick, i, false});
        boundaries.push_back(event.startTick);
        boundaries.push_back(event.endTick);
    }
    for (const auto &measure : part.measures)
    {
        boundaries.push_back(measure.startTick);
        boundaries.push_back(measure.endTick);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    std::stable_sort(changes.begin(), changes.end(),
                     [](const Change &left, const Change &right) { return left.tick < right.tick; });
    std::set<std::pair<int, std::size_t>> active;
    PracticeGuide guide;
    std::size_t changeIndex = 0;
    std::size_t measureIndex = 0;
    for (std::size_t boundary = 0; boundary + 1 < boundaries.size(); ++boundary)
    {
        const auto tick = boundaries[boundary];
        while (changeIndex < changes.size() && changes[changeIndex].tick <= tick)
        {
            const auto &change = changes[changeIndex++];
            const auto key = std::make_pair(track.events[change.source].midiPitch, change.source);
            if (change.starts)
                active.insert(key);
            else
                active.erase(key);
        }
        while (measureIndex + 1 < part.measures.size() && tick >= part.measures[measureIndex].endTick)
            ++measureIndex;
        const int source = active.empty() ? -1 : static_cast<int>(active.rbegin()->second);
        MusicXmlNoteEvent event;
        if (source >= 0)
            event = track.events[static_cast<std::size_t>(source)];
        else
            event.rest = true;
        event.chord = false;
        event.tieStart = event.tieStop = false;
        event.tiedStart = event.tiedStop = false;
        event.measure = static_cast<int>(measureIndex);
        event.startTick = tick;
        event.endTick = boundaries[boundary + 1];
        if (source >= 0 && event.startTick != track.events[static_cast<std::size_t>(source)].startTick)
            event.lyrics.clear();
        if (!guide.events.empty() && guide.sourceEvents.back() == source &&
            guide.events.back().measure == event.measure && guide.events.back().endTick == event.startTick)
        {
            guide.events.back().endTick = event.endTick;
            continue;
        }
        guide.events.push_back(std::move(event));
        guide.sourceEvents.push_back(source);
    }
    for (std::size_t i = 1; i < guide.events.size(); ++i)
    {
        auto &previous = guide.events[i - 1];
        auto &current = guide.events[i];
        if (previous.rest || current.rest || previous.midiPitch != current.midiPitch ||
            previous.endTick != current.startTick)
            continue;
        const auto &sourcePrevious = track.events[static_cast<std::size_t>(guide.sourceEvents[i - 1])];
        const auto &sourceCurrent = track.events[static_cast<std::size_t>(guide.sourceEvents[i])];
        if (guide.sourceEvents[i - 1] == guide.sourceEvents[i] ||
            (sourcePrevious.tieStart && sourceCurrent.tieStop &&
             sourcePrevious.endTick == sourceCurrent.startTick && previous.endTick == sourcePrevious.endTick &&
             current.startTick == sourceCurrent.startTick))
        {
            previous.tieStart = true;
            current.tieStop = true;
        }
    }
    return guide;
}

struct MusicXmlTieIssue
{
    std::size_t sourceEvent = 0;
    QString reason;
};

struct MusicXmlTieReview
{
    std::vector<bool> unresolved;
    std::vector<MusicXmlTieIssue> issues;
};

struct MusicXmlPendingTies
{
    std::deque<std::size_t> sources;
    std::int64_t firstAttackTick = 0;
    bool ambiguous = false;
};

MusicXmlTieReview inspectSoundTies(const MusicXmlTrack &track)
{
    MusicXmlTieReview review;
    review.unresolved.resize(track.events.size(), false);
    std::vector<std::size_t> events;
    std::vector<std::size_t> roots(track.events.size());
    std::vector<bool> unresolvedRoots(track.events.size(), false);
    for (std::size_t i = 0; i < track.events.size(); ++i)
    {
        roots[i] = i;
        if (!track.events[i].rest)
            events.push_back(i);
    }
    const auto rootOf = [&](std::size_t source)
    {
        while (roots[source] != source)
        {
            roots[source] = roots[roots[source]];
            source = roots[source];
        }
        return source;
    };
    std::stable_sort(events.begin(), events.end(), [&](std::size_t left, std::size_t right)
                     { return track.events[left].startTick < track.events[right].startTick; });
    std::map<std::pair<int, std::int64_t>, MusicXmlPendingTies> pending;
    for (const auto source : events)
    {
        const auto &event = track.events[source];
        if (event.tieStop)
        {
            const auto previous = pending.find({event.midiPitch, event.startTick});
            if (previous == pending.end())
            {
                unresolvedRoots[rootOf(source)] = true;
                review.issues.push_back(
                    {source, "Sound tie stop has no contiguous same-pitch start in its original voice."});
            }
            else
            {
                auto &bucket = previous->second;
                auto &candidates = bucket.sources;
                if (bucket.ambiguous)
                {
                    unresolvedRoots[rootOf(source)] = true;
                    for (const auto candidate : candidates)
                        unresolvedRoots[rootOf(candidate)] = true;
                    review.issues.push_back({source, "Sound tie stop has different overlapping attack histories "
                                                     "and cannot be paired uniquely."});
                    pending.erase(previous);
                }
                else
                {
                    const auto previousRoot = rootOf(candidates.front());
                    const auto currentRoot = rootOf(source);
                    unresolvedRoots[previousRoot] = unresolvedRoots[previousRoot] || unresolvedRoots[currentRoot];
                    roots[currentRoot] = previousRoot;
                    candidates.pop_front();
                    if (candidates.empty())
                        pending.erase(previous);
                }
            }
        }
        if (event.tieStart)
        {
            auto &bucket = pending[{event.midiPitch, event.endTick}];
            const auto attackTick = track.events[rootOf(source)].startTick;
            if (bucket.sources.empty())
                bucket.firstAttackTick = attackTick;
            else if (bucket.firstAttackTick != attackTick)
                bucket.ambiguous = true;
            bucket.sources.push_back(source);
        }
    }
    for (const auto &entry : pending)
        for (const auto source : entry.second.sources)
        {
            unresolvedRoots[rootOf(source)] = true;
            review.issues.push_back({source, "Sound tie start is not closed in its original voice."});
        }
    for (std::size_t i = 0; i < track.events.size(); ++i)
        review.unresolved[i] = unresolvedRoots[rootOf(i)];
    return review;
}

bool sameRepeats(const std::vector<MusicXmlRepeat> &left, const std::vector<MusicXmlRepeat> &right)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (left[i].firstMeasure != right[i].firstMeasure || left[i].endMeasure != right[i].endMeasure ||
            left[i].count != right[i].count)
            return false;
    return true;
}
} // namespace

bool MusicXmlPerformanceResult::valid() const
{
    return error.isEmpty() && selectedMelody.valid() && performance.has_value();
}

MusicXmlPerformanceResult musicXmlToPerformance(const MusicXmlImportResult &imported,
                                                std::size_t primaryTrackIndex, bool reviewUnresolvedSoundTies)
{
    MusicXmlPerformanceResult result;
    result.warnings = imported.warnings;
    if (!imported.valid() || primaryTrackIndex >= imported.tracks.size())
    {
        result.error = imported.error.isEmpty() ? "Choose a MusicXML practice-guide track." : imported.error;
        return result;
    }
    const auto &primaryTrack = imported.tracks[primaryTrackIndex];
    const auto &primaryPart = imported.parts[primaryTrack.partIndex];
    const auto &primaryAttributes = attributesAt(primaryPart, 0);
    std::map<std::pair<std::size_t, int>, int> globalStaves;
    for (std::size_t partIndex = 0; partIndex < imported.parts.size(); ++partIndex)
        for (int staff = 1; staff <= imported.parts[partIndex].staffCount; ++staff)
            globalStaves.try_emplace({partIndex, staff}, 0);
    std::vector<MusicXmlTieReview> tieReviews;
    tieReviews.reserve(imported.tracks.size());
    for (const auto &track : imported.tracks)
        globalStaves.try_emplace({track.partIndex, track.staff}, 0);
    if (globalStaves.size() > 2)
    {
        result.error = "Full performance currently supports one or two staves; this MusicXML retains more staves "
                       "in the parsed source.";
        return result;
    }
    int staffNumber = 0;
    for (auto &staff : globalStaves)
        staff.second = ++staffNumber;
    std::vector<MusicXmlRepeat> globalRepeats;
    std::vector<std::size_t> pageBreakMeasures;
    std::set<std::int64_t> signatureTicks{0};
    for (const auto &part : imported.parts)
    {
        if (!part.conversionErrors.isEmpty())
        {
            result.error = part.conversionErrors.join('\n');
            return result;
        }
        if (part.measures.size() != primaryPart.measures.size())
        {
            result.error =
                "MusicXML parts have different measure counts; synchronizing them would change source timing.";
            return result;
        }
        for (std::size_t i = 0; i < part.measures.size(); ++i)
            if (part.measures[i].startTick != primaryPart.measures[i].startTick ||
                part.measures[i].endTick != primaryPart.measures[i].endTick)
            {
                result.error = "MusicXML parts have different measure boundaries; full performance requires "
                               "aligned source ticks.";
                return result;
            }
        std::vector<std::size_t> partPageBreaks;
        int previousPage = 0;
        for (std::size_t i = 0; i < part.measures.size(); ++i)
            if (part.measures[i].pageIndex != previousPage)
            {
                if (i == 0 || part.measures[i].pageIndex != previousPage + 1)
                {
                    result.error = "MusicXML physical pages must advance by one at a written measure boundary.";
                    return result;
                }
                partPageBreaks.push_back(i);
                previousPage = part.measures[i].pageIndex;
            }
        if (!partPageBreaks.empty())
        {
            if (!pageBreakMeasures.empty() && pageBreakMeasures != partPageBreaks)
            {
                result.error = "MusicXML parts have incompatible physical page breaks.";
                return result;
            }
            pageBreakMeasures = std::move(partPageBreaks);
        }
        for (const auto &attributes : part.attributes)
        {
            signatureTicks.insert(attributes.startTick);
            if (attributes.fifths != primaryAttributes.fifths || attributes.mode != primaryAttributes.mode)
            {
                result.error = "Full staff display currently requires a common, fixed key signature across parts.";
                return result;
            }
        }
        if (!part.repeats.empty())
        {
            if (!globalRepeats.empty() && !sameRepeats(globalRepeats, part.repeats))
            {
                result.error = "MusicXML parts contain conflicting repeat boundaries or counts.";
                return result;
            }
            globalRepeats = part.repeats;
        }
    }
    for (const auto tick : signatureTicks)
    {
        const auto &primaryMeter = attributesAt(primaryPart, tick);
        for (const auto &part : imported.parts)
        {
            const auto &meter = attributesAt(part, tick);
            if (meter.beats != primaryMeter.beats || meter.beatUnit != primaryMeter.beatUnit)
            {
                result.error = "MusicXML parts have different time signatures at the same source tick.";
                return result;
            }
        }
    }
    for (const auto &track : imported.tracks)
    {
        if ((track.partId + ':' + track.voice).toUtf8().size() > 256)
        {
            result.error = "MusicXML part/voice identifier exceeds the 256-byte UTF-8 performance voice limit.";
            return result;
        }
        const auto &part = imported.parts[track.partIndex];
        const auto &initial = attributesAt(part, 0);
        MusicXmlClef clef;
        const auto found = std::find_if(initial.clefs.begin(), initial.clefs.end(),
                                        [&](const MusicXmlClef &entry) { return entry.staff == track.staff; });
        if (found != initial.clefs.end())
            clef = *found;
        if ((clef.sign != "G" && clef.sign != "F") || (clef.sign == "G" && clef.line != 2) ||
            (clef.sign == "F" && clef.line != 4) || clef.octaveChange != 0)
        {
            result.error = "Full staff display requires standard treble or bass clefs; no voice was dropped.";
            return result;
        }
        for (const auto &attributes : part.attributes)
            for (const auto &entry : attributes.clefs)
                if (entry.staff == track.staff &&
                    ((entry.sign != "G" && entry.sign != "F") || (entry.sign == "G" && entry.line != 2) ||
                     (entry.sign == "F" && entry.line != 4) || entry.octaveChange != 0))
                {
                    result.error = "Full performance does not support non-standard or octave-clef changes.";
                    return result;
                }
        auto tieReview = inspectSoundTies(track);
        for (const auto &issue : tieReview.issues)
        {
            const auto &event = track.events[issue.sourceEvent];
            const auto &measure = part.measures[static_cast<std::size_t>(event.measure)];
            const QString pitch =
                event.step +
                QString(event.alter < 0 ? -event.alter : event.alter, QChar(event.alter < 0 ? 'b' : '#')) +
                QString::number(event.octave);
            const QString context =
                QString("Part %1, measure %2, staff %3, voice %4, pitch %5 (MIDI %6), tick %7: %8")
                    .arg(track.partId)
                    .arg(measure.number.left(128))
                    .arg(track.staff)
                    .arg(track.voice)
                    .arg(pitch)
                    .arg(event.midiPitch)
                    .arg(event.startTick)
                    .arg(issue.reason);
            if (!reviewUnresolvedSoundTies)
            {
                result.error = context;
                return result;
            }
            warn(result.warnings, context + " Original note events and tie marks are retained; the unresolved "
                                            "chain plays separate written durations until reviewed.");
        }
        tieReviews.push_back(std::move(tieReview));
    }
    std::vector<int> measurePages(primaryPart.measures.size(), 0);
    for (std::size_t i = 0; i < measurePages.size(); ++i)
        measurePages[i] = static_cast<int>(
            std::upper_bound(pageBreakMeasures.begin(), pageBreakMeasures.end(), i) - pageBreakMeasures.begin());
    auto guide = highestNoteGuide(primaryTrack, primaryPart);
    if (guide.events.empty() || guide.events.size() > MaximumScoreNotes)
    {
        result.error = "The highest-note practice guide is empty or exceeds 100000 notes.";
        return result;
    }
    auto guideImport = imported;
    for (std::size_t i = 0; i < guide.events.size(); ++i)
    {
        auto &event = guide.events[i];
        event.pageIndex = measurePages[static_cast<std::size_t>(event.measure)];
        if (guide.sourceEvents[i] >= 0 &&
            tieReviews[primaryTrackIndex].unresolved[static_cast<std::size_t>(guide.sourceEvents[i])])
            event.tieStart = event.tieStop = false;
    }
    for (std::size_t i = 0; i < measurePages.size(); ++i)
        guideImport.parts[primaryTrack.partIndex].measures[i].pageIndex = measurePages[i];
    guideImport.tracks[primaryTrackIndex].events = guide.events;
    guideImport.parts[primaryTrack.partIndex].repeats = globalRepeats;
    result.selectedMelody = musicXmlTrackToScore(guideImport, primaryTrackIndex);
    if (!result.selectedMelody.valid())
    {
        result.error = result.selectedMelody.error;
        return result;
    }
    result.selectedMelody.warnings.removeIf([](const QString &message)
                                            { return message.startsWith("Only selected part "); });
    for (const auto &warning : result.selectedMelody.warnings)
        warn(result.warnings, warning);
    if (result.selectedMelody.score->notes.size() != guide.events.size())
    {
        result.error = "The practice guide cannot map its source notes without changing timing.";
        return result;
    }
    warn(result.warnings, "All written pitches play together. The selected track's highest sounding line is only "
                          "the single-note practice guide.");
    std::vector<int> practiceIndices(primaryTrack.events.size(), -1);
    for (std::size_t i = 0; i < guide.sourceEvents.size(); ++i)
        if (guide.sourceEvents[i] >= 0 && practiceIndices[static_cast<std::size_t>(guide.sourceEvents[i])] < 0)
            practiceIndices[static_cast<std::size_t>(guide.sourceEvents[i])] = static_cast<int>(i);
    StaffPerformance performance;
    performance.staffCount = static_cast<int>(globalStaves.size());
    performance.primaryStaff = globalStaves.at({primaryTrack.partIndex, primaryTrack.staff});
    performance.sourceTonic = result.selectedMelody.score->tonic;
    performance.durationTicks = primaryPart.measures.back().endTick;
    performance.timingFingerprint = staffTimingFingerprint(*result.selectedMelody.score);
    for (const auto &[identity, globalStaff] : globalStaves)
    {
        const auto &part = imported.parts[identity.first];
        bool previousBass = false;
        bool first = true;
        for (const auto &attributes : part.attributes)
        {
            const auto clef =
                std::find_if(attributes.clefs.begin(), attributes.clefs.end(),
                             [&](const MusicXmlClef &entry) { return entry.staff == identity.second; });
            const bool bass = clef != attributes.clefs.end() && clef->sign == "F";
            if (first || bass != previousBass)
            {
                performance.clefChanges.push_back({attributes.startTick, globalStaff, bass});
                previousBass = bass;
                first = false;
            }
        }
    }
    std::stable_sort(performance.clefChanges.begin(), performance.clefChanges.end(),
                     [](const StaffClefChange &left, const StaffClefChange &right)
                     { return left.startTick < right.startTick; });
    for (std::size_t trackIndex = 0; trackIndex < imported.tracks.size(); ++trackIndex)
    {
        const auto &track = imported.tracks[trackIndex];
        for (std::size_t eventIndex = 0; eventIndex < track.events.size(); ++eventIndex)
        {
            const auto &event = track.events[eventIndex];
            if (event.rest)
                continue;
            if ((event.tiedStart && !event.tieStart) || (event.tiedStop && !event.tieStop))
                warn(result.warnings, "A graphical tied marking without a sound tie is not merged in playback.");
            StaffPerformanceNote note;
            note.startTick = event.startTick;
            note.durationTicks = event.endTick - event.startTick;
            note.midiPitch = event.midiPitch;
            note.staff = globalStaves.at({track.partIndex, track.staff});
            note.voice = (track.partId + ':' + track.voice).toStdString();
            note.tieStart = event.tieStart;
            note.tieStop = event.tieStop;
            note.unresolvedSoundTie = tieReviews[trackIndex].unresolved[eventIndex];
            note.staffSpelling = StaffSpelling{event.step.front().toLatin1(), event.alter, event.octave};
            note.pageIndex = measurePages[static_cast<std::size_t>(event.measure)];
            note.beams = event.beams;
            note.stemDirection = event.stemDirection;
            if (trackIndex == primaryTrackIndex)
                note.sourceNoteIndex = practiceIndices[eventIndex];
            performance.notes.push_back(std::move(note));
        }
    }
    if (performance.notes.empty())
    {
        result.error = "MusicXML full performance contains no pitched notes.";
        return result;
    }
    std::stable_sort(performance.notes.begin(), performance.notes.end(),
                     [](const StaffPerformanceNote &left, const StaffPerformanceNote &right)
                     { return left.startTick < right.startTick; });
    result.performance = std::move(performance);
    return result;
}
} // namespace singlilt

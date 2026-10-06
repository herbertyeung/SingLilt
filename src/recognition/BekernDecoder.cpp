// kern/bekern decoding into complete staff-note events.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "BekernDecoder.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
constexpr qsizetype TextLimit = 16 * 1024 * 1024;
constexpr int EventLimit = 100000;
constexpr int MeasureLimit = 10000;
constexpr std::int64_t TickLimit = 1000000000;

QString recordsFromTokens(QString text)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    if (text.contains("<b>") || text.contains("<t>"))
    {
        // Model-token spaces are not chord separators: only <s> denotes a chord space.
        text.remove(QRegularExpression("\\s+"));
        text.replace("<b>", "\n");
        text.replace("<t>", "\t");
        text.replace("<s>", " ");
    }
    text = text.trimmed();
    if (text.startsWith("<bos>"))
        text.remove(0, 5);
    if (text.endsWith("<eos>"))
        text.chop(5);
    return text.trimmed();
}

struct Spine
{
    std::size_t trackIndex = 0;
    std::int64_t endTick = 0;
    bool hasClef = false;
};

struct Duration
{
    std::int64_t ticks = 0;
    QString type;
    int dots = 0;
    int actualNotes = 1;
    int normalNotes = 1;
};

class BekernReader
{
  public:
    BekernReader(MusicXmlImportResult &result, const BekernDecodeOptions &options)
        : result_(result), options_(options)
    {
        MusicXmlPart part;
        part.id = "P1";
        part.name = "Recognized score";
        result_.parts.push_back(std::move(part));
    }

    void read(const QString &text)
    {
        if (options_.reviewRhythmConflicts)
            warn("OMR review clock interpretation—not strict kern or corrected rhythm. Original pitches and "
                 "reciprocal durations are retained; onsets follow the common record clock and require manual "
                 "review.");
        const auto lines = text.split('\n');
        if (lines.size() > 200000)
            fail("Record count exceeds 200000.");
        for (const auto &line : lines)
        {
            ++lineNumber_;
            if (line.trimmed().isEmpty())
                continue;
            if (line.startsWith("!!"))
            {
                readComment(line);
                continue;
            }
            if (terminated_)
                fail("A record follows the final spine termination.");
            lastRecordWasBarline_ = false;
            auto cells = line.split('\t', Qt::KeepEmptyParts);
            for (auto &cell : cells)
            {
                cell = cell.trimmed();
                if (cell.isEmpty())
                    fail("An empty spine field is not a null token.");
            }
            if (spines_.empty())
                createSpines(cells.size());
            if (cells.size() != static_cast<qsizetype>(spines_.size()))
                fail("The record width does not match the active spine count.");
            if (cells.front().startsWith('!'))
            {
                if (!std::all_of(cells.begin(), cells.end(),
                                 [](const QString &cell) { return cell.startsWith('!'); }))
                    fail("A local comment is mixed with musical data.");
            }
            else if (cells.front().startsWith('*'))
                readInterpretations(cells);
            else if (cells.front().startsWith('='))
                readBarline(cells);
            else
                readData(cells);
        }
        if (!terminated_)
        {
            if (!options_.pageFragment || !lastRecordWasBarline_ || currentTick_ != measureStart_ ||
                std::any_of(spines_.begin(), spines_.end(),
                            [&](const Spine &spine) { return spine.endTick != currentTick_; }))
                fail("Missing *- termination or a complete page-fragment barline; the recognition stream may be "
                     "truncated.");
            warn("The model output is a page fragment ending at a complete measure boundary, not a terminated "
                 "whole score; review cross-page ties and navigation.");
        }
        if (eventCount_ == 0 || !hasPitchedNote_ || result_.parts.front().measures.empty())
            fail("The recognition contains no pitched musical events.");
        if (repeatStart_)
            fail("A forward repeat has no matching backward repeat.");
    }

  private:
    [[noreturn]] void fail(const QString &message) const
    {
        throw std::runtime_error(QString("Bekern line %1: %2").arg(lineNumber_).arg(message).toStdString());
    }

    void warn(const QString &message)
    {
        if (!result_.warnings.contains(message))
            result_.warnings.append(message);
    }

    void createSpines(qsizetype count)
    {
        if (count < 1 || count > 2)
            fail("The initial record must contain one or two piano staves.");
        auto &part = result_.parts.front();
        part.staffCount = static_cast<int>(count);
        if (options_.initialAttributes)
        {
            attributes_ = *options_.initialAttributes;
            attributes_.startTick = 0;
            attributes_.measure = 0;
            if (attributes_.beats < 1 || attributes_.beats > 32 || attributes_.beatUnit < 1 ||
                attributes_.beatUnit > 64 || (attributes_.beatUnit & (attributes_.beatUnit - 1)) != 0 ||
                attributes_.fifths < -7 || attributes_.fifths > 7 ||
                (attributes_.mode != "major" && attributes_.mode != "minor"))
                fail("Inherited attributes contain an invalid meter or key signature.");
            hasMeter_ = true;
            hasKeySignature_ = true;
            keySignatureInherited_ = true;
        }
        const auto inheritedClefs = attributes_.clefs;
        if (options_.initialAttributes)
        {
            std::vector<int> seenStaves;
            for (const auto &clef : inheritedClefs)
            {
                if (clef.staff < 1 || clef.staff > count || clef.line < 1 || clef.line > 5 ||
                    (clef.sign != "G" && clef.sign != "F" && clef.sign != "C") || clef.octaveChange != 0 ||
                    std::find(seenStaves.begin(), seenStaves.end(), clef.staff) != seenStaves.end())
                    fail("Inherited clefs contain an unsupported or duplicate staff declaration.");
                seenStaves.push_back(clef.staff);
            }
        }
        attributes_.clefs.clear();
        for (int column = 0; column < count; ++column)
        {
            // Humdrum piano spines are written bottom-to-top, opposite our staff numbering.
            const int staff = static_cast<int>(count) - column;
            MusicXmlTrack track;
            track.partId = part.id;
            track.partName = part.name;
            track.staff = staff;
            track.voice = QString::number(nextVoice_++);
            result_.tracks.push_back(std::move(track));
            const auto inherited = std::find_if(inheritedClefs.begin(), inheritedClefs.end(),
                                                [staff](const MusicXmlClef &clef) { return clef.staff == staff; });
            const bool hasInheritedClef = options_.initialAttributes && inherited != inheritedClefs.end();
            spines_.push_back({result_.tracks.size() - 1, 0, hasInheritedClef});
            attributes_.clefs.push_back(hasInheritedClef
                                            ? *inherited
                                            : MusicXmlClef{staff, staff == 1 ? "G" : "F", staff == 1 ? 2 : 4, 0});
        }
        part.attributes.push_back(attributes_);
    }

    void readComment(const QString &line)
    {
        if (line.startsWith("!!!OTL:"))
            result_.title = line.mid(7).trimmed();
        else if (line.startsWith("!!LO:PB"))
        {
            if (terminated_ || currentTick_ != measureStart_ || result_.parts.front().measures.empty())
                fail("A page break must precede a new written measure.");
            if (++pageIndex_ >= 32)
                fail("Page count exceeds 32.");
        }
    }

    void saveAttributes()
    {
        attributes_.startTick = currentTick_;
        attributes_.measure = static_cast<int>(result_.parts.front().measures.size());
        auto &changes = result_.parts.front().attributes;
        if (changes.back().startTick == currentTick_)
            changes.back() = attributes_;
        else
            changes.push_back(attributes_);
    }

    void readInterpretations(const QStringList &cells)
    {
        if (!std::all_of(cells.begin(), cells.end(), [](const QString &cell) { return cell.startsWith('*'); }))
            fail("An interpretation is mixed with musical data.");
        const bool operation =
            std::any_of(cells.begin(), cells.end(), [](const QString &cell)
                        { return cell == "*^" || cell == "*v" || cell == "*x" || cell == "*-" || cell == "*+"; });
        if (operation)
        {
            readSpineOperations(cells);
            return;
        }
        std::optional<int> meterBeats;
        std::optional<int> meterUnit;
        std::optional<int> keyFifths;
        std::optional<QString> keyMode;
        std::optional<int> keyTokenFifths;
        std::optional<double> tempo;
        for (qsizetype column = 0; column < cells.size(); ++column)
        {
            const auto &token = cells[column];
            const int staff = result_.tracks[spines_[static_cast<std::size_t>(column)].trackIndex].staff;
            if (token == "*")
                continue;
            if (token == "**kern" || token == "**ekern" || token == "**ekern_1.0" || token == "**bekern")
            {
                if (eventCount_ != 0 || sawHeader_)
                    fail("Exclusive interpretations may only appear in the initial record.");
                continue;
            }
            if (token.startsWith("*clef"))
            {
                const auto match = QRegularExpression("^\\*clef([GFC])([1-5])$").match(token);
                if (!match.hasMatch())
                    fail("Unsupported or malformed clef: " + token);
                auto found = std::find_if(attributes_.clefs.begin(), attributes_.clefs.end(),
                                          [staff](const MusicXmlClef &clef) { return clef.staff == staff; });
                found->sign = match.captured(1);
                found->line = match.captured(2).toInt();
                spines_[static_cast<std::size_t>(column)].hasClef = true;
            }
            else if (token.startsWith("*MM"))
            {
                const auto match = QRegularExpression("^\\*MM([0-9]+(?:\\.[0-9]+)?)$").match(token);
                if (!match.hasMatch())
                    fail("Unsupported or malformed tempo: " + token);
                const double bpm = match.captured(1).toDouble();
                if (bpm < 10 || bpm > 400 || (tempo && *tempo != bpm))
                    fail("Tempo is out of range or inconsistent across staves.");
                tempo = bpm;
            }
            else if (token.startsWith("*M") && !token.startsWith("*met"))
            {
                const auto match = QRegularExpression("^\\*M([0-9]+)/([0-9]+)$").match(token);
                if (!match.hasMatch())
                    fail("Unsupported or malformed time signature: " + token);
                const int beats = match.captured(1).toInt();
                const int unit = match.captured(2).toInt();
                if (beats < 1 || beats > 32 || unit < 1 || unit > 64 || (unit & (unit - 1)) != 0 ||
                    (meterBeats && (*meterBeats != beats || *meterUnit != unit)))
                    fail("Time signature is out of range or inconsistent across staves.");
                meterBeats = beats;
                meterUnit = unit;
            }
            else if (token.startsWith("*k["))
            {
                static const QStringList signatures{"",         "f#",         "f#c#",         "f#c#g#",
                                                    "f#c#g#d#", "f#c#g#d#a#", "f#c#g#d#a#e#", "f#c#g#d#a#e#b#"};
                static const QStringList flats{"",         "b-",         "b-e-",         "b-e-a-",
                                               "b-e-a-d-", "b-e-a-d-g-", "b-e-a-d-g-c-", "b-e-a-d-g-c-f-"};
                if (!token.endsWith(']'))
                    fail("Malformed key signature: " + token);
                const auto signature = token.mid(3, token.size() - 4);
                int fifths = signatures.indexOf(signature);
                if (fifths < 0)
                {
                    const int flatCount = flats.indexOf(signature);
                    if (flatCount < 0)
                        fail("Only conventional key signatures are supported: " + token);
                    fifths = -flatCount;
                }
                if (keyFifths && *keyFifths != fifths)
                    fail("Key signatures disagree across staves.");
                keyFifths = fifths;
            }
            else if (QRegularExpression("^\\*[A-Ga-g](?:#|-)?:$").match(token).hasMatch())
            {
                const QString mode = token[1].isLower() ? "minor" : "major";
                if (keyMode && *keyMode != mode)
                    fail("Key modes disagree across staves.");
                keyMode = mode;
                static const int majorFifths[]{3, 5, 0, 2, 4, -1, 1};
                const int accidental = token.contains('#') ? 1 : token.contains('-') ? -1 : 0;
                const int fifths =
                    majorFifths[token[1].toLower().unicode() - 'a'] + accidental * 7 - (mode == "minor" ? 3 : 0);
                if (fifths < -7 || fifths > 7 || (keyTokenFifths && *keyTokenFifths != fifths))
                    fail("Key centers are out of range or disagree across staves.");
                keyTokenFifths = fifths;
            }
            else if (token == "*met(c)" || token == "*met(c|)")
            {
                if (!hasMeter_ && !meterBeats)
                {
                    meterBeats = token == "*met(c)" ? 4 : 2;
                    meterUnit = token == "*met(c)" ? 4 : 2;
                }
            }
            else if (token == "*tuplet" || token == "*Xtuplet")
                warn("Tuplet bracket display is not retained; reciprocal durations preserve the sounding rhythm.");
            else if (token.startsWith("*staff"))
            {
                if (token != "*staff" + QString::number(staff))
                    fail("Staff reassignment conflicts with piano spine order: " + token);
            }
            else
                fail("Unsupported interpretation: " + token);
        }
        if (cells.front().startsWith("**"))
        {
            if (!std::all_of(cells.begin(), cells.end(),
                             [](const QString &cell) { return cell.startsWith("**"); }))
                fail("An exclusive-interpretation header is incomplete.");
            sawHeader_ = true;
        }
        if ((meterBeats || keyFifths || keyMode) && currentTick_ != measureStart_)
            fail("A key or time signature changes inside a written measure.");
        if (keyTokenFifths)
        {
            if ((keyFifths && *keyFifths != *keyTokenFifths) ||
                (!keyFifths && hasKeySignature_ && !keySignatureInherited_ &&
                 attributes_.fifths != *keyTokenFifths))
                fail("The key center conflicts with its key signature.");
            keyFifths = keyTokenFifths;
        }
        if (meterBeats)
        {
            attributes_.beats = *meterBeats;
            attributes_.beatUnit = *meterUnit;
            hasMeter_ = true;
        }
        if (keyFifths)
        {
            attributes_.fifths = *keyFifths;
            hasKeySignature_ = true;
            keySignatureInherited_ = false;
        }
        if (keyMode)
            attributes_.mode = *keyMode;
        if (tempo)
            result_.tempos.push_back({currentTick_, *tempo});
        saveAttributes();
    }

    void readSpineOperations(const QStringList &cells)
    {
        if (std::any_of(cells.begin(), cells.end(), [](const QString &cell)
                        { return cell != "*" && cell != "*^" && cell != "*v" && cell != "*x" && cell != "*-"; }))
            fail("Unsupported or mixed spine operation.");
        if (options_.reviewRhythmConflicts && cells.count("*-") == cells.size())
            closeMeasure();
        const auto exchanges = cells.count("*x");
        if (exchanges != 0)
        {
            if (exchanges != 2 || std::any_of(cells.begin(), cells.end(),
                                              [](const QString &cell) { return cell != "*" && cell != "*x"; }))
                fail("A spine exchange requires exactly two *x tokens and no other operation.");
            const auto first = cells.indexOf("*x");
            const auto second = cells.lastIndexOf("*x");
            std::swap(spines_[static_cast<std::size_t>(first)], spines_[static_cast<std::size_t>(second)]);
            return;
        }
        std::vector<Spine> next;
        for (qsizetype column = 0; column < cells.size(); ++column)
        {
            const auto &token = cells[column];
            const auto spine = spines_[static_cast<std::size_t>(column)];
            if (token == "*-")
            {
                if (spine.endTick != currentTick_)
                    fail("A spine terminates before its pending note or rest has ended.");
                continue;
            }
            if (token == "*v")
            {
                const auto first = column;
                while (column + 1 < cells.size() && cells[column + 1] == "*v")
                {
                    ++column;
                    const auto &other = spines_[static_cast<std::size_t>(column)];
                    if (other.endTick != spine.endTick ||
                        result_.tracks[other.trackIndex].staff != result_.tracks[spine.trackIndex].staff)
                        fail("Merged spines must belong to the same staff and have synchronized durations.");
                }
                if (column == first)
                    fail("A spine merge requires at least two adjacent *v tokens.");
                next.push_back(spine);
                continue;
            }
            next.push_back(spine);
            if (token == "*^")
            {
                if (result_.tracks.size() >= 128)
                    fail("Voice count exceeds 128.");
                auto track = result_.tracks[spine.trackIndex];
                track.events.clear();
                track.voice = QString::number(nextVoice_++);
                result_.tracks.push_back(std::move(track));
                auto branch = spine;
                branch.trackIndex = result_.tracks.size() - 1;
                next.push_back(branch);
            }
        }
        spines_ = std::move(next);
        if (spines_.empty())
        {
            closeMeasure();
            terminated_ = true;
        }
    }

    Duration duration(QString &note, const std::optional<Duration> &inherited)
    {
        static const QRegularExpression expression("(0+|[1-9][0-9]*)(?:%([0-9]+))?(\\.*)");
        auto matches = expression.globalMatch(note);
        if (!matches.hasNext())
        {
            if (!inherited)
                fail("A note or rest has no reciprocal duration: " + note);
            return *inherited;
        }
        const auto match = matches.next();
        if (matches.hasNext())
            fail("A note contains multiple reciprocal durations: " + note);
        Duration parsed;
        parsed.dots = match.captured(3).size();
        if (parsed.dots > 4)
            fail("More than four augmentation dots are not supported.");
        bool valid = false;
        const auto digits = match.captured(1);
        std::int64_t reciprocal = digits.toLongLong(&valid);
        if (!valid || reciprocal > 1000000 || digits.size() > 7)
            fail("A reciprocal duration exceeds its range.");
        std::int64_t numerator = TicksPerQuarter * 4;
        std::int64_t denominator = std::max<std::int64_t>(1, reciprocal);
        if (reciprocal == 0)
        {
            if (digits.size() > 6 || !match.captured(2).isEmpty())
                fail("Invalid longa/breve duration.");
            numerator *= std::int64_t(1) << digits.size();
            parsed.type = digits.size() == 1 ? "breve" : "long";
        }
        else
        {
            std::int64_t multiplier = 1;
            if (!match.captured(2).isEmpty())
            {
                multiplier = match.captured(2).toLongLong(&valid);
                if (!valid || multiplier < 1 || multiplier > 1000000)
                    fail("Invalid rational reciprocal duration.");
            }
            numerator *= multiplier;
            int base = 1;
            while (base < 256 && (base * 2LL) * multiplier <= reciprocal)
                base *= 2;
            const auto divisor = std::gcd(reciprocal, base * multiplier);
            parsed.actualNotes = static_cast<int>(reciprocal / divisor);
            parsed.normalNotes = static_cast<int>(base * multiplier / divisor);
            if (parsed.actualNotes > 64 || parsed.normalNotes > 64)
                fail("Tuplet ratio exceeds the supported range.");
            static const QStringList types{"whole", "half", "quarter", "eighth", "16th",
                                           "32nd",  "64th", "128th",   "256th"};
            int power = 0;
            for (int unit = base; unit > 1; unit /= 2)
                ++power;
            parsed.type = types[power];
        }
        const std::int64_t dotDenominator = std::int64_t(1) << parsed.dots;
        numerator *= dotDenominator * 2 - 1;
        denominator *= dotDenominator;
        if (numerator % denominator != 0)
            fail("A duration cannot be represented exactly at 480 ticks per quarter.");
        parsed.ticks = numerator / denominator;
        if (parsed.ticks < 1 || parsed.ticks > TicksPerQuarter * 256LL)
            fail("A note duration exceeds the playback range.");
        note.remove(match.capturedStart(), match.capturedLength());
        return parsed;
    }

    MusicXmlNoteEvent noteEvent(QString token, const std::optional<Duration> &inherited, Duration &parsed)
    {
        token.remove('@');
        token.remove(QChar(0x00b7));
        parsed = duration(token, inherited);
        MusicXmlNoteEvent event;
        event.startTick = currentTick_;
        event.endTick = currentTick_ + parsed.ticks;
        if (event.endTick > TickLimit)
            fail("The score exceeds the total tick limit.");
        event.measure = static_cast<int>(result_.parts.front().measures.size());
        event.pageIndex = pageIndex_;
        event.type = parsed.type;
        event.dots = parsed.dots;
        event.actualNotes = parsed.actualNotes;
        event.normalNotes = parsed.normalNotes;
        static const QRegularExpression pitchExpression("([a-g]+|[A-G]+|r{1,2})(#{1,2}|-{1,2}|n)?");
        auto pitches = pitchExpression.globalMatch(token);
        if (!pitches.hasNext())
            fail("A note has no pitch or rest: " + token);
        const auto pitch = pitches.next();
        if (pitches.hasNext())
            fail("A note contains multiple pitches without a chord separator: " + token);
        const auto spelling = pitch.captured(1);
        const auto accidental = pitch.captured(2);
        event.rest = spelling.startsWith('r');
        event.midiPitch = -1;
        if (!event.rest)
        {
            if (!std::all_of(spelling.begin(), spelling.end(),
                             [&](QChar letter) { return letter == spelling.front(); }))
                fail("Octave spelling repeats different pitch letters: " + spelling);
            event.step = QString(spelling.front().toUpper());
            const int letterCount = static_cast<int>(spelling.size());
            event.octave = spelling.front().isLower() ? 3 + letterCount : 4 - letterCount;
            if (event.octave < -1 || event.octave > 9)
                fail("Pitch octave exceeds the MIDI range.");
            event.alter = accidental.startsWith('#')   ? accidental.size()
                          : accidental.startsWith('-') ? -accidental.size()
                                                       : 0;
            static const int pitchClasses[]{9, 11, 0, 2, 4, 5, 7};
            event.midiPitch =
                (event.octave + 1) * 12 + pitchClasses[spelling.front().toLower().unicode() - 'a'] + event.alter;
            if (event.midiPitch < 0 || event.midiPitch > 127)
                fail("Pitch exceeds the MIDI range.");
        }
        else if (!accidental.isEmpty())
            fail("A rest cannot carry an accidental.");
        token.remove(pitch.capturedStart(), pitch.capturedLength());
        event.tieStart = token.contains('[') || token.contains('_');
        event.tieStop = token.contains(']') || token.contains('_');
        event.tiedStart = event.tieStart;
        event.tiedStop = event.tieStop;
        if (event.rest && (event.tieStart || event.tieStop))
            fail("A rest cannot be tied.");
        const int starts = token.count('L');
        const int ends = token.count('J');
        const int rightHooks = token.count('K');
        const int leftHooks = token.count('k');
        const int mainBeams = std::max(starts, ends);
        if (mainBeams + rightHooks + leftHooks > 8)
            fail("Beam level exceeds eight.");
        for (int level = 1; level <= mainBeams; ++level)
        {
            if (level <= starts && level <= ends)
                fail("A beam both begins and ends on the same note.");
            if (level <= starts)
                event.beams.push_back({level, StaffBeamKind::Begin});
            else if (level <= ends)
                event.beams.push_back({level, StaffBeamKind::End});
        }
        for (int index = 1; index <= rightHooks; ++index)
            event.beams.push_back({mainBeams + index, StaffBeamKind::ForwardHook});
        for (int index = 1; index <= leftHooks; ++index)
            event.beams.push_back({mainBeams + rightHooks + index, StaffBeamKind::BackwardHook});
        if (token.contains('/') && token.contains('\\'))
            fail("A note has conflicting stem directions.");
        if (token.contains('/'))
            event.stemDirection = StaffStemDirection::Up;
        else if (token.contains('\\'))
            event.stemDirection = StaffStemDirection::Down;
        for (const QChar symbol : token)
        {
            if (QStringLiteral("[]_LJKk/\\").contains(symbol))
                continue;
            if (QStringLiteral("(){}&;'\"`~^uvz,STtMmWwo$yYXx?!").contains(symbol))
            {
                warn("Slurs, articulations, ornaments and editorial marks are not applied to playback.");
                continue;
            }
            fail("Unsupported note symbol: " + QString(symbol));
        }
        return event;
    }

    void readData(const QStringList &cells)
    {
        if (!hasMeter_)
            fail("A time signature is required before the first musical record.");
        if (std::any_of(cells.begin(), cells.end(), [](const QString &cell) { return cell.contains('q'); }))
        {
            readGraceAnnotations(cells);
            return;
        }
        bool newEvent = false;
        for (qsizetype column = 0; column < cells.size(); ++column)
        {
            auto &spine = spines_[static_cast<std::size_t>(column)];
            const auto &cell = cells[column];
            if (cell == ".")
            {
                if (spine.endTick <= currentTick_)
                {
                    if (!options_.reviewRhythmConflicts)
                        fail("A null token has no active note or rest to sustain.");
                    const auto &track = result_.tracks[spine.trackIndex];
                    warn(QString("Bekern line %1: OMR rhythm review: staff %2, voice %3, null token at record "
                                 "tick %4 has no pending event; no rest is invented.")
                             .arg(lineNumber_)
                             .arg(track.staff)
                             .arg(track.voice)
                             .arg(currentTick_));
                }
                continue;
            }
            if (cell.startsWith('*') || cell.startsWith('=') || cell.startsWith('!'))
                fail("A musical record contains a structural token.");
            if (cell.size() > 32768)
                fail("A musical field exceeds its length limit.");
            if (!spine.hasClef)
                fail("A clef is required before the first note on each staff.");
            if (spine.endTick != currentTick_)
            {
                const auto &track = result_.tracks[spine.trackIndex];
                const auto conflict = QString("A new note overlaps or skips the active voice clock: staff %1, "
                                              "voice %2, record tick %3, voice end %4, token '%5'.")
                                          .arg(track.staff)
                                          .arg(track.voice)
                                          .arg(currentTick_)
                                          .arg(spine.endTick)
                                          .arg(cell.left(128));
                if (!options_.reviewRhythmConflicts)
                    fail(conflict);
                warn(QString("Bekern line %1: OMR rhythm review: %2 Original event ends and new reciprocal "
                             "durations remain unchanged.")
                         .arg(lineNumber_)
                         .arg(conflict));
            }
            const auto notes = cell.split(' ', Qt::SkipEmptyParts);
            if (notes.size() > 128)
                fail("A chord exceeds 128 notes.");
            std::optional<Duration> chordDuration;
            bool chordRest = false;
            for (qsizetype index = 0; index < notes.size(); ++index)
            {
                Duration parsed;
                auto event = noteEvent(notes[index], chordDuration, parsed);
                if (chordDuration && parsed.ticks != chordDuration->ticks)
                    fail("Chord tones have different durations.");
                if ((event.rest || chordRest) && notes.size() > 1)
                    fail("A chord cannot contain a rest.");
                chordDuration = parsed;
                chordRest = event.rest;
                event.chord = index > 0;
                hasPitchedNote_ |= !event.rest;
                measureLastEventEnd_ = std::max(measureLastEventEnd_, event.endTick);
                result_.tracks[spine.trackIndex].events.push_back(std::move(event));
                if (++eventCount_ > EventLimit)
                    fail("Event count exceeds 100000.");
            }
            spine.endTick = currentTick_ + chordDuration->ticks;
            if (options_.reviewRhythmConflicts)
                reviewActiveEnds_.insert(spine.endTick);
            newEvent = true;
        }
        if (!newEvent && !options_.reviewRhythmConflicts)
            fail("A record containing only null tokens cannot advance the musical clock.");
        if (options_.reviewRhythmConflicts)
        {
            while (!reviewActiveEnds_.empty() && *reviewActiveEnds_.begin() <= currentTick_)
                reviewActiveEnds_.erase(reviewActiveEnds_.begin());
            if (reviewActiveEnds_.empty())
                fail("A record has no active original duration with which to advance the OMR review clock.");
            if (!newEvent)
                warn(QString("Bekern line %1: OMR rhythm review: an all-null record advances only to the next "
                             "active original event end.")
                         .arg(lineNumber_));
            currentTick_ = *reviewActiveEnds_.begin();
            return;
        }
        currentTick_ = std::min_element(spines_.begin(), spines_.end(), [](const Spine &left, const Spine &right)
                                        { return left.endTick < right.endTick; })
                           ->endTick;
    }

    void readGraceAnnotations(const QStringList &cells)
    {
        if (!options_.preserveGraceAsReviewAnnotation)
            fail("Durationless grace notes require explicit review-annotation mode.");
        for (qsizetype column = 0; column < cells.size(); ++column)
        {
            const auto &cell = cells[column];
            if (cell == ".")
                continue;
            if (cell.size() > 32768)
                fail("A grace-annotation field exceeds its length limit.");
            if (!spines_[static_cast<std::size_t>(column)].hasClef)
                fail("A grace annotation requires an explicit clef.");
            const auto notes = cell.split(' ', Qt::SkipEmptyParts);
            if (notes.size() > 128)
                fail("A grace chord exceeds 128 notes.");
            for (auto token : notes)
            {
                if (!token.contains('q') || token.count('q') > 2)
                    fail("A grace record is mixed with ordinary timed notes.");
                token.remove('q');
                Duration printedDuration;
                const auto annotation = noteEvent(token, Duration{}, printedDuration);
                if (annotation.rest || annotation.tieStart || annotation.tieStop)
                    fail("A durationless grace annotation cannot be a rest or a sound tie.");
            }
            warn(QString("Bekern line %1: Durationless grace annotation '%2' is retained in source notation only, "
                         "not timed playback; review it against the original page.")
                     .arg(lineNumber_)
                     .arg(cell.left(128)));
        }
    }

    void closeMeasure()
    {
        if (options_.reviewRhythmConflicts)
        {
            const auto actualBoundary = std::max(currentTick_, measureLastEventEnd_);
            if (currentTick_ != actualBoundary ||
                std::any_of(spines_.begin(), spines_.end(),
                            [&](const Spine &spine) { return spine.endTick != actualBoundary; }))
                warn(QString("Bekern line %1: OMR rhythm review: the barline uses actual event-end boundary %2; "
                             "source note ends and durations are not padded or shortened.")
                         .arg(lineNumber_)
                         .arg(actualBoundary));
            currentTick_ = actualBoundary;
            // Barline synchronization resets only record clocks, never source event timestamps.
            for (auto &spine : spines_)
                spine.endTick = currentTick_;
            reviewActiveEnds_.clear();
        }
        if (currentTick_ == measureStart_)
            return;
        for (const auto &spine : spines_)
            if (spine.endTick != currentTick_)
                fail("A barline crosses a pending note or rest.");
        auto &measures = result_.parts.front().measures;
        if (measures.size() >= MeasureLimit)
            fail("Measure count exceeds 10000.");
        const auto nominal = attributes_.beats * (TicksPerQuarter * 4LL / attributes_.beatUnit);
        const auto extent = currentTick_ - measureStart_;
        const bool pickup = measures.empty() && extent < nominal;
        if (!pickup && extent != nominal && !options_.reviewRhythmConflicts)
            fail("A written measure's reciprocal durations do not match its time signature.");
        if (extent != nominal && options_.reviewRhythmConflicts)
            warn(QString("Bekern line %1: OMR rhythm review: measure %2 retains actual duration %3 ticks, while "
                         "%4/%5 expects %6; no rhythmic padding or correction is applied.")
                     .arg(lineNumber_)
                     .arg(measureNumber_)
                     .arg(extent)
                     .arg(attributes_.beats)
                     .arg(attributes_.beatUnit)
                     .arg(nominal));
        measures.push_back({measureNumber_, measureStart_, currentTick_, pickup, pageIndex_});
        measureStart_ = currentTick_;
        measureLastEventEnd_ = currentTick_;
        measureNumber_ = QString::number(measures.size() + 1);
    }

    void readBarline(const QStringList &cells)
    {
        if (!std::all_of(cells.begin(), cells.end(), [](const QString &cell) { return cell.startsWith('='); }))
            fail("A barline does not appear on every active spine.");
        const auto style = cells.front();
        const auto repeatMarks = [](const QString &token)
        {
            return std::pair{token.contains(":|") || token.contains(":!"),
                             token.endsWith("|:") || token.endsWith("!:")};
        };
        for (const auto &cell : cells)
        {
            if (!QRegularExpression("^={1,2}[0-9]*[ab]?-?[|!:;]*$").match(cell).hasMatch())
                fail("Unsupported barline: " + cell);
            if (repeatMarks(cell) != repeatMarks(style))
                fail("Repeat boundaries disagree across spines.");
        }
        closeMeasure();
        auto &part = result_.parts.front();
        const int boundary = static_cast<int>(part.measures.size());
        const bool backward = style.contains(":|") || style.contains(":!");
        const bool forward = style.endsWith("|:") || style.endsWith("!:");
        if (backward)
        {
            const int first = repeatStart_.value_or(0);
            if (boundary <= first || (!part.repeats.empty() && first < part.repeats.back().endMeasure))
                fail("Repeat boundaries are empty or overlap.");
            part.repeats.push_back({first, boundary, 2});
            repeatStart_.reset();
        }
        if (forward)
        {
            if (repeatStart_)
                fail("Nested repeats are not supported.");
            repeatStart_ = boundary;
        }
        const auto label = QRegularExpression("^=+([0-9]+)").match(cells.front());
        if (label.hasMatch())
            measureNumber_ = label.captured(1);
        lastRecordWasBarline_ = true;
    }

    MusicXmlImportResult &result_;
    const BekernDecodeOptions &options_;
    MusicXmlAttributes attributes_;
    std::vector<Spine> spines_;
    std::multiset<std::int64_t> reviewActiveEnds_;
    std::optional<int> repeatStart_;
    int nextVoice_ = 1;
    int lineNumber_ = 0;
    int eventCount_ = 0;
    int pageIndex_ = 0;
    std::int64_t currentTick_ = 0;
    std::int64_t measureStart_ = 0;
    std::int64_t measureLastEventEnd_ = 0;
    QString measureNumber_ = "1";
    bool hasMeter_ = false;
    bool hasKeySignature_ = false;
    bool keySignatureInherited_ = false;
    bool sawHeader_ = false;
    bool terminated_ = false;
    bool hasPitchedNote_ = false;
    bool lastRecordWasBarline_ = false;
};
} // namespace

MusicXmlImportResult decodeBekern(const QString &notation, const QString &sourceName,
                                  const BekernDecodeOptions &options)
{
    MusicXmlImportResult result;
    result.sourcePath = sourceName;
    result.title = QFileInfo(sourceName).completeBaseName();
    if (result.title.isEmpty())
        result.title = "Recognized score";
    if (notation.isEmpty() || notation.size() > TextLimit)
    {
        result.error = "Bekern must contain between 1 character and 16 MiB of text.";
        return result;
    }
    result.sourceSha256 = QCryptographicHash::hash(notation.toUtf8(), QCryptographicHash::Sha256).toHex();
    try
    {
        BekernReader reader(result, options);
        reader.read(recordsFromTokens(notation));
    }
    catch (const std::exception &error)
    {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}
} // namespace singlilt

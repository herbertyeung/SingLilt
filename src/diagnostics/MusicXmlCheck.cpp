// MusicXML import, timing, and voice-preservation regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MusicXmlCheck.h"
#include "domain/Timeline.h"
#include "storage/MusicXmlImporter.h"
#include "storage/ProjectStore.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(("Read MusicXML fixture failed: " + path).toStdString());
    return file.readAll();
}

QString sampleDirectory()
{
    const auto deployed = QApplication::applicationDirPath() + "/assets/staff-samples";
    return QFileInfo::exists(deployed) ? deployed : QDir("assets/staff-samples").absolutePath();
}

QByteArray singlePart(const QByteArray &measureContent, const QByteArray &attributes = {})
{
    const QByteArray standard =
        "<divisions>12</divisions><key><fifths>0</fifths><mode>major</mode></key>"
        "<time><beats>4</beats><beat-type>4</beat-type></time><clef><sign>G</sign><line>2</line></clef>";
    return "<score-partwise version=\"4.0\"><part-list><score-part id=\"P1\"><part-name>Test</part-name>"
           "</score-part></part-list><part id=\"P1\"><measure number=\"1\"><attributes>" +
           (attributes.isEmpty() ? standard : attributes) + "</attributes>" + measureContent +
           "</measure></part></score-partwise>";
}

QByteArray pitchNote(const QByteArray &step = "C", const QByteArray &alter = "0", const QByteArray &octave = "4",
                     const QByteArray &duration = "48")
{
    return "<note><pitch><step>" + step + "</step><alter>" + alter + "</alter><octave>" + octave +
           "</octave></pitch><duration>" + duration + "</duration><voice>1</voice></note>";
}
} // namespace

QJsonObject checkMusicXmlImport(const QString &fixtureDirectory)
{
    QJsonArray checks;
    QJsonObject metrics;
    bool passed = true;
    const auto check = [&](const QString &name, bool valid)
    {
        checks.append(QJsonObject{{"name", name}, {"passed", valid}});
        passed &= valid;
    };
    const auto rejects = [&](const QString &name, const QByteArray &xml)
    {
        const auto imported = parseMusicXml(xml);
        check(name, !imported.valid() && !imported.error.isEmpty());
    };
    const auto conversionRejects = [&](const QString &name, const QByteArray &xml)
    {
        const auto imported = parseMusicXml(xml);
        const auto converted = musicXmlTrackToScore(imported, 0);
        check(name, imported.valid() && !converted.valid() && !converted.error.isEmpty());
    };
    try
    {
        const auto samplePath = sampleDirectory() + "/singlilt-melody.musicxml";
        const auto melodyBytes = readBytes(samplePath);
        const auto beforeHash = QCryptographicHash::hash(melodyBytes, QCryptographicHash::Sha256).toHex();
        const auto imported = importMusicXml(samplePath);
        check("Uncompressed MusicXML with an external DOCTYPE imports without resolving the DTD",
              imported.valid());
        check("Source digest identifies the exact original XML", imported.sourceSha256 == beforeHash);
        check("Import never writes the source", readBytes(samplePath) == melodyBytes);
        check("Title and explicit part/staff/voice are preserved",
              imported.title == "SingLilt Morning Phrase" && imported.parts.size() == 1 &&
                  imported.tracks.size() == 1 && imported.tracks[0].staff == 1 && imported.tracks[0].voice == "1");
        if (!imported.valid())
            throw std::runtime_error(imported.error.toStdString());
        const auto &events = imported.tracks[0].events;
        if (events.size() != 9)
            throw std::runtime_error("MusicXML original melody must retain all 9 source events");
        check("Original notes and rests retain source-order timing",
              events.size() == 9 && events[0].startTick == 0 && events[0].endTick == 720 &&
                  events[1].startTick == 720 && events[1].endTick == 960);
        check("Key signature is preserved, not double-applied to pitch alter",
              imported.parts[0].attributes[0].fifths == 2 && events[0].midiPitch == 66 && events[0].step == "F" &&
                  events[0].alter == 1);
        check("Augmentation dots and two lyric verses survive parsing",
              events[0].dots == 1 && events[0].type == "quarter" && events[0].lyrics.size() == 2 &&
                  events[0].lyrics[0].text == "Sing" && events[0].lyrics[1].text == "Light");
        check("Sound ties and graphical tied marks remain distinct",
              events[2].tieStart && events[2].tiedStart && events[3].tieStop && events[3].tiedStop);
        check("Triplet durations use exact integer ticks across divisions changes",
              events[4].startTick == 1920 && events[4].endTick == 2080 && events[5].startTick == 2080 &&
                  events[6].endTick == 2400 && events[4].actualNotes == 3 && events[4].normalNotes == 2);
        check("Rest retains one quarter of silence", events[7].rest && events[7].midiPitch == -1 &&
                                                         events[7].startTick == 2400 && events[7].endTick == 2880);
        const auto converted = musicXmlTrackToScore(imported, 0);
        check("Explicit selected melody converts into the existing Score", converted.valid());
        if (!converted.valid())
            throw std::runtime_error(converted.error.toStdString());
        const auto &score = *converted.score;
        const auto timeline = buildTimeline(score);
        if (!timeline.valid() || timeline.events.size() != 18)
            throw std::runtime_error("MusicXML repeated melody must produce 18 valid playback events");
        check("Tempo means quarter notes per minute", score.bpm == 96 && timeline.bpm == 96);
        check("Selected score retains written enharmonic spelling",
              score.notes[0].staffSpelling && score.notes[0].staffSpelling->step == 'F' &&
                  score.notes[0].staffSpelling->alter == 1 && score.notes[0].staffSpelling->octave == 4);
        check("Playback preserves exact imported MIDI pitches", timeline.events[0].midiPitch == 66 &&
                                                                    timeline.events[1].midiPitch == 74 &&
                                                                    timeline.events[2].midiPitch == 69);
        check("Tie continuation does not reattack", timeline.events[2].attack && !timeline.events[3].attack);
        check("Written measure groups retain zero-based labels through single-track conversion",
              score.notes[0].measure == 0 && score.notes[3].measure == 0 && score.notes[4].measure == 1);
        check("Simple repeat retains half-open note range",
              score.repeats.size() == 1 && score.repeats[0].firstNote == 0 && score.repeats[0].endNote == 9 &&
                  score.repeats[0].count == 2);
        check("Repeated playback has the expected duration and verse pass",
              timeline.valid() && timeline.durationTicks == 7680 &&
                  std::abs(timeline.durationSeconds() - 10) < .000001 && timeline.events[9].verseIndex == 1 &&
                  timeline.events[9].lyric == "Light");
        metrics.insert("melodySourceSha256", QString::fromLatin1(beforeHash));
        metrics.insert("melodySourceNotes", static_cast<int>(score.notes.size()));
        metrics.insert("repeatedDurationTicks", QString::number(timeline.durationTicks));
        metrics.insert("repeatedDurationSeconds", timeline.durationSeconds());

        const auto voices = importMusicXml(sampleDirectory() + "/singlilt-voices.musicxml");
        check("All parts, staves, and voices remain explicit",
              voices.valid() && voices.parts.size() == 2 && voices.tracks.size() == 3);
        if (!voices.valid())
            throw std::runtime_error(voices.error.toStdString());
        check("Chord notes share onset instead of becoming sequential",
              voices.tracks[0].events.size() == 3 && voices.tracks[0].events[0].startTick == 0 &&
                  voices.tracks[0].events[1].startTick == 0 && voices.tracks[0].events[1].chord);
        check("Forward creates a real gap in the source timeline", voices.tracks[0].events[2].startTick == 960);
        check("Backup restores a second staff to tick zero",
              voices.tracks[1].staff == 2 && voices.tracks[1].voice == "2" &&
                  voices.tracks[1].events[0].startTick == 0 && voices.tracks[1].events[0].endTick == 1920);
        check("Selected chord track is rejected, never silently flattened",
              !musicXmlTrackToScore(voices, 0).valid());
        const auto bass = musicXmlTrackToScore(voices, 1);
        check("Explicit bass voice selection retains its clef and warns about other voices",
              bass.valid() && bass.clef.sign == "F" && bass.clef.staff == 2 && !bass.warnings.isEmpty());
        check("Selected bass plays at its original lower octave",
              bass.valid() && buildTimeline(*bass.score).events[0].midiPitch == 48);
        const auto solo = musicXmlTrackToScore(voices, 2);
        check("Tempo written in one part applies to other selected parts", solo.valid() && solo.score->bpm == 120);
        check("Invalid track index returns an explicit diagnostic", !musicXmlTrackToScore(voices, 99).valid());

        const auto flat = parseMusicXml(singlePart(
            pitchNote("B", "-1", "3"), "<divisions>12</divisions><key><fifths>-2</fifths><mode>minor</mode></key>"
                                       "<time><beats>4</beats><beat-type>4</beat-type></time>"
                                       "<clef><sign>F</sign><line>4</line></clef>"));
        const auto flatScore = musicXmlTrackToScore(flat, 0);
        check("Flat minor key and bass clef preserve sounding pitch",
              flatScore.valid() && flatScore.keyFifths == -2 && flatScore.minorKey &&
                  flatScore.score->tonic == 7 && buildTimeline(*flatScore.score).events[0].midiPitch == 58);
        const auto enharmonic = musicXmlTrackToScore(parseMusicXml(singlePart(pitchNote("D", "-1"))), 0);
        check("D-flat spelling survives independently from sounding C-sharp",
              enharmonic.valid() && enharmonic.score->notes[0].staffSpelling->step == 'D' &&
                  enharmonic.score->notes[0].staffSpelling->alter == -1 &&
                  buildTimeline(*enharmonic.score).events[0].midiPitch == 61);
        const auto graphicalTie = musicXmlTrackToScore(
            parseMusicXml(singlePart("<note><pitch><step>C</step><octave>4</octave></pitch><duration>24</"
                                     "duration><notations><tied type=\"start\"/></notations></note>" +
                                     pitchNote("C", "0", "4", "24"))),
            0);
        check("Graphical tied without sound tie cannot suppress a note attack",
              graphicalTie.valid() &&
                  graphicalTie.warnings.contains(
                      "A graphical tied marking without a sound tie is not merged in playback.") &&
                  buildTimeline(*graphicalTie.score).events[1].attack);
        const auto absentTempo = parseMusicXml(singlePart(pitchNote()));
        const auto absentTempoScore = musicXmlTrackToScore(absentTempo, 0);
        check("Missing numeric tempo is explicitly marked as the reviewable 90 BPM practice default",
              absentTempoScore.valid() && absentTempoScore.score->bpm == 90 &&
                  absentTempo.warnings.join('\n').contains("90 BPM practice default"));
        const auto overfull = parseMusicXml(singlePart(pitchNote("C", "0", "4", "60")));
        const auto overfullFull = musicXmlToPerformance(overfull, 0);
        check("Overfull measure is flagged for review without clipping any source event or excess time",
              overfullFull.valid() && overfull.tracks[0].events[0].endTick == 2400 &&
                  overfull.parts[0].measures[0].endTick == 2400 &&
                  overfullFull.performance->durationTicks == 2400 &&
                  overfullFull.performance->notes[0].durationTicks == 2400 &&
                  overfull.warnings.join('\n').contains("2400 ticks") &&
                  overfull.warnings.join('\n').contains("expects 1920"));
        const auto sparseLyrics = musicXmlTrackToScore(
            parseMusicXml(singlePart("<note><pitch><step>C</step><octave>4</octave></pitch><duration>48</duration>"
                                     "<lyric number=\"2\"><text>Second</text></lyric></note>")),
            0);
        check("Missing first verse stays empty instead of renumbering the second verse",
              sparseLyrics.valid() && sparseLyrics.score->notes[0].verseLyrics.size() == 2 &&
                  sparseLyrics.score->notes[0].verseLyrics[0].empty() &&
                  sparseLyrics.score->notes[0].verseLyrics[1] == "Second");
        const auto dottedMetronome = musicXmlTrackToScore(
            parseMusicXml(
                singlePart("<direction><direction-type><metronome><beat-unit>quarter</beat-unit><beat-unit-dot/"
                           "><per-minute>60</per-minute></metronome></direction-type></direction>" +
                           pitchNote())),
            0);
        check("Dotted-quarter metronome converts to quarter-note BPM",
              dottedMetronome.valid() && dottedMetronome.score->bpm == 90);
        const auto visualOffset = parseMusicXml(
            singlePart("<direction><offset>12</offset><sound tempo=\"120\"/></direction>" + pitchNote()));
        check("Direction offset defaults to visual-only, not playback timing",
              visualOffset.valid() && visualOffset.tempos.size() == 1 && visualOffset.tempos[0].startTick == 0);
        const auto audibleOffset = parseMusicXml(singlePart(
            "<direction><offset sound=\"yes\">12</offset><sound tempo=\"120\"/></direction>" + pitchNote()));
        check("Explicit sound offset retains its actual tempo tick",
              audibleOffset.valid() && audibleOffset.tempos.size() == 1 &&
                  audibleOffset.tempos[0].startTick == 480 && !musicXmlTrackToScore(audibleOffset, 0).valid());
        const auto forward =
            musicXmlTrackToScore(parseMusicXml(singlePart("<forward><duration>12</duration></forward>" +
                                                          pitchNote("C", "0", "4", "36"))),
                                 0);
        check("Selected voice's silent gap becomes an explicit rest",
              forward.valid() && forward.score->notes[0].degree == 0 &&
                  forward.score->notes[0].durationTicks == 480 && forward.score->notes[1].durationTicks == 1440 &&
                  forward.score->notes[0].measure == 0 && forward.score->notes[1].measure == 0);
        const QByteArray pickup = "<score-partwise><part-list><score-part "
                                  "id=\"P1\"><part-name>Pickup</part-name></score-part></part-list>"
                                  "<part id=\"P1\"><measure number=\"0\" "
                                  "implicit=\"yes\"><attributes><divisions>12</divisions></attributes>" +
                                  pitchNote("C", "0", "4", "12") + "</measure><measure number=\"1\">" +
                                  pitchNote() + "</measure></part></score-partwise>";
        const auto pickupScore = musicXmlTrackToScore(parseMusicXml(pickup), 0);
        check("Implicit pickup is not stretched to a full bar",
              pickupScore.valid() && pickupScore.score->notes.size() == 2 &&
                  pickupScore.score->notes[0].durationTicks == 480 && pickupScore.score->notes[0].measure == -1 &&
                  pickupScore.score->notes[1].measure == 0 && pickupScore.score->writtenMeasures[0].number == -1);

        const auto grandPath = sampleDirectory() + "/singlilt-grand-staff.musicxml";
        const auto grandBytes = readBytes(grandPath);
        const auto grand = importMusicXml(grandPath);
        const auto full = musicXmlToPerformance(grand, 0);
        check("Full grand staff imports both hands without requiring monophonic source voices", full.valid());
        if (!full.valid())
            throw std::runtime_error(full.error.toStdString());
        const auto &performance = *full.performance;
        const auto &guideScore = *full.selectedMelody.score;
        const auto sourceMeasures = sourceMeasureRanges(guideScore);
        check("Full-import practice guide and source measure ranges retain zero-based written labels",
              guideScore.notes.front().measure == 0 && guideScore.notes.back().measure == 1 &&
                  sourceMeasures.size() == 2 && sourceMeasures[0].measure == 0 && sourceMeasures[1].measure == 1 &&
                  sourceMeasures[0].startTick == 0 && sourceMeasures[0].endTick == 1920 &&
                  sourceMeasures[1].startTick == 1920 && sourceMeasures[1].endTick == 3840);
        const auto fullTimeline = buildTimeline(guideScore);
        const auto fullPlan = buildStaffPerformancePlan(guideScore, fullTimeline, performance);
        check("All 14 original pitched events survive full import", performance.notes.size() == 14);
        check("Two global staves retain the chosen primary staff",
              performance.staffCount == 2 && performance.primaryStaff == 1);
        check("Practice guide chooses the highest chord tone without deleting the lower original tone",
              guideScore.notes.size() == 4 && fullTimeline.events[0].midiPitch == 76 &&
                  std::any_of(performance.notes.begin(), performance.notes.end(),
                              [](const StaffPerformanceNote &note)
                              { return note.startTick == 0 && note.staff == 1 && note.midiPitch == 72; }));
        check("Full source timing is independent from the single-note guide",
              performance.durationTicks == 3840 &&
                  performance.timingFingerprint == staffTimingFingerprint(guideScore));
        check("Original voices retain identity inside global staves",
              std::any_of(performance.notes.begin(), performance.notes.end(), [](const StaffPerformanceNote &note)
                          { return note.staff == 2 && note.voice == "P1:2"; }));
        check("Full original sound ties retain both starts and stops",
              std::count_if(performance.notes.begin(), performance.notes.end(),
                            [](const StaffPerformanceNote &note) { return note.tieStart; }) == 3 &&
                  std::count_if(performance.notes.begin(), performance.notes.end(),
                                [](const StaffPerformanceNote &note) { return note.tieStop; }) == 3);
        check("Authoritative full plan merges tied continuations without deleting chord members",
              fullPlan.valid() && fullPlan.events.size() == 11 && fullPlan.durationTicks == 3840);
        check("Both hands attack together at tick zero",
              std::count_if(fullPlan.events.begin(), fullPlan.events.end(),
                            [](const AccompanimentEvent &event) { return event.startTick == 0; }) == 5);
        check("Left-hand tie sounds across the barline for 2400 ticks",
              std::any_of(fullPlan.events.begin(), fullPlan.events.end(),
                          [](const AccompanimentEvent &event)
                          {
                              return event.startTick == 0 && event.midiPitch == 48 &&
                                     event.durationTicks == 2400 && event.role == AccompanimentRole::Bass;
                          }));
        check("Original full-score XML remains unchanged", readBytes(grandPath) == grandBytes);
        metrics.insert("grandStaffOriginalNotes", static_cast<int>(performance.notes.size()));
        metrics.insert("grandStaffSoundingEvents", static_cast<int>(fullPlan.events.size()));
        metrics.insert("grandStaffDurationTicks", QString::number(fullPlan.durationTicks));
        auto repeatGrand = grand;
        repeatGrand.parts[0].repeats = {{0, 2, 2}};
        const auto repeatedFull = musicXmlToPerformance(repeatGrand, 0);
        const auto repeatedPlan =
            repeatedFull.valid() ? buildStaffPerformancePlan(*repeatedFull.selectedMelody.score,
                                                             buildTimeline(*repeatedFull.selectedMelody.score),
                                                             *repeatedFull.performance)
                                 : AccompanimentPlan{};
        check("Full repeat expands both hands together, not only the practice guide",
              repeatedFull.valid() && repeatedPlan.valid() && repeatedPlan.events.size() == 22 &&
                  repeatedPlan.durationTicks == 7680 && repeatedFull.performance->notes.size() == 14);
        check("More than two staves stays in parser data but produces an explicit full-display diagnostic",
              voices.tracks.size() == 3 && !musicXmlToPerformance(voices, 0).valid() &&
                  musicXmlToPerformance(voices, 0).error.contains("two staves"));
        const auto oneStaffChord = musicXmlToPerformance(
            parseMusicXml(singlePart(
                pitchNote() +
                "<note><chord/><pitch><step>E</step><octave>4</octave></pitch><duration>48</duration></note>")),
            0);
        check("One-staff chords also retain simultaneous source events",
              oneStaffChord.valid() && oneStaffChord.performance->staffCount == 1 &&
                  oneStaffChord.performance->notes.size() == 2 &&
                  oneStaffChord.performance->notes[0].startTick == oneStaffChord.performance->notes[1].startTick);
        auto longVoiceXml = singlePart(pitchNote());
        const QByteArray longPartId(200, 'A');
        const QByteArray longVoice(60, 'B');
        longVoiceXml.replace("id=\"P1\"", "id=\"" + longPartId + '"');
        longVoiceXml.replace("<voice>1</voice>", "<voice>" + longVoice + "</voice>");
        const auto longVoiceSource = parseMusicXml(longVoiceXml);
        const auto longVoiceFull = musicXmlToPerformance(longVoiceSource, 0);
        check("Legal XML with an oversized composed UTF-8 voice is diagnosed before full import",
              longVoiceSource.valid() && !longVoiceFull.valid() && longVoiceFull.error.contains("256-byte"));
        const auto variedChord = musicXmlToPerformance(
            parseMusicXml(singlePart(
                pitchNote("C", "0", "5", "24") +
                "<note><chord/><pitch><step>E</step><octave>5</octave></pitch><duration>12</duration></note>"
                "<forward><duration>24</duration></forward>")),
            0);
        check("Different chord durations form an accurate highest-note contour and necessary silent gap",
              variedChord.valid() && variedChord.selectedMelody.score->notes.size() == 3 &&
                  buildTimeline(*variedChord.selectedMelody.score).events[0].midiPitch == 76 &&
                  variedChord.selectedMelody.score->notes[0].durationTicks == 480 &&
                  buildTimeline(*variedChord.selectedMelody.score).events[1].midiPitch == 72 &&
                  variedChord.selectedMelody.score->notes[1].durationTicks == 480 &&
                  variedChord.selectedMelody.score->notes[2].degree == 0 &&
                  variedChord.performance->notes[0].durationTicks == 960);
        const auto engravedNote = [&](const QByteArray &notation, const QByteArray &duration = "3")
        {
            auto xml = pitchNote("C", "0", "4", duration);
            xml.replace("</note>", notation + "</note>");
            return xml;
        };
        const auto engravingSource = parseMusicXml(singlePart(
            engravedNote("<stem>down</stem><beam>begin</beam><beam number=\"2\">begin</beam>") +
            engravedNote(
                "<stem>up</stem><beam number=\"1\">continue</beam><beam number=\"2\">forward hook</beam>") +
            engravedNote("<stem>none</stem><beam number=\"1\">end</beam><beam number=\"2\">backward hook</beam>") +
            engravedNote("<stem>auto</stem><beam number=\"8\">continue</beam>", "12")));
        const auto engravingFull = musicXmlToPerformance(engravingSource, 0);
        check("MusicXML retains each written beam level, group state, hook and explicit stem direction",
              engravingFull.valid() && engravingFull.performance->notes.size() == 4 &&
                  engravingSource.tracks[0].events[0].beams.size() == 2 &&
                  engravingFull.performance->notes[0].beams[0].level == 1 &&
                  engravingFull.performance->notes[0].beams[0].kind == StaffBeamKind::Begin &&
                  engravingFull.performance->notes[0].stemDirection == StaffStemDirection::Down &&
                  engravingFull.performance->notes[1].beams[0].kind == StaffBeamKind::Continue &&
                  engravingFull.performance->notes[1].beams[1].kind == StaffBeamKind::ForwardHook &&
                  engravingFull.performance->notes[1].stemDirection == StaffStemDirection::Up &&
                  engravingFull.performance->notes[2].beams[0].kind == StaffBeamKind::End &&
                  engravingFull.performance->notes[2].beams[1].kind == StaffBeamKind::BackwardHook &&
                  engravingFull.performance->notes[2].stemDirection == StaffStemDirection::None &&
                  engravingFull.performance->notes[3].beams[0].level == 8 &&
                  engravingFull.performance->notes[3].stemDirection == StaffStemDirection::Auto);
        if (!engravingFull.valid())
            throw std::runtime_error(engravingFull.error.toStdString());
        check("Beams and stems never infer or rewrite sounding duration or MIDI pitch",
              engravingFull.performance->notes[0].midiPitch == 60 &&
                  engravingFull.performance->notes[0].startTick == 0 &&
                  engravingFull.performance->notes[0].durationTicks == 120 &&
                  engravingFull.performance->notes[3].startTick == 360 &&
                  engravingFull.performance->notes[3].durationTicks == 480 &&
                  engravingFull.performance->durationTicks == 1920);
        const auto engravingJson = staffPerformanceToJson(*engravingFull.performance);
        const auto restoredEngraving =
            staffPerformanceFromJson(engravingJson, *engravingFull.selectedMelody.score);
        check("Written beam and stem metadata survives full-performance storage",
              staffPerformanceToJson(restoredEngraving) == engravingJson &&
                  restoredEngraving.notes[1].beams[1].kind == StaffBeamKind::ForwardHook &&
                  restoredEngraving.notes[2].stemDirection == StaffStemDirection::None);
        auto legacyEngravingJson = engravingJson;
        auto legacyEngravingNotes = legacyEngravingJson.value("notes").toArray();
        for (int i = 0; i < legacyEngravingNotes.size(); ++i)
        {
            auto note = legacyEngravingNotes[i].toObject();
            note.remove("beams");
            note.remove("stemDirection");
            legacyEngravingNotes[i] = note;
        }
        legacyEngravingJson.insert("notes", legacyEngravingNotes);
        const auto legacyEngraving =
            staffPerformanceFromJson(legacyEngravingJson, *engravingFull.selectedMelody.score);
        check("Legacy full-performance files without beam or stem fields retain automatic engraving defaults",
              std::all_of(legacyEngraving.notes.begin(), legacyEngraving.notes.end(),
                          [](const StaffPerformanceNote &note)
                          { return note.beams.empty() && note.stemDirection == StaffStemDirection::Auto; }));
        const auto rejectsStoredEngraving = [&](const QString &name, const QJsonObject &note)
        {
            auto json = engravingJson;
            auto notes = json.value("notes").toArray();
            notes[0] = note;
            json.insert("notes", notes);
            bool rejected = false;
            try
            {
                staffPerformanceFromJson(json, *engravingFull.selectedMelody.score);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(name, rejected);
        };
        auto invalidBeamNote = engravingJson.value("notes").toArray()[0].toObject();
        invalidBeamNote.insert("beams", QJsonArray{QJsonObject{{"level", 9}, {"kind", "begin"}}});
        rejectsStoredEngraving("Stored beam levels outside 1..8 are rejected", invalidBeamNote);
        invalidBeamNote.insert("beams", QJsonArray{QJsonObject{{"level", 1.5}, {"kind", "begin"}}});
        rejectsStoredEngraving("Stored fractional beam levels are rejected", invalidBeamNote);
        invalidBeamNote.insert("beams", QJsonArray{QJsonObject{{"level", 1}, {"kind", "sideways"}}});
        rejectsStoredEngraving("Unknown stored beam kinds are rejected rather than changed to flags",
                               invalidBeamNote);
        invalidBeamNote.insert("beams", QJsonArray{QJsonObject{{"level", 1}, {"kind", "begin"}},
                                                   QJsonObject{{"level", 1}, {"kind", "end"}}});
        rejectsStoredEngraving("Duplicate stored beam levels on one note are rejected", invalidBeamNote);
        auto invalidStemNote = engravingJson.value("notes").toArray()[0].toObject();
        invalidStemNote.insert("stemDirection", "sideways");
        rejectsStoredEngraving("Unknown stored stem directions are rejected", invalidStemNote);
        invalidStemNote.insert("stemDirection", true);
        rejectsStoredEngraving("Stored stem direction must be a string", invalidStemNote);
        rejects("MusicXML beam level zero is rejected",
                singlePart(engravedNote("<beam number=\"0\">begin</beam>")));
        rejects("MusicXML beam level nine is rejected",
                singlePart(engravedNote("<beam number=\"9\">begin</beam>")));
        rejects("MusicXML duplicate beam levels are rejected",
                singlePart(engravedNote("<beam>begin</beam><beam>end</beam>")));
        rejects("MusicXML unknown beam values are rejected", singlePart(engravedNote("<beam>sideways</beam>")));
        rejects("MusicXML unknown stem values are rejected", singlePart(engravedNote("<stem>sideways</stem>")));
        rejects("MusicXML double stems are explicitly diagnosed, not replaced by automatic stems",
                singlePart(engravedNote("<stem>double</stem>")));
        const auto unclosedFull = musicXmlToPerformance(
            parseMusicXml(singlePart("<note><pitch><step>C</step><octave>4</octave></pitch><duration>48</"
                                     "duration><tie type=\"start\"/></note>")),
            0);
        check("Unclosed ties in any full-performance voice produce an explicit diagnostic",
              !unclosedFull.valid() && unclosedFull.error.contains("tie start"));
        const auto tiedNote = [](const QByteArray &ties, bool chord)
        {
            return QByteArray("<note>") + (chord ? QByteArray("<chord/>") : QByteArray{}) +
                   "<pitch><step>C</step><octave>4</octave></pitch><duration>12</duration>" + ties +
                   "<voice>1</voice></note>";
        };
        const auto doubledTies = musicXmlToPerformance(
            parseMusicXml(
                singlePart(tiedNote("<tie type=\"start\"/>", false) + tiedNote("<tie type=\"start\"/>", true) +
                           tiedNote("<tie type=\"stop\"/>", false) + tiedNote("<tie type=\"stop\"/>", true))),
            0);
        const auto doubledPlan = doubledTies.valid()
                                     ? buildStaffPerformancePlan(*doubledTies.selectedMelody.score,
                                                                 buildTimeline(*doubledTies.selectedMelody.score),
                                                                 *doubledTies.performance)
                                     : AccompanimentPlan{};
        check("Simultaneous unison ties retain both complete chains instead of overwriting pending starts",
              doubledTies.valid() && doubledTies.performance->notes.size() == 4 && doubledPlan.valid() &&
                  doubledPlan.events.size() == 2 && doubledPlan.events[0].durationTicks == 960 &&
                  doubledPlan.events[1].durationTicks == 960 &&
                  std::none_of(doubledTies.performance->notes.begin(), doubledTies.performance->notes.end(),
                               [](const StaffPerformanceNote &note) { return note.unresolvedSoundTie; }));
        const auto orphanXml = parseMusicXml(singlePart(tiedNote("<tie type=\"stop\"/>", false)));
        const auto orphanStrict = musicXmlToPerformance(orphanXml, 0);
        const auto orphanReview = musicXmlToPerformance(orphanXml, 0, true);
        const auto orphanPlan = orphanReview.valid()
                                    ? buildStaffPerformancePlan(*orphanReview.selectedMelody.score,
                                                                buildTimeline(*orphanReview.selectedMelody.score),
                                                                *orphanReview.performance)
                                    : AccompanimentPlan{};
        check("Explicit review mode retains an orphan tie mark and original note duration while strict import "
              "rejects it",
              !orphanStrict.valid() && orphanReview.valid() && orphanReview.performance->notes.size() == 1 &&
                  orphanReview.performance->notes[0].tieStop &&
                  orphanReview.performance->notes[0].unresolvedSoundTie && orphanPlan.valid() &&
                  orphanPlan.events.size() == 1 && orphanPlan.events[0].durationTicks == 480 &&
                  orphanReview.warnings.join('\n').contains("measure 1, staff 1, voice 1, pitch C4"));
        const auto unclosedChainXml =
            parseMusicXml(singlePart(tiedNote("<tie type=\"start\"/>", false) +
                                     tiedNote("<tie type=\"stop\"/><tie type=\"start\"/>", false)));
        const auto chainReview = musicXmlToPerformance(unclosedChainXml, 0, true);
        const auto chainPlan = chainReview.valid()
                                   ? buildStaffPerformancePlan(*chainReview.selectedMelody.score,
                                                               buildTimeline(*chainReview.selectedMelody.score),
                                                               *chainReview.performance)
                                   : AccompanimentPlan{};
        check("An unresolved chain marks every linked source event, retains raw flags, and disables only "
              "guide/runtime merging",
              !musicXmlToPerformance(unclosedChainXml, 0).valid() && chainReview.valid() && chainPlan.valid() &&
                  chainPlan.events.size() == 2 && chainReview.performance->notes[0].tieStart &&
                  chainReview.performance->notes[1].tieStart && chainReview.performance->notes[1].tieStop &&
                  std::all_of(chainReview.performance->notes.begin(), chainReview.performance->notes.end(),
                              [](const StaffPerformanceNote &note) { return note.unresolvedSoundTie; }) &&
                  !chainReview.selectedMelody.score->notes[0].tieToNext);
        const auto gapXml = parseMusicXml(singlePart(tiedNote("<tie type=\"start\"/>", false) +
                                                     "<forward><duration>3</duration></forward>" +
                                                     tiedNote("<tie type=\"stop\"/>", false)));
        const auto gapReview = musicXmlToPerformance(gapXml, 0, true);
        check("Reviewing a 120-tick tie gap never inserts a note, shifts a start, or stretches a duration",
              !musicXmlToPerformance(gapXml, 0).valid() && gapReview.valid() &&
                  gapReview.performance->notes.size() == 2 && gapReview.performance->notes[0].startTick == 0 &&
                  gapReview.performance->notes[0].durationTicks == 480 &&
                  gapReview.performance->notes[1].startTick == 600 &&
                  gapReview.performance->notes[1].durationTicks == 480 &&
                  gapReview.performance->notes[0].tieStart && gapReview.performance->notes[1].tieStop &&
                  gapReview.performance->notes[0].unresolvedSoundTie &&
                  gapReview.performance->notes[1].unresolvedSoundTie);
        auto shiftedMeasures = grand;
        shiftedMeasures.parts.push_back(shiftedMeasures.parts[0]);
        shiftedMeasures.parts.back().id = "P2";
        shiftedMeasures.parts.back().staffCount = 1;
        shiftedMeasures.parts[0].staffCount = 1;
        shiftedMeasures.tracks[1].partIndex = 1;
        shiftedMeasures.tracks[1].staff = 1;
        shiftedMeasures.parts[1].measures[0].endTick = 1800;
        const auto shiftedFull = musicXmlToPerformance(shiftedMeasures, 0);
        check("Mismatched part measure boundaries never receive guessed synchronization",
              !shiftedFull.valid() && shiftedFull.error.contains("measure boundaries"));

        const auto pageNote = [](const QByteArray &step, const QByteArray &alter, const QByteArray &octave,
                                 int duration, int staff, int voice)
        {
            return "<note><pitch><step>" + step + "</step><alter>" + alter + "</alter><octave>" + octave +
                   "</octave></pitch><duration>" + QByteArray::number(duration) + "</duration><voice>" +
                   QByteArray::number(voice) + "</voice><staff>" + QByteArray::number(staff) + "</staff></note>";
        };
        const auto pageBar = [&](int number, bool newPage, const QByteArray &attributes, int duration,
                                 const QByteArray &step, const QByteArray &alter, const QByteArray &octave)
        {
            return "<measure number=\"" + QByteArray::number(number) + "\">" +
                   (newPage ? QByteArray("<print new-page=\"yes\"/>") : QByteArray{}) + attributes +
                   pageNote(step, alter, octave, duration, 1, 1) + "<backup><duration>" +
                   QByteArray::number(duration) + "</duration></backup>" +
                   pageNote("E", "0", "2", duration, 2, 2) + "</measure>";
        };
        const QByteArray pageHeader = "<attributes><divisions>12</divisions><key><fifths>4</fifths></key>"
                                      "<time><beats>4</beats><beat-type>4</beat-type></time><staves>2</staves>"
                                      "<clef number=\"1\"><sign>G</sign><line>2</line></clef>"
                                      "<clef number=\"2\"><sign>F</sign><line>4</line></clef></attributes>"
                                      "<direction><sound tempo=\"62\"/></direction>";
        const QByteArray pagesXml =
            "<score-partwise><part-list><score-part id=\"P1\"><part-name>Four pages</part-name></score-part>"
            "</part-list><part id=\"P1\">" +
            pageBar(1, true, pageHeader, 48, "E", "0", "5") +
            pageBar(16, true,
                    "<attributes><time><beats>2</beats><beat-type>4</beat-type></time>"
                    "<clef number=\"1\"><sign>F</sign><line>4</line></clef></attributes>",
                    24, "C", "1", "3") +
            pageBar(36, true,
                    "<attributes><time><beats>4</beats><beat-type>4</beat-type></time>"
                    "<clef number=\"1\"><sign>G</sign><line>2</line></clef></attributes>",
                    48, "E", "0", "5") +
            pageBar(56, true, {}, 48, "D", "1", "5") +
            pageBar(58, false, "<attributes><time><beats>2</beats><beat-type>4</beat-type></time></attributes>",
                    24, "G", "1", "4") +
            "</part></score-partwise>";
        const auto pagesSource = parseMusicXml(pagesXml);
        const auto pagesFull = musicXmlToPerformance(pagesSource, 0);
        check("Four physical MusicXML pages retain all voices with standard G/F changes and local meter",
              pagesFull.valid() && pagesFull.performance->notes.size() == 10);
        if (!pagesFull.valid())
            throw std::runtime_error(pagesFull.error.toStdString());
        const auto &pageScore = *pagesFull.selectedMelody.score;
        check("First new-page print stays on page zero; later breaks create independent page IDs",
              pageScore.writtenMeasures.size() == 5 && pageScore.writtenMeasures[0].pageIndex == 0 &&
                  pageScore.writtenMeasures[1].pageIndex == 1 && pageScore.writtenMeasures[2].pageIndex == 2 &&
                  pageScore.writtenMeasures[3].pageIndex == 3 && pageScore.writtenMeasures[4].pageIndex == 3 &&
                  pageScore.notes[1].pageIndex == 1 && pagesFull.performance->notes[2].pageIndex == 1);
        check("Printed page-start measure numbers are retained instead of renumbering each page from one",
              pageScore.writtenMeasures[0].number == 0 && pageScore.writtenMeasures[1].number == 15 &&
                  pageScore.writtenMeasures[2].number == 35 && pageScore.writtenMeasures[3].number == 55 &&
                  pageScore.writtenMeasures[4].number == 57 && pageScore.notes[1].measure == 15);
        check("Local 2/4 measure lengths stay at 960 ticks without 4/4 padding",
              pageScore.writtenMeasures[1].durationTicks == 960 && pageScore.writtenMeasures[1].beatsPerBar == 2 &&
                  pageScore.writtenMeasures[2].startTick == 2880 &&
                  pageScore.writtenMeasures[4].beatsPerBar == 2 && pagesFull.performance->durationTicks == 7680);
        check("Clef metadata changes do not recalculate absolute MIDI pitches",
              std::any_of(pagesFull.performance->clefChanges.begin(), pagesFull.performance->clefChanges.end(),
                          [](const StaffClefChange &change)
                          { return change.staff == 1 && change.startTick == 1920 && change.bassClef; }) &&
                  std::any_of(pagesFull.performance->notes.begin(), pagesFull.performance->notes.end(),
                              [](const StaffPerformanceNote &note)
                              { return note.staff == 1 && note.startTick == 1920 && note.midiPitch == 49; }));
        const auto pageTimeline = buildTimeline(pageScore);
        check("Metronome strong beats use the real changing-meter source boundaries",
              pageTimeline.valid() && pageTimeline.metronomeBeats.size() == 16 &&
                  std::any_of(pageTimeline.metronomeBeats.begin(), pageTimeline.metronomeBeats.end(),
                              [](const MetronomeBeat &beat) { return beat.startTick == 2880 && beat.downbeat; }));
        auto storedPageScore = pageScore;
        for (auto &note : storedPageScore.notes)
            note.source = {1, 1, 12, 12};
        const auto pageJson = scoreToJson(storedPageScore);
        const auto restoredPageScore = scoreFromJson(pageJson);
        check("Page IDs and authoritative written meter spans roundtrip through score storage",
              scoreToJson(restoredPageScore) == pageJson && restoredPageScore.notes[3].pageIndex == 3 &&
                  restoredPageScore.writtenMeasures[1].beatUnit == 4);
        auto overlappingPageJson = pageJson;
        auto badMeasures = overlappingPageJson.value("writtenMeasures").toArray();
        auto badMeasure = badMeasures[1].toObject();
        badMeasure.insert("startTick", 1919);
        badMeasures[1] = badMeasure;
        overlappingPageJson.insert("writtenMeasures", badMeasures);
        bool badSpanRejected = false;
        try
        {
            scoreFromJson(overlappingPageJson);
        }
        catch (const std::exception &)
        {
            badSpanRejected = true;
        }
        check("Score storage rejects overlapping authoritative source spans", badSpanRejected);
        auto fractionalPageJson = pageJson;
        auto badNotes = fractionalPageJson.value("notes").toArray();
        auto badNote = badNotes[1].toObject();
        badNote.insert("pageIndex", 1.5);
        badNotes[1] = badNote;
        fractionalPageJson.insert("notes", badNotes);
        bool badPageRejected = false;
        try
        {
            scoreFromJson(fractionalPageJson);
        }
        catch (const std::exception &)
        {
            badPageRejected = true;
        }
        check("Score storage rejects fractional note page identifiers", badPageRejected);
        auto crossPageXml = grandBytes;
        crossPageXml.replace("<measure number=\"2\">", "<measure number=\"2\"><print new-page=\"yes\"/>");
        const auto crossPageFull = musicXmlToPerformance(parseMusicXml(crossPageXml), 0);
        const auto crossPagePlan =
            crossPageFull.valid() ? buildStaffPerformancePlan(*crossPageFull.selectedMelody.score,
                                                              buildTimeline(*crossPageFull.selectedMelody.score),
                                                              *crossPageFull.performance)
                                  : AccompanimentPlan{};
        check("Cross-page sound ties retain both written events without adding an attack",
              crossPageFull.valid() && crossPagePlan.valid() && crossPagePlan.events.size() == 11 &&
                  std::any_of(crossPageFull.performance->notes.begin(), crossPageFull.performance->notes.end(),
                              [](const StaffPerformanceNote &note)
                              { return note.tieStop && note.pageIndex == 1; }));

        const QByteArray reorderedClefs =
            "<score-partwise><part-list><score-part id=\"P1\"><part-name>Midbar</part-name></score-part>"
            "</part-list><part id=\"P1\"><measure number=\"1\">" +
            pageHeader + pageNote("E", "0", "5", 24, 1, 1) +
            "<attributes><clef number=\"1\"><sign>F</sign><line>4</line></clef></attributes>" +
            pageNote("C", "1", "3", 24, 1, 1) +
            "<backup><duration>48</duration></backup>"
            "<attributes><clef number=\"2\"><sign>F</sign><line>4</line></clef></attributes>" +
            pageNote("E", "0", "2", 48, 2, 2) + "</measure></part></score-partwise>";
        const auto reorderedFull = musicXmlToPerformance(parseMusicXml(reorderedClefs), 0);
        check("Backup and a later-encoded staff attribute cannot move a future clef change to tick zero",
              reorderedFull.valid() &&
                  std::any_of(reorderedFull.performance->clefChanges.begin(),
                              reorderedFull.performance->clefChanges.end(), [](const StaffClefChange &change)
                              { return change.staff == 1 && change.startTick == 0 && !change.bassClef; }) &&
                  std::any_of(reorderedFull.performance->clefChanges.begin(),
                              reorderedFull.performance->clefChanges.end(), [](const StaffClefChange &change)
                              { return change.staff == 1 && change.startTick == 960 && change.bassClef; }));

        rejects("Malformed XML is rejected", "<score-partwise>");
        rejects("Timewise XML requires an explicit export conversion", "<score-timewise/>");
        rejects("Empty source is rejected", {});
        rejects("Custom entities are rejected before expansion",
                "<!DOCTYPE score-partwise [<!ENTITY x 'expanded'>]><score-partwise>&x;</score-partwise>");
        rejects("Missing divisions cannot invent duration units",
                singlePart(pitchNote(), "<key><fifths>0</fifths></key>"));
        rejects("Negative note duration is rejected", singlePart(pitchNote("C", "0", "4", "-1")));
        rejects("64-bit duration overflow is rejected",
                singlePart(pitchNote("C", "0", "4", "9223372036854775807")));
        rejects("Out-of-MIDI-range pitch is rejected", singlePart(pitchNote("B", "0", "9")));
        rejects("Fractional pitch alters require microtone playback", singlePart(pitchNote("C", ".5")));
        rejects("Sub-tick durations are not rounded silently",
                singlePart(pitchNote("C", "0", "4", "1"), "<divisions>7</divisions>"));
        rejects("Backup cannot cross the measure start",
                singlePart("<backup><duration>12</duration></backup>" + pitchNote()));
        rejects(
            "Chord requires a prior note",
            singlePart(
                "<note><chord/><pitch><step>C</step><octave>4</octave></pitch><duration>48</duration></note>"));
        rejects("Grace notes require an explicit duration policy",
                singlePart("<note><grace/><pitch><step>C</step><octave>4</octave></pitch></note>"));
        rejects(
            "Transposing instruments are diagnosed rather than played at written pitch",
            singlePart(pitchNote(), "<divisions>12</divisions><transpose><chromatic>-2</chromatic></transpose>"));
        rejects("Nested repeats are explicitly diagnosed",
                singlePart("<barline location=\"left\"><repeat direction=\"forward\"/></barline>"
                           "<barline location=\"left\"><repeat direction=\"forward\"/></barline>" +
                           pitchNote()));
        conversionRejects(
            "Tempo map changes never become a constant-tempo false success",
            singlePart("<direction><sound tempo=\"90\"/></direction>" + pitchNote("C", "0", "4", "24") +
                       "<direction><sound tempo=\"120\"/></direction>" + pitchNote("D", "0", "4", "24")));
        conversionRejects(
            "Mid-measure time-signature changes remain an explicit unsupported span",
            singlePart(pitchNote("C", "0", "4", "24") +
                       "<attributes><time><beats>3</beats><beat-type>4</beat-type></time></attributes>" +
                       pitchNote("D", "0", "4", "12")));
        conversionRejects("Key changes retain source metadata but block fixed-key display",
                          singlePart(pitchNote("C", "0", "4", "24") +
                                     "<attributes><key><fifths>1</fifths></key></attributes>" +
                                     pitchNote("D", "0", "4", "24")));
        conversionRejects("Alternate endings block conversion instead of playing the wrong form",
                          singlePart(pitchNote() + "<barline><ending number=\"1\" type=\"start\"/></barline>"));
        conversionRejects("Unmatched sound tie is diagnosed",
                          singlePart("<note><pitch><step>C</step><octave>4</octave></pitch><duration>48</"
                                     "duration><tie type=\"start\"/></note>"));

        if (!QDir().mkpath(fixtureDirectory))
            throw std::runtime_error("MusicXML diagnostic directory could not be created");
        QTemporaryDir temporary(fixtureDirectory + "/musicxml-XXXXXX");
        if (!temporary.isValid())
            throw std::runtime_error("MusicXML temporary fixture could not be created");
        const auto roundtrip = [&](const QString &name, const QByteArray &xml, const QString &fileName)
        {
            const auto source = parseMusicXml(xml);
            const auto full = musicXmlToPerformance(source, 0);
            if (!source.valid() || !full.valid())
                throw std::runtime_error((name + ": " + source.error + full.error).toStdString());
            Project candidate;
            candidate.score = *full.selectedMelody.score;
            candidate.staffPerformance = full.performance;
            candidate.generatedNotation = true;
            candidate.notationStyle = NotationStyle::Staff;
            candidate.image = QImage(32, 32, QImage::Format_RGB32);
            candidate.image.fill(Qt::white);
            // These fixtures test music limits, not generated image layout.
            for (auto &note : candidate.score.notes)
            {
                note.hasImageAnchor = false;
                note.source = {};
            }
            for (auto &note : candidate.staffPerformance->notes)
            {
                note.hasImageAnchor = false;
                note.source = {};
            }
            const auto path = temporary.filePath(fileName);
            saveProject(path, candidate);
            const auto restored = loadProject(path);
            check(name, buildTimeline(restored.score).valid() &&
                            scoreToJson(restored.score) == scoreToJson(candidate.score) &&
                            staffPerformanceToJson(*restored.staffPerformance) ==
                                staffPerformanceToJson(*candidate.staffPerformance));
            return restored;
        };
        const auto tempoBoundary =
            roundtrip("MusicXML at 420 BPM saves and reopens without clamping",
                      singlePart("<direction><sound tempo=\"420\"/></direction>" + pitchNote()), "tempo420.jpp");
        roundtrip("MusicXML 3/1 meter saves and reopens without changing its clock",
                  singlePart(pitchNote("C", "0", "4", "144"),
                             "<divisions>12</divisions><time><beats>3</beats><beat-type>1</beat-type></time>"
                             "<clef><sign>G</sign><line>2</line></clef>"),
                  "meter3-1.jpp");
        roundtrip("MusicXML single notes longer than 64 quarters retain their duration through storage",
                  singlePart(pitchNote("C", "0", "4", "780")), "duration65q.jpp");
        const QByteArray tickAttributes =
            "<divisions>480</divisions><time><beats>4</beats><beat-type>4</beat-type></time>"
            "<clef><sign>G</sign><line>2</line></clef>";
        const auto longGuide =
            roundtrip("A sequential 10001-note practice guide and complete staff both roundtrip",
                      singlePart(pitchNote("C", "0", "4", "1").repeated(10001), tickAttributes), "guide10001.jpp");
        check("Guide and complete staff counts are independently retained at 10001",
              longGuide.score.notes.size() == 10001 && longGuide.staffPerformance->notes.size() == 10001);
        auto chordTone = pitchNote("C", "0", "4", "1920");
        chordTone.replace("<note>", "<note><chord/>");
        const auto largeChord =
            roundtrip("A 10001-note complete staff with a one-note guide is not capped by the guide collection",
                      singlePart(pitchNote("C", "0", "4", "1920") + chordTone.repeated(10000), tickAttributes),
                      "staff10001-guide1.jpp");
        check("Saving a large chord preserves every simultaneous source event",
              largeChord.score.notes.size() == 1 && largeChord.staffPerformance->notes.size() == 10001);
        const auto storageRejects = [&](const Score &score)
        {
            try
            {
                scoreFromJson(scoreToJson(score));
                return false;
            }
            catch (const std::exception &)
            {
                return true;
            }
        };
        auto limitScore = tempoBoundary.score;
        limitScore.bpm = MinimumScoreBpm;
        bool tempoEdges = buildTimeline(scoreFromJson(scoreToJson(limitScore))).valid();
        limitScore.bpm = MaximumScoreBpm;
        tempoEdges &= buildTimeline(scoreFromJson(scoreToJson(limitScore))).valid();
        limitScore.bpm = MinimumScoreBpm - 0.1;
        tempoEdges &= !buildTimeline(limitScore).valid() && storageRejects(limitScore);
        limitScore.bpm = MaximumScoreBpm + 1;
        check("Storage and playback share inclusive tempo bounds and reject values outside them",
              tempoEdges && !buildTimeline(limitScore).valid() && storageRejects(limitScore));
        limitScore = tempoBoundary.score;
        limitScore.beatsPerBar = MaximumBeatsPerBar;
        limitScore.beatUnit = MaximumBeatUnit;
        bool meterEdges = buildTimeline(scoreFromJson(scoreToJson(limitScore))).valid();
        limitScore.beatsPerBar = MaximumBeatsPerBar + 1;
        meterEdges &= !buildTimeline(limitScore).valid() && storageRejects(limitScore);
        limitScore.beatsPerBar = 4;
        limitScore.beatUnit = MaximumBeatUnit * 2;
        meterEdges &= !buildTimeline(limitScore).valid() && storageRejects(limitScore);
        limitScore.beatUnit = 3;
        check("Storage and playback share meter bounds and still reject non-power-of-two denominators",
              meterEdges && !buildTimeline(limitScore).valid() && storageRejects(limitScore));
        limitScore = tempoBoundary.score;
        limitScore.writtenMeasures.clear();
        limitScore.notes.front().durationTicks = MaximumNoteDurationTicks;
        const bool durationEdge = buildTimeline(scoreFromJson(scoreToJson(limitScore))).valid();
        ++limitScore.notes.front().durationTicks;
        check("Storage and playback accept the maximum note duration and reject one tick beyond it",
              durationEdge && !buildTimeline(limitScore).valid() && storageRejects(limitScore));
        limitScore = tempoBoundary.score;
        limitScore.writtenMeasures.clear();
        limitScore.notes.front().durationTicks = 1;
        const auto limitNote = limitScore.notes.front();
        limitScore.notes.assign(MaximumScoreNotes, limitNote);
        const bool countEdge = buildTimeline(scoreFromJson(scoreToJson(limitScore))).valid();
        limitScore.notes.push_back(limitNote);
        check("Storage and playback accept the maximum Score note count and reject one more",
              countEdge && !buildTimeline(limitScore).valid() && storageRejects(limitScore));
        const auto coverageRejects = [&](const StaffPerformance &performance)
        {
            try
            {
                staffPerformanceFromJson(staffPerformanceToJson(performance), tempoBoundary.score);
                return false;
            }
            catch (const std::exception &)
            {
                return true;
            }
        };
        auto splitCoverage = *tempoBoundary.staffPerformance;
        splitCoverage.notes.front().durationTicks /= 2;
        auto continuation = splitCoverage.notes.front();
        continuation.startTick = continuation.durationTicks;
        continuation.sourceNoteIndex = -1;
        splitCoverage.notes.push_back(continuation);
        bool splitRejected = coverageRejects(splitCoverage);
        --splitCoverage.notes.front().durationTicks;
        check("Coverage validation does not merge adjacent notes or bridge their gaps to cover one guide note",
              splitRejected && coverageRejects(splitCoverage));
        auto wrongPitchCoverage = *tempoBoundary.staffPerformance;
        ++wrongPitchCoverage.notes.front().midiPitch;
        wrongPitchCoverage.notes.front().staffSpelling.reset();
        check("Coverage validation still requires the primary guide pitch", coverageRejects(wrongPitchCoverage));
        auto overlappingCoverage = *tempoBoundary.staffPerformance;
        auto shorter = overlappingCoverage.notes.front();
        shorter.startTick = 480;
        shorter.durationTicks = 480;
        shorter.sourceNoteIndex = -1;
        overlappingCoverage.notes.insert(overlappingCoverage.notes.begin(), shorter);
        check("Coverage indexing accepts unsorted overlapping notes when one actual note spans the guide",
              staffPerformanceFromJson(staffPerformanceToJson(overlappingCoverage), tempoBoundary.score)
                      .notes.size() == 2);
        const auto oversizedPath = temporary.filePath("oversized.musicxml");
        QFile oversized(oversizedPath);
        if (!oversized.open(QIODevice::WriteOnly) || !oversized.resize(16 * 1024 * 1024 + 1))
            throw std::runtime_error("MusicXML oversized fixture could not be created");
        oversized.close();
        check("Input file size is bounded before parsing", !importMusicXml(oversizedPath).valid());
        check("Compressed mxl gives a dependency diagnostic",
              !importMusicXml(temporary.filePath("score.mxl")).valid() &&
                  importMusicXml(temporary.filePath("score.mxl")).error.contains("ZIP"));
        check("Missing source reports a read error", !importMusicXml(temporary.filePath("missing.xml")).valid());
        const QByteArray deep = QByteArray("<a>").repeated(65) + QByteArray("</a>").repeated(65);
        rejects("XML nesting is bounded", deep);
    }
    catch (const std::exception &error)
    {
        check(QString::fromUtf8(error.what()), false);
    }
    return {{"passed", passed},
            {"checks", checks},
            {"metrics", metrics},
            {"sourceModified", false},
            {"nativeMusicXmlParser", true},
            {"audioHardwareTested", false}};
}

void runMusicXmlCheck(const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(0, &app,
                       [&args, &app]
                       {
                           const auto reportPath = args.value("report");
                           const auto result = checkMusicXmlImport(QFileInfo(reportPath).absolutePath());
                           QFile output(reportPath);
                           const auto bytes = QJsonDocument(result).toJson();
                           if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
                               app.exit(4);
                           else
                               app.exit(result.value("passed").toBool() ? 0 : 3);
                       });
}
} // namespace singlilt

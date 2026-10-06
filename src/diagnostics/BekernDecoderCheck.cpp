// kern/bekern decoding and timing-validation regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "BekernDecoderCheck.h"

#include "domain/Timeline.h"
#include "recognition/BekernDecoder.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <algorithm>
#include <stdexcept>

namespace singlilt
{
QJsonObject checkBekernDecoder()
{
    QJsonArray checks;
    bool passed = true;
    const auto check = [&](const QString &name, bool condition)
    {
        checks.append(QJsonObject{{"name", name}, {"passed", condition}});
        passed &= condition;
    };
    const auto rejects = [&](const QString &name, const QString &text)
    {
        const auto decoded = decodeBekern(text);
        check(name, !decoded.valid() && !decoded.error.isEmpty());
    };
    try
    {
        const QString grand = "**kern\t**kern\n*clefF4\t*clefG2\n*k[f#]\t*k[f#]\n*M4/4\t*M4/4\n"
                              "*MM96\t*MM96\n=1\t=1\n2C\t4c 4e\n.\t4d\n2G\t2g\n==\t==\n*-\t*-\n";
        const auto decoded = decodeBekern(grand, "grand-staff.bekern");
        check("Standard kern headers decode into explicit part/staff/voice events", decoded.valid());
        if (!decoded.valid())
            throw std::runtime_error(decoded.error.toStdString());
        check("Humdrum bottom-to-top order maps to bass staff 2 and treble staff 1",
              decoded.parts.size() == 1 && decoded.parts[0].staffCount == 2 && decoded.tracks.size() == 2 &&
                  decoded.tracks[0].staff == 2 && decoded.tracks[1].staff == 1);
        const auto &bass = decoded.tracks[0].events;
        const auto &treble = decoded.tracks[1].events;
        check("Held bass, null tokens and chord tones share one exact musical clock",
              bass.size() == 2 && treble.size() == 4 && bass[0].startTick == 0 && bass[0].endTick == 960 &&
                  bass[1].startTick == 960 && treble[0].startTick == 0 && treble[1].startTick == 0 &&
                  treble[1].chord && treble[2].startTick == 480 && treble[3].startTick == 960);
        check("Kern pitch spelling is absolute rather than receiving the key signature twice",
              decoded.parts[0].attributes[0].fifths == 1 && treble[3].step == "G" && treble[3].midiPitch == 67 &&
                  bass[0].midiPitch == 48);
        const auto converted = musicXmlToPerformance(decoded, 1, true);
        check("Decoded events enter the existing full-performance conversion without writing MusicXML",
              converted.valid() && converted.performance->notes.size() == 6 &&
                  converted.performance->staffCount == 2 && converted.selectedMelody.score->bpm == 96);
        if (!converted.valid())
            throw std::runtime_error(converted.error.toStdString());
        const auto timeline = buildTimeline(*converted.selectedMelody.score);
        const auto performance =
            buildStaffPerformancePlan(*converted.selectedMelody.score, timeline, *converted.performance);
        check("The existing sounding plan retains simultaneous chord tones and both hands",
              performance.valid() && performance.durationTicks == 1920 &&
                  std::count_if(performance.events.begin(), performance.events.end(),
                                [](const AccompanimentEvent &event) { return event.startTick == 0; }) == 3);

        const QString tokens = "<bos> *clefF4 <t> *clefG2 <b> *k[f#] <t> *k[f#] <b> *M4/4 <t> *M4/4 <b> "
                               "*MM96 <t> *MM96 <b> =1 <t> =1 <b> 2 C <t> 4 c <s> 4 e <b> . <t> 4 d <b> "
                               "2 G <t> 2 g <b> == <t> == <b> *- <t> *- <b> <eos>";
        const auto tokenized = decodeBekern(tokens);
        check("SMT token spaces are joined, while only <s> creates a chord boundary",
              tokenized.valid() && tokenized.tracks[0].events.size() == bass.size() &&
                  tokenized.tracks[1].events.size() == treble.size() && tokenized.tracks[1].events[1].chord &&
                  tokenized.tracks[1].events[2].startTick == 480);
        const auto extended = decodeBekern("**ekern\n*clefG2\n*M1/4\n4@c@#\n==\n*-\n");
        check("Extended kern component separators preserve enharmonic spelling",
              extended.valid() && extended.tracks[0].events[0].midiPitch == 61 &&
                  extended.tracks[0].events[0].step == "C" && extended.tracks[0].events[0].alter == 1);
        const auto grandStaffHeader = decodeBekern("**ekern_1.0\n*clefG2\n*M1/4\n4·c·#\n==\n*-\n");
        const auto unknownHeader = decodeBekern("**ekern_2.0\n*clefG2\n*M1/4\n4c\n==\n*-\n");
        check("GrandStaff's exact ekern_1.0 header accepts extended tokens without accepting unknown versions",
              grandStaffHeader.valid() && grandStaffHeader.tracks[0].events[0].midiPitch == 61 &&
                  !unknownHeader.valid() && unknownHeader.error.contains("Unsupported interpretation"));

        const auto voices = decodeBekern("*clefF4\t*clefG2\n*M4/4\t*M4/4\n2C\t4c\n*\t*^\n.\t8d\t4g\n.\t8e\t.\n"
                                         "*\t*v\t*v\n2G\t2f\n==\t==\n*-\t*-\n");
        check("Split and merge retain every voice without flattening or duplicating a held note",
              voices.valid() && voices.tracks.size() == 3 && voices.tracks[2].events.size() == 1 &&
                  voices.tracks[2].events[0].startTick == 480 && voices.tracks[2].events[0].endTick == 960 &&
                  voices.tracks[1].events.size() == 4 && voices.tracks[1].events.back().startTick == 960);
        const auto voicePerformance = musicXmlToPerformance(voices, 1, true);
        check("The complete multi-voice output passes current downstream validation",
              voicePerformance.valid() && voicePerformance.performance->notes.size() == 7);

        const auto rhythm = decodeBekern("*clefG2\n*M4/4\n=|:\n[4c\n4c]\n12d\n12e\n12f\n4r\n=:|!\n*-\n");
        check("Ties, exact triplet durations, rests and ordinary repeats remain explicit",
              rhythm.valid() && rhythm.tracks[0].events.size() == 6 && rhythm.tracks[0].events[0].tieStart &&
                  rhythm.tracks[0].events[1].tieStop &&
                  rhythm.tracks[0].events[2].endTick - rhythm.tracks[0].events[2].startTick == 160 &&
                  rhythm.tracks[0].events[2].actualNotes == 3 && rhythm.tracks[0].events[2].normalNotes == 2 &&
                  rhythm.tracks[0].events[5].rest && rhythm.parts[0].repeats.size() == 1);
        const auto tied = musicXmlToPerformance(rhythm, 0);
        const auto tiedTimeline = tied.valid() ? buildTimeline(*tied.selectedMelody.score) : Timeline{};
        check("Sound ties and repeats enter the existing timeline without reattacking a tied continuation",
              tied.valid() && tiedTimeline.valid() && tiedTimeline.durationTicks == 3840 &&
                  tiedTimeline.events.size() > 1 && !tiedTimeline.events[1].attack);
        const auto dotted = decodeBekern("*clefG2\n*M3/4\n4.c-\n8d\n4en\n==\n*-\n");
        check("Dotted duration, flat and explicit natural preserve sound and notation",
              dotted.valid() && dotted.tracks[0].events[0].endTick == 720 &&
                  dotted.tracks[0].events[0].dots == 1 && dotted.tracks[0].events[0].alter == -1 &&
                  dotted.tracks[0].events[2].alter == 0);
        const auto pages = decodeBekern("*clefG2\n*M1/4\n4c\n=2\n!!LO:PB:g=z\n4d\n==\n*-\n");
        check("Explicit physical page breaks preserve measure and note page identities",
              pages.valid() && pages.parts[0].measures.size() == 2 && pages.parts[0].measures[1].pageIndex == 1 &&
                  pages.tracks[0].events[1].pageIndex == 1);
        const auto slur = decodeBekern("*clefG2\n*M2/4\n(4c\n4d)\n==\n*-\n");
        check("Slurs are reported as unsupported playback expression rather than invented sound ties",
              slur.valid() && !slur.warnings.empty() && !slur.tracks[0].events[0].tieStart);

        BekernDecodeOptions fragmentOptions;
        fragmentOptions.pageFragment = true;
        fragmentOptions.preserveGraceAsReviewAnnotation = true;
        const QString fragmentText = "*clefF4\t*clefG2\n*M1/4\t*M1/4\n.\tcc#q\n4C\t4c[\n=\t=||\n";
        const auto fragment = decodeBekern(fragmentText, "page-fragment.krn", fragmentOptions);
        check("Explicit page-fragment mode keeps a grace annotation in source without inventing playback time",
              fragment.valid() && fragment.tracks[0].events.size() == 1 && fragment.tracks[1].events.size() == 1 &&
                  fragment.tracks[1].events[0].startTick == 0 && fragment.tracks[1].events[0].endTick == 480 &&
                  fragment.warnings.join('\n').contains("Durationless grace annotation 'cc#q'") &&
                  fragment.warnings.join('\n').contains("page fragment"));
        const auto reviewedFragment = musicXmlToPerformance(fragment, 1, true);
        check("A page-end open sound tie is retained as an unresolved review chain",
              reviewedFragment.valid() &&
                  std::any_of(reviewedFragment.performance->notes.begin(),
                              reviewedFragment.performance->notes.end(),
                              [](const StaffPerformanceNote &note) { return note.unresolvedSoundTie; }));
        rejects("Strict mode still rejects the same grace/page fragment", fragmentText);
        const auto incompleteFragment = decodeBekern("*clefG2\n*M1/4\n4c\n", {}, fragmentOptions);
        check("Page-fragment mode requires a final complete barline, not merely a finished note",
              !incompleteFragment.valid() && !incompleteFragment.error.isEmpty());
        const auto shortMeasure = decodeBekern("*clefG2\n*M4/4\n1c\n=\n4d\n=\n", {}, fragmentOptions);
        check("Page-fragment mode still rejects a later incomplete written measure",
              !shortMeasure.valid() && shortMeasure.error.contains("time signature"));
        const auto overlappingVoices =
            decodeBekern("*clefF4\t*clefG2\n*M4/4\t*M4/4\n8C\t8r\n8E\t8e\n8A\t16.e\n.\t32e\n"
                         "8E\t32e\n.\t32g\n8G\t8r\n=\t=\n",
                         {}, fragmentOptions);
        check("A real-model-style short upper rhythm never moves the held bass clock backward",
              !overlappingVoices.valid() && overlappingVoices.error.contains("active voice clock"));
        BekernDecodeOptions rhythmReviewOptions = fragmentOptions;
        rhythmReviewOptions.reviewRhythmConflicts = true;
        const auto reviewedRhythm =
            decodeBekern("*clefF4\t*clefG2\n*M4/4\t*M4/4\n8C\t8r\n8E\t8e\n8A\t16.e\n.\t32e\n"
                         "8E\t32e\n.\t32g\n8G\t8r\n=\t=\n",
                         {}, rhythmReviewOptions);
        const auto reviewedRhythmPerformance = musicXmlToPerformance(reviewedRhythm, 1, true);
        check("Explicit OMR clock review preserves every pitch/duration and the original overlapping ends",
              reviewedRhythm.valid() && reviewedRhythm.tracks[0].events.size() == 5 &&
                  reviewedRhythm.tracks[1].events.size() == 7 &&
                  reviewedRhythm.tracks[0].events[3].startTick == 720 &&
                  reviewedRhythm.tracks[0].events[3].endTick == 960 &&
                  reviewedRhythm.tracks[0].events[4].startTick == 840 &&
                  reviewedRhythm.tracks[0].events[4].endTick == 1080 &&
                  reviewedRhythm.tracks[0].events[4].midiPitch == 55 &&
                  reviewedRhythm.tracks[1].events[4].endTick - reviewedRhythm.tracks[1].events[4].startTick ==
                      60 &&
                  reviewedRhythm.warnings.join('\n').contains("OMR review clock interpretation") &&
                  reviewedRhythmPerformance.valid() && reviewedRhythmPerformance.performance->notes.size() == 10);
        const auto actualShortMeasure = decodeBekern("*clefG2\n*M4/4\n1c\n=\n4d\n=\n", {}, rhythmReviewOptions);
        const auto actualShortPerformance = musicXmlToPerformance(actualShortMeasure, 0, true);
        check("OMR clock review retains actual short later-measure duration without adding rest events",
              actualShortMeasure.valid() && actualShortMeasure.parts[0].measures.size() == 2 &&
                  actualShortMeasure.parts[0].measures[1].endTick -
                          actualShortMeasure.parts[0].measures[1].startTick ==
                      480 &&
                  actualShortMeasure.tracks[0].events.size() == 2 &&
                  actualShortMeasure.tracks[0].events[1].endTick == 2400 &&
                  actualShortMeasure.warnings.join('\n').contains("no rhythmic padding") &&
                  actualShortPerformance.valid() && actualShortPerformance.performance->durationTicks == 2400);
        const auto unfinishedReview = decodeBekern("*clefG2\n*M1/4\n4c\n", {}, rhythmReviewOptions);
        check("OMR clock review still rejects a stream without a closed barline or termination record",
              !unfinishedReview.valid() && unfinishedReview.error.contains("Missing *-"));
        const auto unknownReview = decodeBekern("*clefG2\n*M1/4\n*>A\n4c\n=\n", {}, rhythmReviewOptions);
        check("OMR clock review does not swallow unknown structural navigation",
              !unknownReview.valid() && unknownReview.error.contains("Unsupported interpretation"));
        const auto keyMode = decodeBekern("*clefG2\n*M1/4\n*f#:\n4f#\n==\n*-\n");
        check("An explicit minor key center supplies its consistent three-sharp signature",
              keyMode.valid() && keyMode.parts[0].attributes[0].fifths == 3 &&
                  keyMode.parts[0].attributes[0].mode == "minor");
        rejects("A conflicting key center is not silently discarded", "*clefG2\n*M1/4\n*k[]\n*D:\n4d\n==\n*-\n");

        BekernDecodeOptions successorOptions = fragmentOptions;
        MusicXmlAttributes precedingAttributes;
        precedingAttributes.startTick = 4320;
        precedingAttributes.measure = 8;
        precedingAttributes.beats = 3;
        precedingAttributes.beatUnit = 4;
        precedingAttributes.fifths = 4;
        successorOptions.initialAttributes = precedingAttributes;
        const QString successorText = "*clefG2\n4c#\n4d#\n4e\n=\n";
        const auto successor = decodeBekern(successorText, "system-2.krn", successorOptions);
        const auto successorPerformance = musicXmlToPerformance(successor, 0, true);
        check("A later system inherits explicit validated meter/key context on its own zero-based clock",
              successor.valid() && successorPerformance.valid() &&
                  successor.parts[0].attributes[0].startTick == 0 &&
                  successor.parts[0].attributes[0].measure == 0 && successor.parts[0].attributes[0].fifths == 4 &&
                  successor.parts[0].measures[0].endTick == 1440);
        const auto missingContext = decodeBekern(successorText, "system-2.krn", fragmentOptions);
        check("The same later-system input without explicit context still rejects its missing meter",
              !missingContext.valid() && missingContext.error.contains("time signature"));
        const auto changedContext = decodeBekern("*clefF4\n*M2/4\n*k[]\n4C\n4D\n=\n", {}, successorOptions);
        check("Actual later-system clef/key/time declarations override inherited attributes",
              changedContext.valid() && changedContext.parts[0].attributes[0].beats == 2 &&
                  changedContext.parts[0].attributes[0].fifths == 0 &&
                  changedContext.parts[0].attributes[0].clefs[0].sign == "F" &&
                  changedContext.parts[0].measures[0].endTick == 960);

        rejects("The observed note-free SMT result with inconsistent meters is rejected",
                "*clefF4 <t> *clefG2 <b> *k[] <t> *k[] <b> *M3/4 <t> *M9/8 <b> *- <t> *- <b>");
        rejects("A note-free stream is not a successful score", "*clefG2\n*M4/4\n*-\n");
        rejects("Missing termination detects a truncated prediction", "*clefG2\n*M1/4\n4c\n==\n");
        rejects("Unknown structural interpretations are never ignored", "*clefG2\n*M1/4\n*>A\n4c\n*-\n");
        rejects("Unknown note semantics are never silently dropped", "*clefG2\n*M1/4\n4c@unknown\n*-\n");
        rejects("Grace notes do not receive invented rhythmic playback", "*clefG2\n*M1/4\n8cq\n*-\n");
        rejects("Non-integral tick rhythms are rejected instead of rounded", "*clefG2\n*M1/4\n7c\n*-\n");
        rejects("Record width must match active spines", "*clefF4\t*clefG2\n*M1/4\t*M1/4\n4C\n*-\t*-\n");
        rejects("A null token must sustain a real pending event", "*clefG2\n*M1/4\n.\n*-\n");
        rejects("A chord must use one shared duration", "*clefG2\n*M1/4\n4c 8e\n*-\n");
        rejects("A barline cannot cut through a held voice",
                "*clefF4\t*clefG2\n*M4/4\t*M4/4\n1C\t4c\n=\t=\n*-\t*-\n");
        rejects("A singleton merge is malformed", "*clefG2\n*M1/4\n*v\n4c\n*-\n");
        rejects("A stream after termination is not accepted", "*clefG2\n*M1/4\n4c\n*-\n4d\n");
        rejects("Mid-measure meter changes do not rewrite preceding events",
                "*clefG2\n*M4/4\n4c\n*M3/4\n4d\n*-\n");
    }
    catch (const std::exception &error)
    {
        check(QString::fromUtf8(error.what()), false);
    }
    return {{"passed", passed},
            {"checks", checks},
            {"validationScope", "Native C++ bekern decoding and existing full-performance conversion"},
            {"realModelAccuracyTested", false}};
}

void runBekernDecoderCheck(const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(0, &app,
                       [&args, &app]
                       {
                           const auto reportPath = args.value("report");
                           QDir().mkpath(QFileInfo(reportPath).absolutePath());
                           const auto report = checkBekernDecoder();
                           const auto bytes = QJsonDocument(report).toJson();
                           QFile output(reportPath);
                           if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
                               app.exit(4);
                           else
                               app.exit(report.value("passed").toBool() ? 0 : 3);
                       });
}
} // namespace singlilt

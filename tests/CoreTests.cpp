// Regression tests for score, timeline, and accompaniment behavior.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "SingingTests.h"
#include "NumberedPerformanceTests.h"
#include "domain/Accompaniment.h"
#include "domain/AccompanimentTimeline.h"
#include "domain/Score.h"
#include "domain/Timeline.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

void check(bool result, const std::string &message)
{
    if (!result)
        throw std::runtime_error(message);
}

void near(double actual, double expected, const std::string &message)
{
    check(std::abs(actual - expected) < 0.000001, message);
}

singlilt::Note note(int degree, int ticks = singlilt::TicksPerQuarter)
{
    singlilt::Note result;
    result.degree = degree;
    result.durationTicks = ticks;
    return result;
}

void pitchAndTransposition()
{
    using namespace singlilt;
    auto n = note(1);
    check(midiPitch(n, 0) == 60, "C tonic must be C4");
    check(midiPitch(n, 1) == 61, "C-sharp tonic must be C-sharp4");
    n.degree = 7;
    check(midiPitch(n, 0) == 71, "Major degree 7 must be B4");
    n.octave = -1;
    n.accidental = -1;
    check(midiPitch(n, 0, 2) == 60, "Octave, accidental and transpose must compose");
    check(midiPitch(note(0), 0) == -1, "Rest must be silent");
    check(midiPitch(note(9), 0) == -1, "Invalid degree must not index scale table");
    n.octave = std::numeric_limits<int>::max();
    check(midiPitch(n, 0) == -1, "Huge octave must not overflow");

    Score score;
    score.tonic = 1;
    score.notes = {note(1), note(1), note(5)};
    score.notes[1].keyOverride = 2;
    auto timeline = buildTimeline(score, -1);
    check(timeline.valid(), "Key-change score must be valid");
    check(timeline.events[0].midiPitch == 60 && timeline.events[1].midiPitch == 61 &&
              timeline.events[2].midiPitch == 68,
          "Key override must persist forward");
}

void durationsAndMeter()
{
    using namespace singlilt;
    Score score;
    score.bpm = 60;
    score.notes = {note(1, 720), note(0, 240), note(2, 960)};
    auto timeline = buildTimeline(score);
    check(timeline.valid() && timeline.durationTicks == 1920, "Tick durations must remain exact");
    near(timeline.durationSeconds(), 4.0, "Quarter=60 must last four seconds");
    check(timeline.events[1].startTick == 720 && timeline.events[1].midiPitch == -1,
          "Rest must consume time without a pitch");
    check(ticksPerBar(score) == 1920 && ticksPerMetronomeBeat(score) == 480, "4/4 pulse");
    score.beatsPerBar = 6;
    score.beatUnit = 8;
    check(ticksPerBar(score) == 1440 && ticksPerMetronomeBeat(score) == 720, "6/8 dotted-quarter pulse");
    score.beatsPerBar = 3;
    check(ticksPerBar(score) == 720 && ticksPerMetronomeBeat(score) == 240, "3/8 is simple meter");
    score.beatsPerBar = 12;
    check(ticksPerBar(score) == 2880 && ticksPerMetronomeBeat(score) == 720, "12/8 compound pulse");
    score.beatUnit = 3;
    check(ticksPerBar(score) == 0 && ticksPerMetronomeBeat(score) == 0, "Reject non-binary denominator");
}

void repeatsAndEndings()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(1), note(2), note(3), note(4), note(5)};
    score.repeats = {{1, 4, 2, 3}};
    auto timeline = buildTimeline(score);
    check(timeline.valid(), "First/second-ending score must be valid");
    const std::vector<std::size_t> expected{0, 1, 2, 3, 1, 2, 4};
    check(timeline.events.size() == expected.size(), "Repeat expansion count");
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(timeline.events[i].sourceNoteIndex == expected[i], "Repeat must retain original source index");

    // A modulation near a repeat's end must not leak into its repeated beginning.
    score.notes[2].keyOverride = 2;
    timeline = buildTimeline(score);
    check(timeline.events[1].midiPitch == timeline.events[4].midiPitch,
          "Repeat must restore the source key context");
    check(timeline.events[2].midiPitch == timeline.events[5].midiPitch,
          "Repeated key change must be deterministic");

    score.repeats = {{0, 2, 3, -1}, {3, 5, 2, -1}};
    timeline = buildTimeline(score);
    check(timeline.valid() && timeline.events.size() == 11, "Multiple separate repeats");
    score.repeats = {{0, 4, 2, -1}, {1, 2, 2, -1}};
    check(!buildTimeline(score).valid(), "Nested repeat must produce explicit error rather than loop");
    score.repeats = {{0, 6, 2, -1}};
    check(!buildTimeline(score).valid(), "Repeat endpoint must be in range");
    score.repeats = {{0, 2, 100, -1}};
    check(!buildTimeline(score).valid(), "Repeat count must be bounded");
    score.repeats = {{0, 4, 2, 4}};
    check(!buildTimeline(score).valid(), "First ending must lie inside repeat interval");
}

void ties()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(3, 480), note(3, 240), note(3, 240), note(4)};
    score.notes[0].tieToNext = true;
    score.notes[1].tieToNext = true;
    auto timeline = buildTimeline(score);
    check(timeline.valid() && timeline.events.size() == 4, "Ties retain cursor segments");
    check(timeline.events[0].attack && !timeline.events[1].attack && !timeline.events[2].attack &&
              timeline.events[3].attack,
          "Tied notes must not retrigger");
    check(timeline.durationTicks == 1440, "Ties retain exact duration");
    score.notes[2].tieToNext = true;
    timeline = buildTimeline(score);
    check(timeline.valid() && !timeline.diagnostics.empty() && timeline.events[3].attack,
          "Pitch-mismatched tie must warn and retrigger");
    score.notes.back().tieToNext = true;
    check(buildTimeline(score).diagnostics.size() == 2, "Unfinished terminal tie must warn");
}

void validation()
{
    using namespace singlilt;
    Score score;
    check(!buildTimeline(score).valid(), "Empty score cannot play");
    score.notes = {note(1)};
    score.bpm = std::numeric_limits<double>::quiet_NaN();
    check(!buildTimeline(score).valid(), "NaN tempo must fail");
    score.bpm = 90;
    score.notes[0].durationTicks = 0;
    auto timeline = buildTimeline(score);
    check(!timeline.valid() && timeline.events.empty(), "Invalid score must not produce partial playback");
    score.notes[0] = note(8);
    check(!buildTimeline(score).valid(), "Invalid degree must fail");
    score.notes[0] = note(1);
    score.notes[0].keyOverride = 12;
    check(!buildTimeline(score).valid(), "Invalid key override must fail");
    score.notes[0] = note(1);
    check(!buildTimeline(score, 100).valid(), "Out-of-MIDI-range transpose must fail");
    score.notes[0].confidence = 1.1;
    check(!buildTimeline(score).valid(), "Invalid confidence must fail");
    score.notes[0] = note(1);
    score.notes[0].source.width = -1;
    check(!buildTimeline(score).valid(), "Malformed source rectangle must fail");
    score.notes.assign(7000, note(1));
    score.repeats = {{0, 7000, 16, -1}};
    check(!buildTimeline(score).valid(), "Expanded event count must remain bounded");
}

void seekBounds()
{
    using namespace singlilt;
    Score score;
    score.bpm = 120;
    score.notes = {note(1), note(2), note(0)};
    const auto timeline = buildTimeline(score);
    check(timeline.clampTick(-3) == 0 && timeline.clampTick(9999) == 1440, "Seek clamps to song bounds");
    check(timeline.tickAtSeconds(-1) == 0 && timeline.tickAtSeconds(200) == 1440, "Seconds seek bounds");
    check(timeline.tickAtSeconds(std::numeric_limits<double>::quiet_NaN()) == 0, "NaN seek is start");
    check(timeline.tickAtSeconds(std::numeric_limits<double>::infinity()) == 1440, "Infinite seek is end");
    check(timeline.tickAtSeconds(0.5) == 480, "Seconds map to exact boundary");
    check(timeline.eventIndexAtTick(0) == 0 && timeline.eventIndexAtTick(479) == 0 &&
              timeline.eventIndexAtTick(480) == 1,
          "Cursor switches at note boundary");
    check(!timeline.eventIndexAtTick(-1) && !timeline.eventIndexAtTick(1440), "No active note outside timeline");
    near(timeline.secondsAtTick(-1), 0, "Negative tick clamps");
    near(timeline.secondsAtTick(9999), 1.5, "Past-end tick clamps");
}

void verseLyricsAndMigration()
{
    using namespace singlilt;
    auto n = note(1);
    n.lyric = "legacy A\r\nlegacy B";
    check(lyricVerses(n) == std::vector<std::string>{"legacy A", "legacy B"}, "Migrate Windows newline verses");
    check(lyricForVerse(n, 1) == "legacy B", "Legacy B lyric must be selected");
    n.lyric = "/B/";
    check(lyricVerses(n) == std::vector<std::string>{"", "B", ""}, "Slash migration retains empty endpoints");
    n.lyric = "A/\nC";
    check(lyricVerses(n) == std::vector<std::string>{"A", "", "C"}, "Mixed delimiters retain blank B");
    n.verseLyrics = {"explicit A", ""};
    check(lyricForVerse(n, 0) == "explicit A" && lyricForVerse(n, 1).empty(), "Explicit blank B stays blank");
    check(lyricForVerse(n, 2).empty(), "Missing third verse must not repeat A");
    n.verseLyrics = {"", "explicit B"};
    check(lyricForVerse(n, 0).empty() && lyricForVerse(n, 1) == "explicit B", "Blank A does not shift B");
    n.verseLyrics = {"shared"};
    check(lyricForVerse(n, 0) == "shared" && lyricForVerse(n, 15) == "shared", "Singleton shared on every pass");
    n.verseLyrics = {""};
    check(lyricForVerse(n, 0).empty() && lyricForVerse(n, 1).empty(),
          "Explicit empty singleton hides legacy text");
    check(lyricForVerse(n, -1).empty(), "Negative verse lookup is empty");
    Score score;
    check(programForVerse(score, 0) == 0 && programForVerse(score, 1) == 4, "Default A/B programs");
    check(programForVerse(score, 15) == 4 && programForVerse(score, -1) == 0, "Program lookup stays bounded");
}

void repeatVersesAndSeeking()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(1), note(2), note(3), note(4)};
    score.notes[0].verseLyrics = {"shared prefix"};
    score.notes[1].verseLyrics = {"A2", "B2"};
    score.notes[2].verseLyrics = {"A3", "B3"};
    score.notes[3].verseLyrics = {"tail A", "tail B"};
    score.notes[1].id = 42;
    score.notes[1].source = {123, 234, 15, 18};
    score.repeats = {{1, 3, 2, -1}};
    const auto timeline = buildTimeline(score);
    check(timeline.valid() && timeline.events.size() == 6 && timeline.durationTicks == 2880,
          "Verse annotation must not change repeated source order or duration");
    const auto &firstPass = timeline.events[1];
    const auto &secondPass = timeline.events[3];
    check(firstPass.sourceNoteIndex == secondPass.sourceNoteIndex && firstPass.midiPitch == secondPass.midiPitch &&
              firstPass.durationTicks == secondPass.durationTicks,
          "A/B events share the source, pitch and rhythm");
    check(score.notes[firstPass.sourceNoteIndex].id == 42 &&
              score.notes[secondPass.sourceNoteIndex].source.x == 123,
          "Both passes preserve original image anchor");
    check(firstPass.verseIndex == 0 && firstPass.program == 0 && firstPass.lyric == "A2", "First pass uses A");
    check(secondPass.verseIndex == 1 && secondPass.program == 4 && secondPass.lyric == "B2", "Second pass uses B");
    check(timeline.events.back().verseIndex == 1 && timeline.events.back().lyric == "tail B", "Tail retains B");
    check(timeline.events[*timeline.eventIndexAtTick(1439)].verseIndex == 0, "Before repeat boundary remains A");
    const auto boundary = timeline.eventIndexAtTick(1440);
    check(boundary && timeline.events[*boundary].verseIndex == 1 && timeline.events[*boundary].program == 4,
          "Seek at repeat boundary resolves B instrument");
    check(timeline.events[*timeline.eventIndexAtTick(1680)].lyric == "B2", "Seek inside B resolves B lyric");
    score.repeats.clear();
    const auto single = buildTimeline(score);
    for (const auto &event : single.events)
        check(event.verseIndex == 0 && event.program == 0, "No repeat remains A");
}

void verseEndingsAndCarry()
{
    using namespace singlilt;
    Score score;
    for (int i = 0; i < 8; ++i)
    {
        auto n = note(i % 7 + 1);
        n.verseLyrics = {"A" + std::to_string(i), "B" + std::to_string(i), "C" + std::to_string(i)};
        score.notes.push_back(n);
    }
    score.repeats = {{1, 4, 2, 3}, {5, 7, 3, -1}};
    const auto timeline = buildTimeline(score);
    const std::vector<std::size_t> sources{0, 1, 2, 3, 1, 2, 4, 5, 6, 5, 6, 5, 6, 7};
    const std::vector<int> verses{0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 2, 2, 2};
    check(timeline.valid() && timeline.events.size() == sources.size(), "Multiple repeat verse layout valid");
    for (std::size_t i = 0; i < sources.size(); ++i)
    {
        const auto &event = timeline.events[i];
        check(event.sourceNoteIndex == sources[i] && event.verseIndex == verses[i],
              "Ending and next-repeat verse order");
        check(event.program == (verses[i] == 0 ? 0 : 4), "Third verse reuses final configured program");
        check(event.lyric == std::string(1, static_cast<char>('A' + verses[i])) + std::to_string(sources[i]),
              "Each ending and tail uses occurrence verse lyrics");
    }
    check(timeline.events[3].lyric == "A3" && timeline.events[6].lyric == "B4", "First ending A, second ending B");
    check(timeline.events[7].verseIndex == 0, "A new repeat resets its first pass to A");
    check(timeline.events.back().verseIndex == 2, "Final tail carries last repeat pass");
}

void verseTiesAndValidation()
{
    using namespace singlilt;
    Score score;
    score.versePrograms = {73, 24};
    score.notes = {note(3), note(3), note(4)};
    score.notes[0].verseLyrics = {"A", "B"};
    score.notes[1].tieToNext = true;
    score.repeats = {{0, 2, 2, -1}};
    auto timeline = buildTimeline(score);
    check(timeline.valid() && timeline.events.size() == 5, "Cross-pass tie score must be valid");
    check(!timeline.events[2].attack && timeline.events[2].verseIndex == 1 && timeline.events[2].program == 73 &&
              timeline.events[2].lyric == "B",
          "Tied B occurrence retains sounding A timbre but uses B lyric");
    check(timeline.events[3].attack && timeline.events[3].program == 24, "Next attack switches to B timbre");
    score.notes[0].degree = 0;
    timeline = buildTimeline(score);
    check(timeline.events[2].midiPitch == -1 && timeline.events[2].program == 24,
          "A B-pass rest must not inherit the previous sounding timbre");

    score.repeats.clear();
    score.notes = {note(1)};
    for (const auto &programs : std::vector<std::vector<int>>{{}, {-1}, {128}, std::vector<int>(17, 0)})
    {
        score.versePrograms = programs;
        const auto invalid = buildTimeline(score);
        check(!invalid.valid() && invalid.events.empty(), "Invalid program configuration rejected atomically");
    }
    score.versePrograms = {127};
    check(buildTimeline(score).valid(), "Last General MIDI program is valid");
    score.notes[0].verseLyrics.assign(17, "text");
    check(!buildTimeline(score).valid(), "Explicit lyrics limited to 16 slots");
    score.notes[0].verseLyrics.clear();
    score.notes[0].lyric.assign(16, '/');
    check(!buildTimeline(score).valid(), "Legacy lyric delimiters must also respect verse limit");
    score.notes[0].verseLyrics = {"A", ""};
    check(buildTimeline(score).valid(), "Authoritative explicit lyrics ignore stale legacy field");
}

void controllableVelocity()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(1, 480), note(2, 240), note(3, 240), note(4, 480), note(0, 480)};
    const auto accented = buildTimeline(score);
    const std::vector<int> expected{96, 88, 84, 88, 0};
    check(accented.valid() && accented.durationTicks == 1920, "Velocity score retains exact bar duration");
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(accented.events[i].velocity == expected[i], "Downbeat, beat, offbeat and rest velocity");
    score.accentBeats = false;
    const auto uniform = buildTimeline(score);
    for (std::size_t i = 0; i < uniform.events.size(); ++i)
    {
        const auto &event = uniform.events[i];
        check(event.velocity == (event.midiPitch < 0 ? 0 : 88), "Disabled accents produce uniform attacks");
        check(event.startTick == accented.events[i].startTick &&
                  event.durationTicks == accented.events[i].durationTicks &&
                  event.midiPitch == accented.events[i].midiPitch &&
                  event.sourceNoteIndex == accented.events[i].sourceNoteIndex &&
                  event.verseIndex == accented.events[i].verseIndex && event.lyric == accented.events[i].lyric,
              "Dynamics must not change pitches, timestamps, source anchors or verse text");
    }
    score.accentBeats = true;
    score.baseVelocity = 127;
    auto timeline = buildTimeline(score);
    check(timeline.events[0].velocity == 127 && timeline.events[2].velocity == 123, "Upper velocity clamp");
    score.baseVelocity = 1;
    timeline = buildTimeline(score);
    check(timeline.events[0].velocity == 9 && timeline.events[2].velocity == 1, "Lower velocity clamp");
    score.baseVelocity = 88;
    score.repeats = {{0, 5, 2, -1}};
    timeline = buildTimeline(score);
    check(timeline.durationTicks == 3840, "Dynamics preserve repeat timing");
    for (std::size_t i = 0; i < 5; ++i)
        check(timeline.events[i].velocity == timeline.events[i + 5].velocity,
              "Source accents repeat deterministically");

    score.repeats.clear();
    score.notes = {note(1, 240), note(1, 240), note(2, 480), note(3, 960)};
    score.notes[0].tieToNext = true;
    timeline = buildTimeline(score);
    check(timeline.events[0].velocity == 96 && !timeline.events[1].attack && timeline.events[1].velocity == 96,
          "An offbeat tied continuation keeps the original attack strength");

    score.notes = {note(1, 240), note(2, 240), note(3), note(4), note(5), note(6)};
    for (std::size_t i = 2; i < score.notes.size(); ++i)
        score.notes[i].measure = 1;
    timeline = buildTimeline(score);
    check(timeline.events[0].velocity == 88 && timeline.events[1].velocity == 84 &&
              timeline.events[2].velocity == 96,
          "Initial short pickup avoids a false downbeat accent");

    score.beatsPerBar = 6;
    score.beatUnit = 8;
    score.notes = {note(1, 720), note(2, 240), note(3, 240), note(4, 240)};
    timeline = buildTimeline(score);
    check(timeline.events[0].velocity == 96 && timeline.events[1].velocity == 88 &&
              timeline.events[2].velocity == 84,
          "Compound meter accents follow dotted-quarter beats");
    for (const auto invalidBase : {0, 128})
    {
        score.baseVelocity = invalidBase;
        const auto invalid = buildTimeline(score);
        check(!invalid.valid() && invalid.events.empty(), "Invalid base velocity rejected atomically");
    }
}

bool accompanimentErrors(const std::vector<singlilt::Diagnostic> &diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const singlilt::Diagnostic &diagnostic)
                       { return diagnostic.severity == singlilt::DiagnosticSeverity::Error; });
}

bool accompanimentDiagnostic(const std::vector<singlilt::Diagnostic> &diagnostics, const std::string &key)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [&key](const singlilt::Diagnostic &diagnostic) { return diagnostic.message == key; });
}

void accompanimentDeterminismAndPatterns()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(1, 480), note(3, 480), note(5, 480), note(1, 480)};
    const auto originalTimeline = buildTimeline(score);
    const auto first = generateAccompaniment(score);
    const auto second = generateAccompaniment(score);
    check(first.valid() && first.chords.size() == 1, "One bar generates one harmonic suggestion");
    check(first.melodyFingerprint == second.melodyFingerprint && first.generatorVersion == 1,
          "Generation preserves a deterministic source fingerprint/version");
    check(first.chords[0].rootPitchClass == 0 && first.chords[0].quality == ChordQuality::Major,
          "C major triad melody suggests C major");
    check(first.chords[0].startTick == 0 && first.chords[0].endTick == 1920 &&
              first.chords[0].inversion == second.chords[0].inversion,
          "Generation is deterministic and half-open");
    check(!accompanimentErrors(validateAccompaniment(score, first)), "Generated arrangement validates");
    const auto block = buildAccompanimentPlan(score, originalTimeline, first);
    const auto repeated = buildAccompanimentPlan(score, originalTimeline, second);
    check(block.valid() && block.events.size() == 8 && block.durationTicks == 1920,
          "4/4 block pattern has two three-note chords and two bass attacks, not one attack per melody note");
    for (std::size_t i = 0; i < block.events.size(); ++i)
    {
        const auto &event = block.events[i];
        const auto &same = repeated.events[i];
        check(event.startTick == same.startTick && event.durationTicks == same.durationTicks &&
                  event.midiPitch == same.midiPitch && event.role == same.role && event.velocity == same.velocity,
              "Accompaniment event ordering and voicing are deterministic");
        check(event.midiPitch >= 36 && event.midiPitch <= 72 && event.durationTicks == 960,
              "Voicing leaves playback-transpose headroom and holds across short melody notes");
        check(event.velocity == (event.role == AccompanimentRole::Bass ? 68 : 62),
              "Roles keep separate velocities");
    }
    auto arpeggioArrangement = first;
    arpeggioArrangement.settings.pattern = AccompanimentPattern::Arpeggio;
    const auto arpeggio = buildAccompanimentPlan(score, originalTimeline, arpeggioArrangement);
    check(arpeggio.valid() && arpeggio.events.size() == 10 && arpeggio.durationTicks == block.durationTicks,
          "Arpeggio has eight subdivisions and two bass notes on the same melody clock");
    for (const auto &event : arpeggio.events)
    {
        if (event.role == AccompanimentRole::Chord)
            check(event.durationTicks == 240, "Arpeggio uses eighth-note subdivisions");
    }
    const auto unchanged = buildTimeline(score);
    check(unchanged.events.size() == originalTimeline.events.size() &&
              unchanged.durationTicks == originalTimeline.durationTicks,
          "Generation never adds notes to melody timeline");
    for (std::size_t i = 0; i < unchanged.events.size(); ++i)
        check(unchanged.events[i].midiPitch == originalTimeline.events[i].midiPitch &&
                  unchanged.events[i].startTick == originalTimeline.events[i].startTick,
              "Generation leaves source pitch and timing untouched");
}

void accompanimentMinorAndModulation()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(6, 480), note(1, 480), note(3, 480), note(6, 480)};
    auto arrangement = generateAccompaniment(score);
    check(arrangement.valid() && arrangement.chords[0].rootPitchClass == 9 &&
              arrangement.chords[0].quality == ChordQuality::Minor,
          "A-centered 6-1-3 melody analyzes relative A minor, not C minor");
    AccompanimentSettings settings;
    settings.mode = HarmonicMode::Minor;
    score.tonic = 2;
    arrangement = generateAccompaniment(score, settings);
    check(arrangement.chords[0].rootPitchClass == 11 && arrangement.chords[0].quality == ChordQuality::Minor,
          "1=D with implicit minor tonic means B minor");
    settings.harmonicTonic = 2;
    score.notes = {note(1, 960), note(1, 960)};
    score.notes[0].accidental = 0;
    score.notes[1].accidental = 0;
    arrangement = generateAccompaniment(score, settings);
    check(arrangement.chords[0].rootPitchClass == 2 && arrangement.chords[0].quality == ChordQuality::Minor,
          "Explicit harmonic tonic uses absolute D pitch class independent of numbered 1");
    score.tonic = 0;
    settings.mode = HarmonicMode::Major;
    settings.harmonicTonic = -1;
    score.notes = {note(1, 960), note(1, 960)};
    score.notes[1].keyOverride = 2;
    arrangement = generateAccompaniment(score, settings);
    check(arrangement.valid() && arrangement.chords.size() == 2 && arrangement.chords[0].endTick == 960 &&
              arrangement.chords[1].startTick == 960,
          "Midbar modulation splits source harmony without stretching the melody");
    check(arrangement.chords[0].rootPitchClass == 0 && arrangement.chords[1].rootPitchClass == 2,
          "Automatic source-key contexts keep C then D roots in absolute coordinates");
    const auto plan = buildAccompanimentPlan(score, buildTimeline(score, 2), arrangement);
    check(plan.valid() && std::any_of(plan.events.begin(), plan.events.end(), [](const AccompanimentEvent &event)
                                      { return event.startTick == 0 && event.midiPitch % 12 == 0; }),
          "Supplying a transposed melody timeline does not transpose accompaniment in the domain layer");
    score.notes[0].accidental = 1;
    arrangement = generateAccompaniment(score, settings);
    check(arrangement.valid() &&
              accompanimentDiagnostic(arrangement.diagnostics, "messages.accompaniment.chromatic_notes"),
          "Chromatic melody produces an explicit review suggestion, not a rewritten pitch");
}

void accompanimentPickupAndMeters()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(5, 240), note(1, 1920), note(3, 960)};
    score.notes[1].measure = 1;
    score.notes[2].measure = 2;
    auto arrangement = generateAccompaniment(score);
    check(arrangement.valid() && arrangement.chords.size() == 3 && arrangement.chords[0].startTick == 0 &&
              arrangement.chords[0].endTick == 240 && arrangement.chords[1].startTick == 240 &&
              arrangement.chords[1].endTick == 2160 && arrangement.chords[2].endTick == 3120,
          "Pickup/full/final partial source measures keep exact ticks");
    check(accompanimentDiagnostic(arrangement.diagnostics, "messages.accompaniment.pickup") &&
              accompanimentDiagnostic(arrangement.diagnostics, "messages.accompaniment.incomplete_final_measure"),
          "Pickup and final partial measures are diagnosed");
    score.notes[1].durationTicks = 960;
    arrangement = generateAccompaniment(score);
    check(arrangement.valid() &&
              accompanimentDiagnostic(arrangement.diagnostics, "messages.accompaniment.measure_alignment") &&
              arrangement.chords[0].endTick == 1920 && arrangement.chords.back().endTick == 2160,
          "Invalid middle measure is regrouped by cumulative ticks with a correction diagnostic");
    score.notes = {note(1, 960), note(3, 960), note(5, 960), note(1, 960)};
    arrangement = generateAccompaniment(score);
    check(arrangement.valid() && arrangement.chords.size() == 2,
          "A single unannotated OCR measure spanning bars falls back to cumulative ticks");
    for (const auto meter : {2, 3, 4})
    {
        score.beatsPerBar = meter;
        score.beatUnit = 4;
        score.notes = {note(1, meter * 480)};
        arrangement = generateAccompaniment(score);
        const auto plan = buildAccompanimentPlan(score, buildTimeline(score), arrangement);
        check(arrangement.valid() && plan.valid() && plan.durationTicks == meter * 480,
              "Supported simple meters preserve the source bar length");
    }
    score.beatsPerBar = 6;
    score.beatUnit = 8;
    score.bpm = 60;
    score.notes = {note(1, 1440)};
    arrangement = generateAccompaniment(score);
    const auto timeline = buildTimeline(score);
    const auto compound = buildAccompanimentPlan(score, timeline, arrangement);
    check(compound.valid() && compound.events.size() == 8, "6/8 has two dotted-quarter chord/bass groups");
    check(compound.events[0].durationTicks == 720 && compound.events[4].startTick == 720,
          "6/8 accompaniment follows compound grouping");
    near(timeline.durationSeconds(), 3, "6/8 BPM remains quarter notes per minute");
    for (const auto meter : {5, 7, 9, 12})
    {
        score.beatsPerBar = meter;
        arrangement = generateAccompaniment(score);
        check(!arrangement.valid() && arrangement.chords.empty() &&
                  accompanimentDiagnostic(arrangement.diagnostics, "messages.accompaniment.meter_unsupported"),
              "Unsupported P0 meters fail explicitly instead of silently acting like 4/4");
    }
}

void accompanimentRepeatsAndSourceRuns()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(1, 240), note(2, 240), note(3, 240), note(4, 240), note(5, 240)};
    score.repeats = {{1, 4, 2, 3}};
    score.notes[1].verseLyrics = {"A", "B"};
    const auto timeline = buildTimeline(score);
    AccompanimentArrangement arrangement;
    arrangement.melodyFingerprint = accompanimentFingerprint(score);
    arrangement.chords = {{0, 1200, 0, ChordQuality::Major}};
    auto plan = buildAccompanimentPlan(score, timeline, arrangement);
    check(plan.valid() && plan.durationTicks == 1680, "Repeat/endings accompaniment follows the melody duration");
    check(plan.events.size() == 12,
          "Continuous short notes share sustained chords while each jump restores four voices");
    for (const auto &event : plan.events)
    {
        check(event.startTick + event.durationTicks <= plan.durationTicks, "No event crosses the song end");
        check(!(event.startTick < 960 && event.startTick + event.durationTicks > 960),
              "The repeat-back jump truncates old sustained harmony");
        check(!(event.startTick < 1440 && event.startTick + event.durationTicks > 1440),
              "The skipped first ending truncates harmony before the second ending");
    }
    check(timeline.events[4].verseIndex == 1 && timeline.events[4].lyric == "B",
          "Independent accompaniment plan never changes the melody verse cursor");
    arrangement.chords = {{0, 240, 0, ChordQuality::None},
                          {240, 720, 0, ChordQuality::Major},
                          {720, 960, 5, ChordQuality::Major},
                          {960, 1200, 7, ChordQuality::Major}};
    plan = buildAccompanimentPlan(score, timeline, arrangement);
    const auto hasRootAt = [&plan](std::int64_t tick, int pitchClass)
    {
        return std::any_of(plan.events.begin(), plan.events.end(),
                           [tick, pitchClass](const AccompanimentEvent &event)
                           {
                               return event.startTick == tick && event.role == AccompanimentRole::Bass &&
                                      event.midiPitch % 12 == pitchClass;
                           });
    };
    check(plan.valid() && hasRootAt(720, 5) && hasRootAt(1440, 7),
          "First and second endings use their own source harmony");
    check(!hasRootAt(1200, 5), "The first-ending chord never leaks into the later pass");
    score.repeats = {{0, 5, 3, -1}};
    arrangement.melodyFingerprint = accompanimentFingerprint(score);
    plan = buildAccompanimentPlan(score, buildTimeline(score), arrangement);
    check(plan.valid() && plan.durationTicks == 3600, "Multiple repeat passes expand the accepted arrangement");
    check(hasRootAt(240, 0) && hasRootAt(1440, 0) && hasRootAt(2640, 0),
          "Every occurrence preserves source chord timing");
}

void accompanimentFingerprintBoundaries()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(1), note(3), note(5), note(1)};
    const auto original = accompanimentFingerprint(score);
    auto edited = score;
    edited.title = "New title";
    edited.imagePath = "new.png";
    edited.bpm = 140;
    edited.baseVelocity = 20;
    edited.accentBeats = false;
    edited.versePrograms = {73, 24};
    edited.notes[0].lyric = "new words";
    edited.notes[0].verseLyrics = {"A", "B"};
    edited.notes[0].confidence = 0.2;
    edited.notes[0].source = {10, 20, 30, 40};
    edited.notes[0].id = 99;
    edited.notes[0].line = 2;
    check(accompanimentFingerprint(edited) == original,
          "Lyrics, image anchors, title, tempo, dynamics and melody programs do not invalidate harmony");
    for (int field = 0; field < 9; ++field)
    {
        edited = score;
        switch (field)
        {
        case 0:
            edited.notes[0].degree = 2;
            break;
        case 1:
            edited.notes[0].octave = -1;
            break;
        case 2:
            edited.notes[0].accidental = 1;
            break;
        case 3:
            edited.notes[0].durationTicks = 240;
            break;
        case 4:
            edited.notes[0].keyOverride = 2;
            break;
        case 5:
            edited.notes[0].measure = 1;
            break;
        case 6:
            edited.notes[0].tieToNext = true;
            break;
        case 7:
            edited.tonic = 2;
            break;
        case 8:
            edited.beatsPerBar = 3;
            break;
        }
        check(accompanimentFingerprint(edited) != original,
              "Every harmony-relevant note/key/meter field invalidates");
    }
    edited = score;
    edited.repeats = {{0, 4, 2, 3}};
    const auto repeated = accompanimentFingerprint(edited);
    check(repeated != original, "Repeat path changes invalidate");
    edited.repeats[0].count = 3;
    check(accompanimentFingerprint(edited) != repeated, "Repeat occurrence count changes invalidate");
    edited = score;
    edited.beatUnit = 8;
    check(accompanimentFingerprint(edited) != original, "Meter denominator changes invalidate");
}

void accompanimentValidationAndUserCorrections()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(1, 1920)};
    const auto timeline = buildTimeline(score);
    const auto generated = generateAccompaniment(score);
    check(generated.valid() && AccompanimentPlan{}.valid(), "Empty legacy audio plan is valid silence");
    auto arrangement = generated;
    arrangement.chords[0].rootPitchClass = 5;
    arrangement.chords[0].quality = ChordQuality::Minor;
    arrangement.chords[0].inversion = 2;
    arrangement.chords[0].userEdited = true;
    const auto corrected = buildAccompanimentPlan(score, timeline, arrangement);
    check(corrected.valid() && arrangement.chords[0].userEdited && corrected.events.front().midiPitch % 12 == 0,
          "Corrected F minor second inversion persists and changes voiced chord pitches");
    arrangement.settings.pattern = AccompanimentPattern::Arpeggio;
    check(buildAccompanimentPlan(score, timeline, arrangement).valid() &&
              arrangement.chords[0].rootPitchClass == 5 && arrangement.chords[0].inversion == 2,
          "Pattern changes preserve user-corrected roots, quality and inversion");
    arrangement.chords[0].quality = ChordQuality::None;
    arrangement.chords[0].inversion = 0;
    auto silent = buildAccompanimentPlan(score, timeline, arrangement);
    check(silent.valid() && silent.events.empty() && silent.durationTicks == 1920,
          "Explicit no-accompaniment intervals retain the complete logical duration");
    arrangement.chords.clear();
    silent = buildAccompanimentPlan(score, timeline, arrangement);
    check(silent.valid() && silent.events.empty(), "A valid empty accepted arrangement is silence");
    for (int field = 0; field < 12; ++field)
    {
        arrangement = generated;
        switch (field)
        {
        case 0:
            arrangement.chords[0].startTick = -1;
            break;
        case 1:
            arrangement.chords[0].endTick = 0;
            break;
        case 2:
            arrangement.chords[0].endTick = 1921;
            break;
        case 3:
            arrangement.chords.push_back(arrangement.chords[0]);
            break;
        case 4:
            arrangement.chords[0].rootPitchClass = 12;
            break;
        case 5:
            arrangement.chords[0].quality = static_cast<ChordQuality>(99);
            break;
        case 6:
            arrangement.chords[0].inversion = 3;
            break;
        case 7:
            arrangement.settings.chordProgram = 128;
            break;
        case 8:
            arrangement.settings.bassVelocity = 0;
            break;
        case 9:
            arrangement.settings.harmonicTonic = 12;
            break;
        case 10:
            arrangement.settings.pattern = static_cast<AccompanimentPattern>(99);
            break;
        case 11:
            arrangement.generatorVersion = 2;
            break;
        }
        const auto invalid = buildAccompanimentPlan(score, timeline, arrangement);
        check(!invalid.valid() && invalid.events.empty(),
              "Invalid arrangements fail atomically without partial audio");
    }
    arrangement = generated;
    score.notes[0].durationTicks = 960;
    auto invalid = buildAccompanimentPlan(score, buildTimeline(score), arrangement);
    check(!invalid.valid() && invalid.events.empty() &&
              accompanimentDiagnostic(invalid.diagnostics, "messages.accompaniment.stale"),
          "Source edits preserve old arrangement data but reject stale audio");
    score.notes[0].durationTicks = 1920;
    auto malformedTimeline = timeline;
    malformedTimeline.events[0].sourceNoteIndex = 100;
    invalid = buildAccompanimentPlan(score, malformedTimeline, generated);
    check(!invalid.valid() && invalid.events.empty(), "Malformed source mapping cannot index outside the score");
    malformedTimeline = timeline;
    malformedTimeline.events[0].durationTicks = 1919;
    check(!buildAccompanimentPlan(score, malformedTimeline, generated).valid(),
          "A stale timeline duration cannot silently shift accompaniment");
    score.notes = {note(0, 1920)};
    arrangement = generateAccompaniment(score);
    check(arrangement.valid() && arrangement.chords[0].quality == ChordQuality::None &&
              buildAccompanimentPlan(score, buildTimeline(score), arrangement).events.empty(),
          "All-rest passage stays silent instead of inventing a tonic attack");
    score.notes[0].durationTicks = 0;
    check(!generateAccompaniment(score).valid(), "Invalid source duration fails generation");
}

void accompanimentEventBounds()
{
    using namespace singlilt;
    Score score;
    score.notes.assign(1600, note(1, TicksPerQuarter * 256));
    auto arrangement = generateAccompaniment(score);
    check(!arrangement.valid() && arrangement.chords.empty() &&
              accompanimentDiagnostic(arrangement.diagnostics, "messages.accompaniment.event_limit"),
          "An enormous valid-tick source cannot allocate unbounded chord intervals");
    score.notes = {note(1, 1920)};
    arrangement = generateAccompaniment(score);
    arrangement.chords.resize(100001, arrangement.chords.front());
    const auto diagnostics = validateAccompaniment(score, arrangement);
    check(accompanimentErrors(diagnostics) && diagnostics.size() <= 2,
          "Oversized arrangement fails once before scanning all conflicting spans");
    score.notes.assign(400, note(1, TicksPerQuarter * 256));
    score.repeats = {{0, score.notes.size(), 2, -1}};
    AccompanimentSettings settings;
    settings.pattern = AccompanimentPattern::Arpeggio;
    arrangement = generateAccompaniment(score, settings);
    check(arrangement.valid(), "A bounded long source remains analyzable");
    const auto plan = buildAccompanimentPlan(score, buildTimeline(score), arrangement);
    check(!plan.valid() && plan.events.empty() &&
              accompanimentDiagnostic(plan.diagnostics, "messages.accompaniment.event_limit"),
          "Repeated expansion exceeds its event budget atomically rather than returning truncated audio");
}

void writtenMeasuresAndMetronome()
{
    using namespace singlilt;
    Score score;
    score.notes = {note(1, 480), note(2, 1440), note(3, 960), note(4, 1920)};
    score.notes[2].measure = 15;
    score.notes[2].pageIndex = 1;
    score.notes[3].measure = 35;
    score.notes[3].pageIndex = 2;
    score.writtenMeasures = {{0, 1920, 0, 4, 4, 0}, {1920, 960, 15, 2, 4, 1}, {2880, 1920, 35, 4, 4, 2}};
    score.repeats = {{2, 4, 2, -1}};
    const auto timeline = buildTimeline(score);
    check(timeline.valid() && timeline.durationTicks == 7680,
          "Written meter changes preserve source durations and repeated playback order");
    const std::vector<std::int64_t> beatPositions{0,    480,  960,  1440, 1920, 2400, 2880, 3360,
                                                  3840, 4320, 4800, 5280, 5760, 6240, 6720, 7200};
    const std::vector<std::int64_t> downbeats{0, 1920, 2880, 4800, 5760};
    check(timeline.metronomeBeats.size() == beatPositions.size(),
          "Each source beat expands exactly once per pass");
    for (std::size_t i = 0; i < beatPositions.size(); ++i)
    {
        check(timeline.metronomeBeats[i].startTick == beatPositions[i], "Expanded beat retains its source offset");
        check(timeline.metronomeBeats[i].downbeat ==
                  (std::find(downbeats.begin(), downbeats.end(), beatPositions[i]) != downbeats.end()),
              "Downbeats follow the local 4/4 and 2/4 written measure boundaries on every pass");
    }
    const auto ranges = sourceMeasureRanges(score);
    check(ranges.size() == 3 && ranges[0].measure == 0 && ranges[1].measure == 15 && ranges[2].measure == 35 &&
              ranges[1].startTick == 1920 && ranges[1].endTick == 2880,
          "Actual written labels and the short 2/4 span survive range extraction without fixed-meter padding");
    auto invalid = score;
    invalid.writtenMeasures[1].startTick = 1919;
    check(!buildTimeline(invalid).valid(), "Overlapping written measure spans are rejected");
    invalid = score;
    invalid.writtenMeasures.back().durationTicks -= 1;
    check(!buildTimeline(invalid).valid(), "Written measures must cover the complete source timeline");
    invalid = score;
    invalid.notes[2].pageIndex = 0;
    check(!buildTimeline(invalid).valid(), "A guide note cannot silently move to a different physical page");
    invalid = score;
    invalid.writtenMeasures[1].beatUnit = 3;
    check(!buildTimeline(invalid).valid(), "A malformed local meter is rejected");

    Score compound;
    compound.beatsPerBar = 6;
    compound.beatUnit = 8;
    compound.notes = {note(1, 1440)};
    compound.writtenMeasures = {{0, 1440, 0, 6, 8, 0}};
    const auto compoundTimeline = buildTimeline(compound);
    check(compoundTimeline.valid() && compoundTimeline.metronomeBeats.size() == 2 &&
              compoundTimeline.metronomeBeats[1].startTick == 720,
          "Written compound meter pulses on dotted quarters");
    Score pickup;
    pickup.notes = {note(1, 480), note(2, 1920)};
    pickup.notes[1].measure = 1;
    pickup.writtenMeasures = {{0, 480, 0, 4, 4, 0}, {480, 1920, 1, 4, 4, 0}};
    const auto pickupTimeline = buildTimeline(pickup);
    check(pickupTimeline.valid() && !pickupTimeline.metronomeBeats[0].downbeat &&
              pickupTimeline.metronomeBeats[1].startTick == 480 && pickupTimeline.metronomeBeats[1].downbeat,
          "A short initial pickup does not become a false strong downbeat");
    pickup.writtenMeasures.clear();
    check(buildTimeline(pickup).metronomeBeats.empty(),
          "Legacy scores keep the existing audio metronome fallback");
}

} // namespace

int main()
{
    const std::vector<std::pair<const char *, void (*)()>> tests{
        {"braced numbered performance", numberedPerformanceTests},
        {"continuous singing pitch", singingPitchTests},
        {"singing assessment", singingAssessmentTests},
        {"ear-training generation", earTrainingTests},
        {"pitch and transposition", pitchAndTransposition},
        {"durations and compound meter", durationsAndMeter},
        {"repeats and endings", repeatsAndEndings},
        {"ties", ties},
        {"validation", validation},
        {"seek bounds", seekBounds},
        {"verse lyrics and legacy migration", verseLyricsAndMigration},
        {"repeat verses and boundary seeking", repeatVersesAndSeeking},
        {"verse endings and tail carry", verseEndingsAndCarry},
        {"verse ties and validation", verseTiesAndValidation},
        {"controllable velocity", controllableVelocity},
        {"written measures, pages and expanded metronome", writtenMeasuresAndMetronome},
        {"accompaniment deterministic patterns", accompanimentDeterminismAndPatterns},
        {"accompaniment minor and modulation", accompanimentMinorAndModulation},
        {"accompaniment pickup and meters", accompanimentPickupAndMeters},
        {"accompaniment repeats and continuous source runs", accompanimentRepeatsAndSourceRuns},
        {"accompaniment fingerprint boundaries", accompanimentFingerprintBoundaries},
        {"accompaniment validation and corrections", accompanimentValidationAndUserCorrections},
        {"accompaniment event bounds", accompanimentEventBounds},
    };
    int failed = 0;
    for (const auto &[name, test] : tests)
    {
        try
        {
            test();
            std::cout << "PASS " << name << '\n';
        }
        catch (const std::exception &exception)
        {
            ++failed;
            std::cerr << "FAIL " << name << ": " << exception.what() << '\n';
        }
    }
    std::cout << "Core tests: " << tests.size() - failed << '/' << tests.size() << " passed\n";
    return failed == 0 ? 0 : 1;
}

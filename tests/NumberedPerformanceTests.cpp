// Braced numbered-score timing, chords and repeat regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "NumberedPerformanceTests.h"
#include "domain/NumberedPerformance.h"
#include <algorithm>
#include <stdexcept>

namespace
{
void check(bool passed, const char *message)
{
    if (!passed)
        throw std::runtime_error(message);
}

singlilt::Note note(int line, double x, int degree, int duration, double y = -1)
{
    singlilt::Note result;
    result.line = line;
    result.degree = degree;
    result.durationTicks = duration;
    result.source = {x, y < 0 ? 50.0 + line * 70 : y, 10, 20};
    result.octave = line % 2 ? -1 : 0;
    return result;
}
} // namespace

void numberedPerformanceTests()
{
    using namespace singlilt;
    Score upperRepeat;
    upperRepeat.notes = {note(0, 20, 1, 480), note(0, 60, 2, 480), note(1, 20, 3, 960), note(2, 20, 4, 480),
                         note(3, 20, 5, 480)};
    // noteAfter() at the upper row's right edge points at the first lower-row source note.
    upperRepeat.repeats = {{0, 2, 2, -1}};
    const auto upperRepeatParts = buildNumberedPerformance(upperRepeat, {{0, 1, {}}, {2, 3, {}}});
    const auto upperRepeatPlan =
        buildStaffPerformancePlan(upperRepeat, buildTimeline(upperRepeat), upperRepeatParts);
    check(upperRepeatPlan.valid() && upperRepeat.repeats[0].endNote == 2 &&
              upperRepeatPlan.durationTicks == 2400 && upperRepeatPlan.events.size() == 8,
          "A closing repeat at the upper row's edge includes both complete hands");
    Score interiorRepeat;
    interiorRepeat.notes = {note(0, 20, 1, 480), note(0, 60, 2, 480), note(0, 120, 3, 480), note(0, 160, 4, 480),
                            note(1, 20, 5, 1920)};
    interiorRepeat.repeats = {{0, 2, 2, -1}};
    const auto interiorParts = buildNumberedPerformance(interiorRepeat, {{0, 1, {}}});
    const auto interiorPlan =
        buildStaffPerformancePlan(interiorRepeat, buildTimeline(interiorRepeat), interiorParts);
    check(interiorPlan.valid() && interiorRepeat.repeats[0].endNote == 2 && interiorPlan.durationTicks == 2880,
          "An interior upper-row repeat boundary must not become a full-system boundary");

    Score score;
    score.notes = {note(0, 20, 1, 480), note(0, 60, 2, 480), note(1, 20, 3, 960), note(2, 20, 4, 480),
                   note(3, 20, 5, 480)};
    const auto performance = buildNumberedPerformance(score, {{0, 1, {}}, {2, 3, {}}});
    const auto timeline = buildTimeline(score);
    auto plan = buildStaffPerformancePlan(score, timeline, performance);
    check(timeline.valid() && plan.valid(), "Two-hand timing must form a valid performance");
    check(score.notes.size() == 3 && timeline.durationTicks == 1440,
          "A brace plays hands in parallel, then advances to the next system");
    check(performance.notes[0].startTick == 0 && performance.notes[2].startTick == 0 &&
              performance.notes[3].startTick == 960 && performance.notes[4].startTick == 960,
          "Both hands share each system's onset");
    check(performance.notes[2].source.y == 120 && performance.notes[2].staff == 2,
          "The lower hand retains its own source anchor and staff");

    Score unequal;
    unequal.notes = {note(0, 20, 1, 240), note(0, 120, 2, 480), note(1, 20, 3, 480), note(1, 160, 4, 480)};
    const auto padded = buildNumberedPerformance(unequal, {{0, 1, {100}}});
    check(unequal.notes.size() == 3 && unequal.notes[1].degree == 0 && unequal.notes[1].durationTicks == 240 &&
              !unequal.notes[1].hasImageAnchor,
          "A shorter upper-hand measure gets a rest, not a stretched note");
    check(padded.notes[1].startTick == 0 && padded.notes[2].startTick == 480 && padded.notes[3].startTick == 480,
          "Barlines align different rhythms and different horizontal spacing");

    Score chord;
    chord.notes = {note(0, 20, 0, 240), note(0, 60, 1, 240), note(1, 20, 3, 240), note(1, 20, 5, 480, 85),
                   note(1, 60, 6, 240)};
    const auto chords = buildNumberedPerformance(chord, {{0, 1, {}}});
    check(chords.notes.size() == 4 && chords.durationTicks == 480,
          "Stacked pitches do not add sequential duration");
    check(chords.notes[1].startTick == 0 && chords.notes[2].startTick == 0 &&
              chords.notes[1].durationTicks == 240 && chords.notes[2].durationTicks == 240 &&
              chords.notes[3].startTick == 240,
          "All chord tones use the bottom digit's rhythm");
    check(chords.notes[0].startTick == 240, "A rest delays only its own hand");

    Score repeated;
    repeated.notes = {note(0, 20, 1, 480), note(1, 20, 3, 480), note(2, 20, 2, 480), note(3, 20, 4, 480)};
    repeated.repeats = {{0, 2, 2, -1}};
    const auto repeatedParts = buildNumberedPerformance(repeated, {{0, 1, {}}, {2, 3, {}}});
    plan = buildStaffPerformancePlan(repeated, buildTimeline(repeated), repeatedParts);
    check(plan.valid() && plan.durationTicks == 1440 && plan.events.size() == 6,
          "The same repeat expands both hands, not just the practice guide");
    check(plan.events[2].startTick == 480 && plan.events[3].startTick == 480,
          "Both hands restart together on the second pass");

    Score endings;
    endings.notes = {note(0, 20, 1, 480), note(1, 20, 3, 480), note(2, 20, 2, 480),
                     note(3, 20, 4, 480), note(4, 20, 5, 480), note(5, 20, 6, 480)};
    endings.repeats = {{0, 4, 2, 2}};
    const auto endingParts = buildNumberedPerformance(endings, {{0, 1, {}}, {2, 3, {}}, {4, 5, {}}});
    plan = buildStaffPerformancePlan(endings, buildTimeline(endings), endingParts);
    check(plan.valid() && plan.events.size() == 8 && plan.durationTicks == 1920 &&
              endings.repeats[0].firstEndingNote == 1,
          "First endings remap to the guide and skip together in both hands");

    Score modulation;
    modulation.notes = {note(0, 20, 1, 480), note(0, 60, 1, 480), note(1, 20, 3, 480), note(1, 60, 3, 480)};
    modulation.notes[1].keyOverride = 2;
    const auto modulated = buildNumberedPerformance(modulation, {{0, 1, {}}});
    check(modulated.notes[2].midiPitch == 52 && modulated.notes[3].midiPitch == 54,
          "The lower hand follows the key at its onset, not the final key of the measure");
    Score lowerKey;
    lowerKey.notes = {note(0, 20, 1, 960), note(0, 120, 2, 960), note(1, 20, 3, 480), note(1, 60, 3, 480),
                      note(1, 120, 4, 960)};
    lowerKey.notes[3].keyOverride = 2;
    const auto lowerKeyParts = buildNumberedPerformance(lowerKey, {{0, 1, {}}});
    const auto lowerKeyTimeline = buildTimeline(lowerKey);
    check(lowerKey.keyChanges.size() == 1 && lowerKey.keyChanges[0].startTick == 480 &&
              lowerKey.keyChanges[0].tonic == 2 && lowerKey.keyChanges[0].sourceNoteIndex == -1 &&
              lowerKeyTimeline.events[0].midiPitch == 60 && lowerKeyTimeline.events[1].midiPitch == 64 &&
              lowerKeyParts.notes[2].midiPitch == 52 && lowerKeyParts.notes[3].midiPitch == 54 &&
              lowerKeyParts.notes[4].midiPitch == 55,
          "A lower-hand modulation preserves its exact onset without retuning the held upper note");
    auto retimedUpper = lowerKey.notes[0];
    retimedUpper.durationTicks = 480;
    const auto retimedLowerKey = correctedNumberedGuide(lowerKey, lowerKeyParts, 0, retimedUpper);
    check(retimedLowerKey.score.keyChanges[0].startTick == 480 &&
              retimedLowerKey.performance.notes[2].midiPitch == 52 &&
              retimedLowerKey.performance.notes[3].midiPitch == 54 &&
              buildStaffPerformancePlan(retimedLowerKey.score, buildTimeline(retimedLowerKey.score),
                                        retimedLowerKey.performance)
                  .valid(),
          "Guide duration correction keeps independently timed lower-hand modulations and pitches");
    Score lowerRest;
    lowerRest.notes = {note(0, 20, 1, 480), note(1, 20, 0, 1920)};
    const auto lowerRestParts = buildNumberedPerformance(lowerRest, {{0, 1, {}}});
    auto shorter = lowerRest.notes[0];
    shorter.durationTicks = 240;
    const auto restCorrection = correctedNumberedGuide(lowerRest, lowerRestParts, 0, shorter);
    check(restCorrection.performance.durationTicks == 1920 &&
              restCorrection.score.notes.back().durationTicks == 1680,
          "A guide correction must retain the other hand's entirely silent measure");

    Score tied;
    tied.notes = {note(0, 20, 1, 480), note(0, 60, 1, 480), note(1, 20, 3, 480), note(1, 60, 3, 480)};
    tied.notes[0].tieToNext = true;
    tied.notes[2].tieToNext = true;
    const auto ties = buildNumberedPerformance(tied, {{0, 1, {}}});
    plan = buildStaffPerformancePlan(tied, buildTimeline(tied), ties);
    check(plan.valid() && plan.events.size() == 2 && plan.events[0].durationTicks == 960 &&
              plan.events[1].durationTicks == 960,
          "Each hand sustains its own ties");

    Score mixed;
    mixed.notes = {note(0, 20, 1, 480), note(1, 20, 2, 480), note(2, 20, 3, 480)};
    const auto mixedParts = buildNumberedPerformance(mixed, {{0, -1, {}}, {1, 2, {}}});
    check(mixedParts.durationTicks == 960 && mixedParts.notes[0].startTick == 0 &&
              mixedParts.notes[1].startTick == 480 && mixedParts.notes[2].startTick == 480,
          "Unbraced systems stay sequential alongside braced systems");

    Score editable;
    editable.notes = {note(0, 20, 1, 480), note(0, 60, 4, 480), note(1, 20, 3, 960)};
    editable.repeats = {{0, 2, 2, -1}};
    const auto editableParts = buildNumberedPerformance(editable, {{0, 1, {}}});
    const auto metadata = correctedNumberedMetadata(editable, editableParts, 2, 3, 8);
    const auto metadataTimeline = buildTimeline(metadata.score);
    check(metadataTimeline.valid() && metadata.score.tonic == 2 && metadata.score.beatsPerBar == 3 &&
              metadata.score.beatUnit == 8 && metadata.score.writtenMeasures[0].beatsPerBar == 3 &&
              metadata.score.writtenMeasures[0].beatUnit == 8 && metadata.performance.sourceTonic == 2 &&
              metadata.performance.notes[0].midiPitch == 62 && metadata.performance.notes[2].midiPitch == 54 &&
              metadata.performance.durationTicks == editableParts.durationTicks &&
              metadata.performance.notes[2].durationTicks == 960 &&
              buildStaffPerformancePlan(metadata.score, metadataTimeline, metadata.performance).valid(),
          "Global numbered key and meter corrections synchronize both hands without changing written timing");
    const auto metadataModulation = correctedNumberedMetadata(lowerKey, lowerKeyParts, 3, 4, 4);
    check(metadataModulation.performance.notes[0].midiPitch == 63 &&
              metadataModulation.performance.notes[2].midiPitch == 55 &&
              metadataModulation.performance.notes[3].midiPitch == 54 &&
              metadataModulation.performance.notes[1].midiPitch == 64,
          "Global key correction respects explicit independently timed modulations in either hand");
    auto highScore = editable;
    highScore.tonic = 10;
    highScore.notes[0].degree = 6;
    highScore.notes[0].octave = 4;
    auto highParts = editableParts;
    highParts.sourceTonic = 10;
    highParts.notes[0].midiPitch = 127;
    highParts.notes[1].midiPitch += 10;
    highParts.notes[2].midiPitch += 10;
    highParts.timingFingerprint = staffTimingFingerprint(highScore);
    try
    {
        correctedNumberedMetadata(highScore, highParts, 11, 4, 4);
        throw std::runtime_error("Out-of-range global key correction must be rejected");
    }
    catch (const std::invalid_argument &)
    {
        check(highScore.tonic == 10 && highParts.notes[0].midiPitch == 127,
              "Rejected metadata corrections leave both input models unchanged");
    }
    auto replacement = editable.notes[0];
    replacement.degree = 4;
    replacement.durationTicks = 240;
    replacement.lyric = "corrected";
    replacement.verseLyrics = {"corrected", "second verse"};
    replacement.tieToNext = true;
    const auto corrected = correctedNumberedGuide(editable, editableParts, 0, replacement);
    plan = buildStaffPerformancePlan(corrected.score, buildTimeline(corrected.score), corrected.performance);
    check(plan.valid() && corrected.score.notes.size() == 3 && corrected.score.repeats[0].endNote == 3 &&
              corrected.score.notes[2].degree == 0 && corrected.score.notes[2].durationTicks == 240 &&
              !corrected.score.notes[2].hasImageAnchor,
          "Duration corrections retain the other hand and remap repeats across the padding rest");
    check(corrected.performance.notes[0].midiPitch == 65 && corrected.performance.notes[0].durationTicks == 240 &&
              corrected.performance.notes[0].tieStart && corrected.performance.notes[1].tieStop &&
              corrected.performance.notes[1].startTick == 240 &&
              corrected.performance.notes[2].durationTicks == 960,
          "Numbered pitch, duration and tie corrections update the performed notes without stretching the lower "
          "hand");
    check(corrected.score.notes[0].verseLyrics == replacement.verseLyrics &&
              corrected.score.notes[0].lyric == "corrected",
          "Numbered correction retains lyrics in the editable guide");
    replacement = corrected.score.notes[0];
    replacement.durationTicks = 480;
    const auto restoredTiming = correctedNumberedGuide(corrected.score, corrected.performance, 0, replacement);
    check(restoredTiming.score.notes.size() == 2 && restoredTiming.score.repeats[0].endNote == 2 &&
              restoredTiming.performance.notes[1].startTick == 480,
          "Restoring duration removes only generated padding and keeps repeat indexes current");

    replacement = editable.notes[0];
    replacement.degree = 0;
    const auto silenced = correctedNumberedGuide(editable, editableParts, 0, replacement);
    check(silenced.performance.notes.size() == 2 && silenced.score.notes[0].degree == 0,
          "Correcting a guide note to a rest removes its performed pitch");
    replacement.degree = 5;
    const auto sounded = correctedNumberedGuide(silenced.score, silenced.performance, 0, replacement);
    check(sounded.performance.notes.size() == 3 && sounded.performance.notes.back().sourceNoteIndex == 0 &&
              sounded.performance.notes.back().midiPitch == 67,
          "Correcting a printed rest to a sounding note creates its linked performed event");

    replacement = upperRepeat.notes[0];
    replacement.durationTicks = 960;
    const auto longer = correctedNumberedGuide(upperRepeat, upperRepeatParts, 0, replacement);
    check(longer.performance.notes[2].durationTicks == 960 && longer.performance.notes[3].startTick == 1440 &&
              longer.performance.notes[4].startTick == 1440 && longer.score.writtenMeasures[1].startTick == 1440,
          "Longer guide notes shift later systems without stretching written lower-hand notes");
    replacement = editable.notes[0];
    replacement.durationTicks = 0;
    try
    {
        correctedNumberedGuide(editable, editableParts, 0, replacement);
        throw std::runtime_error("An invalid numbered correction must be rejected");
    }
    catch (const std::invalid_argument &)
    {
        check(editable.notes[0].durationTicks == 480 && editableParts.notes[0].durationTicks == 480,
              "Rejected corrections preserve the guide and performed notes");
    }

    Score invalid;
    invalid.notes = {note(0, 20, 1, 480), note(1, 20, 3, 480)};
    try
    {
        buildNumberedPerformance(invalid, {{0, -1, {}}});
        throw std::runtime_error("An unassigned hand must not be silently dropped");
    }
    catch (const std::invalid_argument &)
    {
        check(invalid.notes.size() == 2, "Failed conversion preserves the original score");
    }
}

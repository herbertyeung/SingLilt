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

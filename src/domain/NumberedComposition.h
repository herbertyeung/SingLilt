// Authoritative authored voices and shared-instrument performance compilation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#pragma once
#include "AccompanimentTimeline.h"

namespace singlilt
{
inline constexpr int MaximumNumberedVoices = 8;
struct NumberedVoice
{
    int id = 1;
    std::string name;
    std::vector<Note> notes;
    bool operator==(const NumberedVoice &) const = default;
};
struct NumberedMeasure
{
    int id = 0;
    int beats = 0, unit = 0; // Zero inherits the preceding meter.
    double bpm = 0;          // Zero inherits.
    int tonic = -1;          // -1 inherits.
    bool operator==(const NumberedMeasure &) const = default;
};
struct NumberedMeasureRepeat
{
    int first = 0, last = 0, firstEnding = 0; // Stable IDs, inclusive last; zero means no ending.
    int count = 2;
    bool operator==(const NumberedMeasureRepeat &) const = default;
};
struct NumberedScoreStructure
{
    std::vector<NumberedMeasure> measures;
    std::vector<NumberedMeasureRepeat> repeats;
    int nextId = 1;
    bool operator==(const NumberedScoreStructure &) const = default;
};
struct NumberedComposition
{
    std::vector<NumberedVoice> voices;
    int nextId = 2;
    NumberedScoreStructure structure;
    bool operator==(const NumberedComposition &) const = default;
};
struct NumberedMeasureSpan
{
    int id;
    std::int64_t start, end;
    int beats, unit, tonic;
    double bpm;
};
struct PerformedMeasure
{
    std::size_t measure;
    std::int64_t start;
    int pass;
};
struct PerformedScoreMap
{
    std::vector<NumberedMeasureSpan> written;
    std::vector<PerformedMeasure> order;
    std::int64_t writtenEnd = 0;
    std::int64_t duration = 0;
};
std::vector<NumberedMeasureSpan> numberedMeasures(const Score &score, const NumberedComposition &composition);
std::vector<NumberedMeasureSpan> numberedMeasures(const Score &score, const NumberedScoreStructure &structure,
                                                  std::int64_t duration);
std::size_t numberedMeasureAt(const std::vector<NumberedMeasureSpan> &measures, std::int64_t tick);
PerformedScoreMap performedScoreMap(const Score &score, const NumberedComposition &composition);
Timeline compositionTimeline(const Score &metadata, const NumberedComposition &composition);
NumberedVoice &numberedVoice(NumberedComposition &composition, int id);
const NumberedVoice &numberedVoice(const NumberedComposition &composition, int id);
Timeline buildNumberedWrittenTimeline(Score score, const std::vector<NumberedMeasureSpan> &measures,
                                      int transpose = 0);
void validateNumberedComposition(const Score &metadata, const NumberedComposition &composition, int transpose = 0);
std::int64_t compositionDuration(const NumberedComposition &composition);
int compositionBars(const Score &metadata, const NumberedComposition &composition);
Score compositionPlaybackScore(const Score &metadata, const NumberedComposition &composition);
AccompanimentPlan compositionPlaybackPlan(const Score &metadata, const NumberedComposition &composition);
} // namespace singlilt

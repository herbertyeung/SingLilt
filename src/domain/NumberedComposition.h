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
struct NumberedComposition
{
    std::vector<NumberedVoice> voices;
    int nextId = 2;
    bool operator==(const NumberedComposition &) const = default;
};
NumberedVoice &numberedVoice(NumberedComposition &composition, int id);
const NumberedVoice &numberedVoice(const NumberedComposition &composition, int id);
void validateNumberedComposition(const Score &metadata, const NumberedComposition &composition);
std::int64_t compositionDuration(const NumberedComposition &composition);
int compositionBars(const Score &metadata, const NumberedComposition &composition);
Score compositionPlaybackScore(const Score &metadata, const NumberedComposition &composition);
AccompanimentPlan compositionPlaybackPlan(const Score &metadata, const NumberedComposition &composition);
} // namespace singlilt

// Uniform-meter melody structure and bounded note entry.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "Score.h"

namespace singlilt
{
inline constexpr int MaximumEditorBars = 128;
std::int64_t numberedDuration(const Score &score);
int numberedBarCount(const Score &score);
// Structural editing is limited to uniform-meter melody scores.
bool canRestructureNumberedScore(const Score &score);
bool insertNumberedBar(Score &score, int bar);
bool removeNumberedBar(Score &score, int bar);
// Replaces [bar start, bar end) with silence without compressing later written time.
bool replaceNumberedBarWithRest(Score &score, int bar);
bool replaceNumberedTimeWithRest(Score &score, std::int64_t first, int length);
bool insertNumberedTime(Score &score, std::int64_t boundary, int length);
void removeNumberedTime(Score &score, std::int64_t first, int length);
bool appendNumberedNote(Score &score, std::int64_t tick, Note note, std::int64_t limit = 0);
} // namespace singlilt

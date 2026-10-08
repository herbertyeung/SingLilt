// Braced numbered-notation systems mapped to the existing polyphonic transport.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "StaffPerformance.h"

namespace singlilt
{
struct NumberedSystem
{
    int upperLine = 0;
    int lowerLine = -1;           // -1 is an unbraced, single-voice system.
    std::vector<double> barlines; // Interior barlines in source-image coordinates.
};

// Keeps the upper voice as the practice guide; all written pitches use explicit onsets.
// Shorter measures are padded with rests, never stretched to fit the other hand.
StaffPerformance buildNumberedPerformance(Score &score, const std::vector<NumberedSystem> &systems);

struct NumberedGuideCorrection
{
    Score score;
    StaffPerformance performance;
    std::size_t selectedNote = 0;
};

// Correct the practice guide without dropping or stretching the other written hand.
NumberedGuideCorrection correctedNumberedGuide(const Score &score, const StaffPerformance &performance,
                                               std::size_t noteIndex, Note replacement);
} // namespace singlilt

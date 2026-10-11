// Shared stereo output gain and sample-peak limiting at 48 kHz.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "OutputLevel.h"
#include <algorithm>
#include <cmath>

namespace singlilt
{
void OutputLevel::reset()
{
    attenuation_ = 1.0f;
}

bool OutputLevel::process(float *left, float *right, int frames, int stride, bool enhanced)
{
    if (!left || !right || frames < 0 || (stride != 1 && stride != 2))
        return false;
    if (!enhanced)
    {
        reset();
        return true;
    }
    // Immediate peak protection; a 100 ms release avoids per-channel balance changes.
    constexpr float release = 0.999791688f;
    bool finite = true;
    for (int frame = 0; frame < frames; ++frame, left += stride, right += stride)
    {
        if (!std::isfinite(*left) || !std::isfinite(*right))
        {
            *left = *right = 0.0f;
            finite = false;
            continue;
        }
        const double boostedLeft = double(*left) * EnhancedGain;
        const double boostedRight = double(*right) * EnhancedGain;
        const double peak = std::max(std::abs(boostedLeft), std::abs(boostedRight));
        const float required = peak > PeakCeiling ? float(PeakCeiling / peak) : 1.0f;
        attenuation_ = std::min(required, 1.0f - (1.0f - attenuation_) * release);
        *left = float(boostedLeft * attenuation_);
        *right = float(boostedRight * attenuation_);
    }
    return finite;
}
} // namespace singlilt

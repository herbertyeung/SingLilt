// Shared stereo output gain and sample-peak limiting at 48 kHz.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

namespace singlilt
{
class OutputLevel final
{
  public:
    static constexpr float EnhancedGain = 1.9952623f; // +6 dB.
    static constexpr float PeakCeiling = 0.98f;

    void reset();
    // Planar buffers use stride 1; interleaved left/right pointers use stride 2.
    // One render thread owns this state. The caller supplies the current setting.
    bool process(float *left, float *right, int frames, int stride, bool enhanced);

  private:
    float attenuation_ = 1.0f;
};
} // namespace singlilt

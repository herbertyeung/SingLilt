// Fundamental-frequency estimation from mono audio samples.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <span>

namespace singlilt
{
struct PitchObservation
{
    double seconds = 0.0;
    double durationSeconds = 0.02;
    double frequency = 0.0;
    double midiPitch = -1.0;
    double confidence = 0.0;
    double rms = 0.0;
    bool clipped = false;
};

// Caller supplies mono samples. Frequency remains continuous for cents feedback.
PitchObservation detectSingingPitch(std::span<const float> samples, int sampleRate, double noiseGate = 0.008);
} // namespace singlilt

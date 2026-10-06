// Pitch and rhythm assessment of recorded practice attempts.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "Timeline.h"
#include "audio/PitchDetector.h"
#include <span>
#include <vector>

namespace singlilt
{
struct ExpectedTone
{
    std::size_t sourceNoteIndex = 0;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int midiPitch = 60;
};

enum class SingingVerdict
{
    Accurate,
    High,
    Low,
    WrongOctave,
    Unstable,
    Missing,
    Uncertain
};
struct ToneAssessment
{
    ExpectedTone target;
    SingingVerdict verdict = SingingVerdict::Missing;
    double cents = 0.0;
    double coverage = 0.0;
    double accurateFraction = 0.0;
    double onsetErrorSeconds = 0.0;
    double releaseErrorSeconds = 0.0;
    bool timingDetected = false;
    bool rhythmAccurate = false;
};

struct SingingResult
{
    std::vector<ToneAssessment> tones;
    double pitchPercent = 0.0;
    double rhythmPercent = 0.0;
    double coveragePercent = 0.0;
    bool reliable = false;
};

std::vector<ExpectedTone> expectedSingingTones(const Timeline &timeline, double speed = 1.0);
SingingResult assessSinging(std::span<const ExpectedTone> targets, std::span<const PitchObservation> observations,
                            double centsTolerance = 50.0, double minimumConfidence = 0.85,
                            double timingTolerance = 0.18);
const char *singingVerdictKey(SingingVerdict verdict);
} // namespace singlilt

// Pitch and interval exercise generation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <random>
#include <vector>

namespace singlilt
{
enum class EarExercise
{
    PitchDirection,
    SameMelody,
    ScaleDegree,
    Interval,
    Rhythm,
    SingleEcho,
    PhraseEcho
};

struct EarTone
{
    int midiPitch = 60;
    int durationTicks = 480;
    bool operator==(const EarTone &) const = default;
};

struct EarQuestion
{
    EarExercise type = EarExercise::PitchDirection;
    int tonicMidi = 60;
    std::vector<EarTone> reference;
    std::vector<EarTone> prompt;
    std::vector<std::vector<EarTone>> rhythmOptions;
    std::vector<int> options;
    int correctIndex = 0;
    bool requiresMicrophone() const;
};

EarQuestion makeEarQuestion(EarExercise type, int difficulty, std::mt19937 &random, int tonicMidi = 60);
const char *earExerciseKey(EarExercise type);
} // namespace singlilt

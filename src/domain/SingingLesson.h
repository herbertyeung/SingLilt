// Lesson material, practice goals, and assessment settings.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "EarTraining.h"
#include "Score.h"
#include <string>
#include <vector>

namespace singlilt
{
struct SingingLesson
{
    int id = 0;
    std::string title;
    std::string goal;
    std::string steps;
    std::string selfCheck;
    double centsTolerance = 50.0;
    double timingToleranceSeconds = 0.18;
    std::vector<EarExercise> earExercises;
    Score score;
};
} // namespace singlilt

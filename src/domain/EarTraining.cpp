// Pitch and interval exercise generation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "EarTraining.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace singlilt
{
bool EarQuestion::requiresMicrophone() const
{
    return type == EarExercise::SingleEcho || type == EarExercise::PhraseEcho;
}

const char *earExerciseKey(EarExercise type)
{
    switch (type)
    {
    case EarExercise::PitchDirection:
        return "ui.ear.direction";
    case EarExercise::SameMelody:
        return "ui.ear.same";
    case EarExercise::ScaleDegree:
        return "ui.ear.degree";
    case EarExercise::Interval:
        return "ui.ear.interval";
    case EarExercise::Rhythm:
        return "ui.ear.rhythm";
    case EarExercise::SingleEcho:
        return "ui.ear.single_echo";
    case EarExercise::PhraseEcho:
        return "ui.ear.phrase_echo";
    }
    return "ui.ear.direction";
}

EarQuestion makeEarQuestion(EarExercise type, int difficulty, std::mt19937 &random, int tonicMidi)
{
    if (difficulty < 0 || difficulty > 2 || tonicMidi < 48 || tonicMidi > 72 || static_cast<int>(type) < 0 ||
        static_cast<int>(type) > 6)
        throw std::invalid_argument("Invalid ear-training configuration");
    const auto choose = [&random](int count) { return std::uniform_int_distribution<int>(0, count - 1)(random); };
    constexpr std::array<int, 7> major{0, 2, 4, 5, 7, 9, 11};
    const int degreeCount = difficulty == 0 ? 3 : difficulty == 1 ? 5 : 7;
    EarQuestion question;
    question.type = type;
    question.tonicMidi = tonicMidi;
    switch (type)
    {
    case EarExercise::PitchDirection:
    {
        question.options = {-1, 0, 1};
        question.correctIndex = choose(3);
        const int distance = difficulty == 0 ? 4 + choose(4) : difficulty == 1 ? 2 + choose(3) : 1 + choose(2);
        const int base = tonicMidi + choose(5);
        question.prompt = {{base, 960}, {base + question.options[question.correctIndex] * distance, 960}};
        break;
    }
    case EarExercise::SameMelody:
    {
        const int count = 3 + difficulty;
        for (int i = 0; i < count; ++i)
            question.reference.push_back({tonicMidi + major[choose(degreeCount)], 480});
        question.prompt = question.reference;
        question.options = {1, 0};
        question.correctIndex = choose(2);
        if (question.correctIndex == 1)
            question.prompt[choose(count)].midiPitch += difficulty == 0 ? 4 : 2;
        break;
    }
    case EarExercise::ScaleDegree:
    {
        question.reference = {{tonicMidi, 960}};
        for (int degree = 1; degree <= degreeCount; ++degree)
            question.options.push_back(degree);
        question.correctIndex = choose(degreeCount);
        question.prompt = {{tonicMidi + major[question.correctIndex], 960}};
        break;
    }
    case EarExercise::Interval:
    {
        question.options = difficulty == 0 ? std::vector<int>{2, 3, 5} : std::vector<int>{2, 3, 4, 5, 6, 7, 8};
        question.correctIndex = choose(static_cast<int>(question.options.size()));
        const int degree = question.options[question.correctIndex];
        const int semitones = degree == 8 ? 12 : major[degree - 1];
        question.prompt = {{tonicMidi, 960}, {tonicMidi + semitones, 960}};
        break;
    }
    case EarExercise::Rhythm:
    {
        const std::array<std::vector<int>, 4> patterns{
            {{480, 480, 480, 480}, {240, 240, 480, 480, 480}, {480, 240, 240, 960}, {720, 240, 480, 480}}};
        const int count = difficulty == 0 ? 3 : 4;
        for (int i = 0; i < count; ++i)
        {
            std::vector<EarTone> pattern;
            for (int ticks : patterns[i])
                pattern.push_back({tonicMidi, ticks});
            question.rhythmOptions.push_back(std::move(pattern));
            question.options.push_back(i);
        }
        std::shuffle(question.rhythmOptions.begin(), question.rhythmOptions.end(), random);
        question.correctIndex = choose(count);
        question.prompt = question.rhythmOptions[question.correctIndex];
        break;
    }
    case EarExercise::SingleEcho:
        question.prompt = {{tonicMidi + major[choose(degreeCount)], 1920}};
        break;
    case EarExercise::PhraseEcho:
        for (int i = 0; i < 2 + difficulty; ++i)
            question.prompt.push_back({tonicMidi + major[choose(degreeCount)], 960});
        break;
    }
    return question;
}
} // namespace singlilt

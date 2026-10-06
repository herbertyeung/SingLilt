// Pitch detection, singing assessment, and ear-training regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "SingingTests.h"
#include "audio/PitchDetector.h"
#include "domain/EarTraining.h"
#include "domain/SingingAssessment.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
std::vector<float> tone(double frequency, int rate, bool harmonics = false)
{
    std::vector<float> samples(static_cast<std::size_t>(rate * 0.086));
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const double phase = 2.0 * std::numbers::pi * frequency * i / rate;
        samples[i] =
            static_cast<float>(0.15 * std::sin(phase) +
                               (harmonics ? 0.2 * std::sin(phase * 2.0) + 0.08 * std::sin(phase * 3.0) : 0.0));
    }
    return samples;
}
std::vector<singlilt::PitchObservation> frames(double start, double end, double midi, double confidence = 0.98)
{
    std::vector<singlilt::PitchObservation> observations;
    for (int frame = static_cast<int>(std::lround(start * 50)); frame < static_cast<int>(std::lround(end * 50));
         ++frame)
        observations.push_back({frame / 50.0, 0.02, midi < 0 ? 0 : 440.0 * std::pow(2.0, (midi - 69) / 12.0), midi,
                                confidence, 0.1, false});
    return observations;
}
} // namespace

void singingPitchTests()
{
    using namespace singlilt;
    for (int rate : {16000, 44100, 48000, 96000, 192000})
        for (double frequency : {70.0, 110.0, 220.0, 440.0, 880.0, 1318.51})
        {
            const auto pitch = detectSingingPitch(tone(frequency, rate), rate);
            const double error = 1200.0 * std::log2(pitch.frequency / frequency);
            if (pitch.confidence <= 0.85 || std::abs(error) >= 8.0)
                std::cerr << "PITCH rate=" << rate << " frequency=" << frequency << " detected=" << pitch.frequency
                          << " cents=" << error << " confidence=" << pitch.confidence << '\n';
            check(pitch.confidence > 0.85 && std::abs(error) < 8.0,
                  "Continuous pitch tracks low/high voices and device sample rates");
        }
    for (double cents : {-35.0, 35.0, 75.0})
    {
        const auto pitch = detectSingingPitch(tone(440.0 * std::pow(2.0, cents / 1200.0), 48000), 48000);
        check(std::abs((pitch.midiPitch - 69) * 100.0 - cents) < 3.0,
              "Pitch feedback retains fractional semitones instead of rounding to MIDI");
    }
    const auto harmonic = detectSingingPitch(tone(180.0, 48000, true), 48000);
    check(std::abs(1200 * std::log2(harmonic.frequency / 180.0)) < 8.0,
          "A stronger second harmonic does not become a false octave");
    std::vector<float> silence(4128, 0.0f);
    check(detectSingingPitch(silence, 48000).midiPitch < 0.0, "Silence does not invent pitch");
    std::mt19937 random(14);
    for (auto &sample : silence)
        sample = std::uniform_real_distribution<float>(-0.3f, 0.3f)(random);
    check(detectSingingPitch(silence, 48000).midiPitch < 0.0, "Unvoiced noise remains uncertain");
    for (std::size_t i = 0; i < silence.size(); ++i)
        silence[i] = i % 2 ? 1.0f : -1.0f;
    check(detectSingingPitch(silence, 48000).clipped && detectSingingPitch(silence, 48000).midiPitch < 0.0,
          "Clipped audio is not graded as reliable pitch");
    silence[0] = std::numeric_limits<float>::quiet_NaN();
    check(detectSingingPitch(silence, 48000).midiPitch < 0.0, "Malformed samples are rejected");
}

void singingAssessmentTests()
{
    using namespace singlilt;
    const std::vector<ExpectedTone> targets{{0, 0.0, 1.0, 69}};
    auto result = assessSinging(targets, frames(0.0, 1.0, 69.0));
    check(result.reliable && result.pitchPercent > 99.9 && result.rhythmPercent == 100.0,
          "Matching pitch and timing produce complete feedback");
    check(assessSinging(targets, frames(0, 1, 69.75)).tones[0].verdict == SingingVerdict::High,
          "Sharp singing is reported high");
    check(assessSinging(targets, frames(0, 1, 68.25)).tones[0].verdict == SingingVerdict::Low,
          "Flat singing is reported low");
    result = assessSinging(targets, frames(0, 1, 81));
    check(result.tones[0].verdict == SingingVerdict::WrongOctave && result.pitchPercent == 0,
          "Octave errors are not silently accepted as pitch-class matches");
    result = assessSinging(targets, {});
    check(!result.reliable && result.tones[0].verdict == SingingVerdict::Missing && result.pitchPercent == 0,
          "Not singing cannot earn a high score");
    check(assessSinging(targets, frames(0, 1, -1, 0)).tones[0].verdict == SingingVerdict::Uncertain,
          "Energetic noise is distinguished from missed singing");
    result = assessSinging(targets, frames(0.3, 1.0, 69));
    check(result.pitchPercent < 80 && result.rhythmPercent == 0 && result.tones[0].onsetErrorSeconds > 0.25,
          "Late entries are scored separately from pitch and cannot hide missing coverage");
    result = assessSinging(targets, frames(0.0, 0.6, 69));
    check(result.rhythmPercent == 0 && result.tones[0].releaseErrorSeconds < -0.3,
          "Shortened sustained notes are reported");
    result = assessSinging(std::vector<ExpectedTone>{{0, 0.5, 1.5, 69}}, frames(0.2, 1.8, 69));
    check(result.rhythmPercent == 0 && result.tones[0].onsetErrorSeconds < -0.25 &&
              result.tones[0].releaseErrorSeconds > 0.25,
          "Early entries and late releases exceed the tolerance");
    auto wavering = frames(0.0, 1.0, 69.0);
    for (std::size_t i = 0; i < wavering.size(); ++i)
        wavering[i].midiPitch += i % 2 ? 1.0 : -1.0;
    check(assessSinging(targets, wavering).tones[0].verdict == SingingVerdict::Unstable,
          "Sharp and flat oscillation does not average into an accurate sustained tone");
    auto noisy = frames(0, 1, 69, 0.5);
    check(!assessSinging(targets, noisy).reliable, "Low-confidence detection has no reliable score");
    auto stable = frames(0, 1, 69.35);
    check(assessSinging(targets, stable, 50).pitchPercent > 99 &&
              assessSinging(targets, stable, 25).pitchPercent == 0,
          "Configured cents tolerances affect grading");
    bool rejected = false;
    stable[1].seconds = stable[0].seconds;
    try
    {
        assessSinging(targets, stable);
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    check(rejected, "Overlapping observations cannot double-count coverage");
    Score score;
    Note first;
    first.durationTicks = 480;
    first.tieToNext = true;
    Note next = first;
    next.tieToNext = false;
    score.notes = {first, next};
    const auto tied = expectedSingingTones(buildTimeline(score), 0.5);
    check(tied.size() == 1 && std::abs(tied[0].endSeconds - 8.0 / 3.0) < 1e-6,
          "Ties and playback speed share the score timeline");
}

void earTrainingTests()
{
    using namespace singlilt;
    std::mt19937 left(1234), right(1234);
    for (int type = 0; type <= 6; ++type)
        for (int difficulty = 0; difficulty < 3; ++difficulty)
            for (int iteration = 0; iteration < 30; ++iteration)
            {
                const auto question = makeEarQuestion(static_cast<EarExercise>(type), difficulty, left, 57);
                const auto repeat = makeEarQuestion(static_cast<EarExercise>(type), difficulty, right, 57);
                check(question.prompt == repeat.prompt && question.correctIndex == repeat.correctIndex,
                      "Question generation is reproducible with a seed");
                check(!question.prompt.empty(), "Every question has a playable example");
                for (const auto &note : question.prompt)
                    check(note.midiPitch >= 48 && note.midiPitch <= 84 && note.durationTicks > 0,
                          "Generated questions remain within configured bounds");
                if (!question.requiresMicrophone())
                    check(question.correctIndex >= 0 &&
                              question.correctIndex < static_cast<int>(question.options.size()),
                          "Choice questions have a valid answer");
                if (question.type == EarExercise::SameMelody)
                    check((question.prompt == question.reference) == (question.correctIndex == 0),
                          "Same/different answers agree with the sounded notes");
                if (question.type == EarExercise::ScaleDegree)
                    check(question.reference.size() == 1 && question.reference[0].midiPitch == 57,
                          "Scale degree exercises provide a tonic reference");
                if (question.type == EarExercise::Rhythm)
                {
                    check(question.prompt == question.rhythmOptions[question.correctIndex],
                          "Rhythm answers match the actually played sequence");
                    for (std::size_t i = 0; i < question.rhythmOptions.size(); ++i)
                        for (std::size_t j = i + 1; j < question.rhythmOptions.size(); ++j)
                            check(question.rhythmOptions[i] != question.rhythmOptions[j],
                                  "Rhythm options are distinct");
                }
            }
}

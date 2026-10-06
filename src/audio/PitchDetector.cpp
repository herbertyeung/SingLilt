// Fundamental-frequency estimation from mono audio samples.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "PitchDetector.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace singlilt
{
PitchObservation detectSingingPitch(std::span<const float> samples, int sampleRate, double noiseGate)
{
    PitchObservation observation;
    if (sampleRate < 8000 || sampleRate > 192000 || samples.size() < 512 || samples.size() > 32768 ||
        !std::isfinite(noiseGate) || noiseGate < 0.0 || noiseGate > 1.0)
        return observation;
    // A small fixed analysis rate bounds work even on 192 kHz input.
    constexpr int analysisRate = 12000;
    const int count = static_cast<int>(samples.size() * double(analysisRate) / sampleRate);
    if (count < 384)
        return observation;
    std::vector<double> mono(static_cast<std::size_t>(count));
    double sum = 0.0, squares = 0.0;
    int saturated = 0;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const double sample = samples[i];
        if (!std::isfinite(sample))
            return observation;
        sum += sample;
        squares += sample * sample;
        saturated += std::abs(sample) >= 0.995;
    }
    const double mean = sum / samples.size();
    observation.rms = std::sqrt(std::max(0.0, squares / samples.size() - mean * mean));
    observation.clipped = saturated > static_cast<int>(samples.size() / 100);
    if (observation.rms < noiseGate || observation.clipped)
        return observation;
    for (int i = 0; i < count; ++i)
    {
        const double start = i * double(sampleRate) / analysisRate;
        const double end = std::min(double(samples.size()), (i + 1) * double(sampleRate) / analysisRate);
        double total = 0.0;
        // Box integration reduces high-frequency aliasing when downsampling.
        for (int j = static_cast<int>(start); j < static_cast<int>(std::ceil(end)); ++j)
            total += (samples[static_cast<std::size_t>(j)] - mean) *
                     (std::min(end, double(j + 1)) - std::max(start, double(j)));
        mono[static_cast<std::size_t>(i)] = total / (end - start);
    }
    const int minimumLag = analysisRate / 1500;
    const int maximumLag = std::min(analysisRate / 65, count / 2 - 1);
    const int comparisonCount = count - maximumLag;
    std::vector<double> difference(static_cast<std::size_t>(maximumLag + 1), 1.0);
    std::vector<double> squaredDifference(static_cast<std::size_t>(maximumLag + 1), 0.0);
    double accumulated = 0.0;
    for (int lag = 1; lag <= maximumLag; ++lag)
    {
        double error = 0.0;
        for (int i = 0; i < comparisonCount; ++i)
        {
            const double delta = mono[static_cast<std::size_t>(i)] - mono[static_cast<std::size_t>(i + lag)];
            error += delta * delta;
        }
        accumulated += error;
        squaredDifference[static_cast<std::size_t>(lag)] = error;
        difference[static_cast<std::size_t>(lag)] = accumulated > 1e-12 ? error * lag / accumulated : 1.0;
    }
    int selected = -1;
    for (int lag = minimumLag; lag < maximumLag; ++lag)
    {
        if (difference[static_cast<std::size_t>(lag)] >= 0.15)
            continue;
        while (lag + 1 < maximumLag &&
               difference[static_cast<std::size_t>(lag + 1)] < difference[static_cast<std::size_t>(lag)])
            ++lag;
        selected = lag;
        break;
    }
    if (selected < 0)
        return observation;
    double period = selected;
    // Interpolate the unnormalized minimum: cumulative normalization biases
    // short periods enough to affect high-note cents feedback.
    const double left = squaredDifference[static_cast<std::size_t>(selected - 1)];
    const double center = squaredDifference[static_cast<std::size_t>(selected)];
    const double right = squaredDifference[static_cast<std::size_t>(selected + 1)];
    const double denominator = left - 2.0 * center + right;
    if (std::abs(denominator) > 1e-12)
        period += std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5);
    observation.frequency = analysisRate / period;
    observation.midiPitch = 69.0 + 12.0 * std::log2(observation.frequency / 440.0);
    observation.confidence = std::clamp(1.0 - difference[static_cast<std::size_t>(selected)], 0.0, 1.0);
    return observation;
}
} // namespace singlilt

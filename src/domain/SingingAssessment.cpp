// Pitch and rhythm assessment of recorded practice attempts.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "SingingAssessment.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
std::vector<ExpectedTone> expectedSingingTones(const Timeline &timeline, double speed)
{
    if (!timeline.valid() || !std::isfinite(speed) || speed < 0.25 || speed > 2.0)
        throw std::invalid_argument("Invalid singing timeline");
    std::vector<ExpectedTone> tones;
    for (const auto &event : timeline.events)
    {
        if (event.midiPitch < 0)
            continue;
        const double start = timeline.secondsAtTick(event.startTick) / speed;
        const double end = timeline.secondsAtTick(event.startTick + event.durationTicks) / speed;
        if (!event.attack && !tones.empty() && tones.back().midiPitch == event.midiPitch &&
            std::abs(tones.back().endSeconds - start) < 1e-6)
            tones.back().endSeconds = end;
        else
            tones.push_back({event.sourceNoteIndex, start, end, event.midiPitch});
    }
    return tones;
}

const char *singingVerdictKey(SingingVerdict verdict)
{
    switch (verdict)
    {
    case SingingVerdict::Accurate:
        return "ui.singing.accurate";
    case SingingVerdict::High:
        return "ui.singing.high";
    case SingingVerdict::Low:
        return "ui.singing.low";
    case SingingVerdict::WrongOctave:
        return "ui.singing.octave";
    case SingingVerdict::Unstable:
        return "ui.singing.unstable";
    case SingingVerdict::Missing:
        return "ui.singing.missing";
    case SingingVerdict::Uncertain:
        return "ui.singing.uncertain";
    }
    return "ui.singing.uncertain";
}

SingingResult assessSinging(std::span<const ExpectedTone> targets, std::span<const PitchObservation> observations,
                            double centsTolerance, double minimumConfidence, double timingTolerance)
{
    if (!std::isfinite(centsTolerance) || centsTolerance < 10.0 || centsTolerance > 100.0 ||
        !std::isfinite(minimumConfidence) || minimumConfidence < 0.5 || minimumConfidence > 1.0 ||
        !std::isfinite(timingTolerance) || timingTolerance < 0.05 || timingTolerance > 0.5 ||
        targets.size() > 10000 || observations.size() > 60000)
        throw std::invalid_argument("Invalid singing assessment configuration");
    double previousEnd = 0.0;
    for (const auto &target : targets)
    {
        if (!std::isfinite(target.startSeconds) || !std::isfinite(target.endSeconds) ||
            target.startSeconds < previousEnd - 1e-6 || target.endSeconds <= target.startSeconds ||
            target.midiPitch < 0 || target.midiPitch > 127)
            throw std::invalid_argument("Invalid singing targets");
        previousEnd = target.endSeconds;
    }
    double previousTime = -1.0;
    for (const auto &frame : observations)
    {
        if (!std::isfinite(frame.seconds) || frame.seconds < 0.0 || frame.seconds < previousTime ||
            !std::isfinite(frame.durationSeconds) || frame.durationSeconds <= 0.0 || frame.durationSeconds > 0.1 ||
            !std::isfinite(frame.midiPitch) || !std::isfinite(frame.confidence) || frame.confidence < 0.0 ||
            frame.confidence > 1.0 || !std::isfinite(frame.rms))
            throw std::invalid_argument("Invalid singing observation");
        previousTime = frame.seconds + frame.durationSeconds - 1e-6;
    }
    SingingResult result;
    double total = 0.0, detected = 0.0, accurate = 0.0;
    int rhythmCorrect = 0;
    std::size_t firstFrame = 0;
    const double timingSearch = timingTolerance * 2.0;
    for (const auto &target : targets)
    {
        ToneAssessment tone;
        tone.target = target;
        const double duration = target.endSeconds - target.startSeconds;
        const double trim = std::min(0.08, duration * 0.1);
        const double stableStart = target.startSeconds + trim;
        const double stableEnd = target.endSeconds - trim;
        const double stableDuration = stableEnd - stableStart;
        double usable = 0.0, correct = 0.0, energetic = 0.0, centSum = 0.0, octave = 0.0;
        double onset = -1.0, release = -1.0;
        while (firstFrame < observations.size() &&
               observations[firstFrame].seconds + observations[firstFrame].durationSeconds <
                   target.startSeconds - timingSearch)
            ++firstFrame;
        for (std::size_t i = firstFrame; i < observations.size(); ++i)
        {
            const auto &frame = observations[i];
            if (frame.seconds > target.endSeconds + timingSearch)
                break;
            const double end = frame.seconds + frame.durationSeconds;
            const double overlap = std::max(0.0, std::min(end, stableEnd) - std::max(frame.seconds, stableStart));
            if (frame.rms >= 0.008)
                energetic += overlap;
            if (frame.midiPitch < 0.0 || frame.confidence < minimumConfidence || frame.clipped)
                continue;
            const double cents = (frame.midiPitch - target.midiPitch) * 100.0;
            if (overlap > 0.0)
            {
                usable += overlap;
                centSum += cents * overlap;
                if (std::abs(cents) <= centsTolerance)
                    correct += overlap;
                if (std::abs(std::abs(cents) - 1200.0) <= centsTolerance)
                    octave += overlap;
            }
            // Nearby events must not borrow each other's timing or fill missing notes.
            const double timingStart =
                target.startSeconds -
                (result.tones.empty()
                     ? timingSearch
                     : std::min(timingSearch,
                                (target.startSeconds - targets[result.tones.size() - 1].endSeconds) * 0.5));
            const std::size_t next = result.tones.size() + 1;
            const double timingEnd =
                target.endSeconds +
                (next == targets.size()
                     ? timingSearch
                     : std::min(timingSearch, (targets[next].startSeconds - target.endSeconds) * 0.5));
            if (end > timingStart && frame.seconds < timingEnd && std::abs(cents) < 150.0)
            {
                if (onset < 0.0)
                    onset = std::max(frame.seconds, timingStart);
                release = std::min(end, timingEnd);
            }
        }
        tone.coverage = std::clamp(usable / stableDuration, 0.0, 1.0);
        tone.accurateFraction = std::clamp(correct / stableDuration, 0.0, 1.0);
        tone.cents = usable > 0.0 ? centSum / usable : 0.0;
        if (tone.coverage < 0.5)
            tone.verdict = energetic / stableDuration > 0.2 ? SingingVerdict::Uncertain : SingingVerdict::Missing;
        else if (octave > usable * 0.5)
            tone.verdict = SingingVerdict::WrongOctave;
        else if (correct / usable >= 0.75)
            tone.verdict = SingingVerdict::Accurate;
        else if (std::abs(tone.cents) <= centsTolerance)
            tone.verdict = SingingVerdict::Unstable;
        else
            tone.verdict = tone.cents >= 0.0 ? SingingVerdict::High : SingingVerdict::Low;
        tone.timingDetected = onset >= 0.0;
        if (tone.timingDetected)
        {
            tone.onsetErrorSeconds = onset - target.startSeconds;
            tone.releaseErrorSeconds = release - target.endSeconds;
            tone.rhythmAccurate = std::abs(tone.onsetErrorSeconds) <= timingTolerance &&
                                  std::abs(tone.releaseErrorSeconds) <= timingTolerance;
        }
        rhythmCorrect += tone.rhythmAccurate;
        total += stableDuration;
        detected += usable;
        accurate += correct;
        result.tones.push_back(tone);
    }
    if (total > 0.0)
    {
        result.pitchPercent = std::clamp(100.0 * accurate / total, 0.0, 100.0);
        result.coveragePercent = std::clamp(100.0 * detected / total, 0.0, 100.0);
        result.rhythmPercent = 100.0 * rhythmCorrect / targets.size();
        result.reliable = result.coveragePercent >= 50.0;
    }
    return result;
}
} // namespace singlilt

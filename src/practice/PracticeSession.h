// Lesson recording sessions and assessment coordination.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "audio/MicrophoneCapture.h"
#include "domain/SingingAssessment.h"
#include <memory>
#include <vector>

namespace singlilt
{
class PracticeSession final
{
  public:
    enum class State
    {
        Idle,
        Starting,
        Monitoring,
        Recording,
        Finished,
        Failed
    };
    struct Snapshot
    {
        State state = State::Idle;
        QString error;
        PitchObservation latest;
        std::vector<PitchObservation> observations;
        double noiseGate = 0.008;
        double clockSeconds = 0.0;
        double recordStartSeconds = 0.0;
    };
    PracticeSession();
    ~PracticeSession();
    PracticeSession(const PracticeSession &) = delete;
    PracticeSession &operator=(const PracticeSession &) = delete;
    std::vector<MicrophoneDevice> devices();
    void calibrateNoise(const QString &deviceId);
    // Capturing begins now; only samples within the future countdown-aligned span are retained.
    void record(const QString &deviceId, double startClockSeconds, double durationSeconds,
                double latencySeconds = 0.0);
    void stop();
    Snapshot snapshot() const;
    std::vector<PitchObservation> observations() const;
    bool hasRecording() const;
    void exportWave(const QString &path) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace singlilt

// Repeat expansion and source-note playback occurrences.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "Score.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace singlilt
{

enum class DiagnosticSeverity
{
    Warning,
    Error
};

struct Diagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    int noteIndex = -1;
};

struct TimelineEvent
{
    std::size_t sourceNoteIndex = 0;
    std::int64_t startTick = 0;
    std::int64_t durationTicks = 0;
    int midiPitch = -1; // -1 is silence.
    bool attack = true; // False for a continuation of a valid tie.
    int verseIndex = 0; // Repeat pass: 0=A, 1=B; carried into the following tail.
    // A tied continuation retains its attack's program, even across a pass change.
    int program = 0;
    std::string lyric;
    // Deterministic performed attack strength: 0 for rest; ties retain attack value.
    int velocity = 100;
};

struct MetronomeBeat
{
    std::int64_t startTick = 0;
    bool downbeat = false;
};

struct Timeline
{
    std::vector<TimelineEvent> events;
    std::vector<Diagnostic> diagnostics;
    std::int64_t durationTicks = 0;
    double bpm = 90.0;

    bool valid() const;
    double durationSeconds() const;
    double secondsAtTick(std::int64_t tick) const;
    std::int64_t tickAtSeconds(double seconds) const;
    std::int64_t clampTick(std::int64_t tick) const;
    std::optional<std::size_t> eventIndexAtTick(std::int64_t tick) const;
    std::vector<MetronomeBeat> metronomeBeats;
};

Timeline buildTimeline(const Score &score, int transpose = 0);

} // namespace singlilt

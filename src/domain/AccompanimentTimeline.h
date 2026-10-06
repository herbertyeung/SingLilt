// Chord and bass events on the expanded playback timeline.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "Accompaniment.h"

#include <cstdint>
#include <vector>

namespace singlilt
{

struct AccompanimentEvent
{
    std::int64_t startTick = 0;
    std::int64_t durationTicks = 0;
    int midiPitch = -1; // Untransposed MIDI pitch, as in the source score.
    int velocity = 62;
    AccompanimentRole role = AccompanimentRole::Chord;
};

struct AccompanimentPlan
{
    std::vector<AccompanimentEvent> events;
    std::int64_t durationTicks = 0;
    std::vector<Diagnostic> diagnostics;

    bool valid() const;
};

// Melody timeline owns playback order and lyrics; this plan is an independent track.
AccompanimentPlan buildAccompanimentPlan(const Score &score, const Timeline &timeline,
                                         const AccompanimentArrangement &arrangement);

} // namespace singlilt

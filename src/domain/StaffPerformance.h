// Complete staff voices, measures, ties, and performance validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "AccompanimentTimeline.h"
#include "Score.h"
#include <cstdint>
#include <string>
#include <vector>

namespace singlilt
{
enum class StaffBeamKind
{
    Begin,
    Continue,
    End,
    ForwardHook,
    BackwardHook
};

struct StaffBeam
{
    int level = 1;
    StaffBeamKind kind = StaffBeamKind::Begin;
};

enum class StaffStemDirection
{
    Auto,
    Up,
    Down,
    None
};

struct StaffPerformanceNote
{
    std::int64_t startTick = 0;
    std::int64_t durationTicks = TicksPerQuarter;
    int midiPitch = 60;
    int staff = 1;
    std::string voice = "1";
    int velocity = 88;
    int sourceNoteIndex = -1;
    bool tieStart = false;
    bool tieStop = false;
    SourceRect source;
    std::optional<StaffSpelling> staffSpelling;
    int pageIndex = 0;
    bool unresolvedSoundTie = false;
    std::vector<StaffBeam> beams;
    StaffStemDirection stemDirection = StaffStemDirection::Auto;
    bool hasImageAnchor = true;
};

struct StaffClefChange
{
    std::int64_t startTick = 0;
    int staff = 1;
    bool bassClef = false;
};

struct StaffPerformance
{
    // All written pitches, including the lead line; the Score is its practice guide.
    std::vector<StaffPerformanceNote> notes;
    int staffCount = 2;
    int primaryStaff = 1;
    int sourceTonic = 0;
    std::int64_t durationTicks = 0;
    std::string timingFingerprint;
    int primaryProgram = 0;
    int otherProgram = 0;
    std::vector<StaffClefChange> clefChanges;
};

std::string staffTimingFingerprint(const Score &score);
AccompanimentPlan buildStaffPerformancePlan(const Score &score, const Timeline &timeline,
                                            const StaffPerformance &performance);
} // namespace singlilt

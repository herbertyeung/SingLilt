// Chord arrangements, candidate generation, and validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "Timeline.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace singlilt
{

enum class AccompanimentPattern
{
    BlockChords,
    Arpeggio,
    Sparse
};
enum class HarmonicMode
{
    Automatic,
    Major,
    Minor
};
enum class ChordQuality
{
    Major,
    Minor,
    Diminished,
    None
};
enum class AccompanimentRole
{
    Chord,
    Bass
};

struct AccompanimentSettings
{
    AccompanimentPattern pattern = AccompanimentPattern::BlockChords;
    HarmonicMode mode = HarmonicMode::Automatic;
    int harmonicTonic = -1; // Absolute pitch class; -1 analyzes the source key context.
    int chordProgram = 0;
    int bassProgram = 0;
    int chordVelocity = 62;
    int bassVelocity = 68;
    // Runtime routing only; original staff notes replace the guide melody.
    bool originalStaff = false;
};

struct ChordSpan
{
    std::int64_t startTick = 0;
    std::int64_t endTick = 0; // Half-open ticks in the unexpanded score.
    int rootPitchClass = 0;
    ChordQuality quality = ChordQuality::Major;
    int inversion = 0;
    bool userEdited = false;
    std::optional<AccompanimentPattern> patternOverride;
};

struct AccompanimentArrangement
{
    AccompanimentSettings settings;
    std::vector<ChordSpan> chords;
    std::string melodyFingerprint;
    int generatorVersion = 1;
    std::vector<Diagnostic> diagnostics;
    std::string name;
    bool userAuthored = false;

    bool valid() const;
};

struct AccompanimentCandidate
{
    std::string id;
    std::string labelKey;
    AccompanimentArrangement arrangement;
};

struct SourceMeasureRange
{
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
    int measure = 0; // Zero-based written label, or a cumulative fallback index.
    bool pickup = false;
};

struct TonalityAt
{
    HarmonicMode mode = HarmonicMode::Major; // Resolves Automatic to Major or Minor.
    int tonic = 0;                           // Absolute pitch class, independent of the numbered-note mapping.
};

struct PracticeMix
{
    bool melodyEnabled = true;
    bool accompanimentEnabled = false;
    double melodyVolume = 0.9;
    double accompanimentVolume = 0.55;
};

// Stable across lyric, display, tempo, dynamics and playback-transpose changes.
std::string accompanimentFingerprint(const Score &score);
AccompanimentArrangement generateAccompaniment(const Score &score, const AccompanimentSettings &settings = {});
std::vector<AccompanimentCandidate> generateAccompanimentCandidates(const Score &score,
                                                                    const AccompanimentSettings &settings = {});
std::vector<SourceMeasureRange> sourceMeasureRanges(const Score &score);
// Uses the same key-segment analysis as generation, including relative minor.
TonalityAt resolveHarmonicTonality(const Score &score, const AccompanimentSettings &settings,
                                   std::int64_t sourceTick);
// Empty/gapped arrangements are valid silence; overlap and stale fingerprints fail.
std::vector<Diagnostic> validateAccompaniment(const Score &score, const AccompanimentArrangement &arrangement);

} // namespace singlilt

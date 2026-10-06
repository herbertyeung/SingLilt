// Score types, pitch conversion, and musical-range validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace singlilt
{

inline constexpr int TicksPerQuarter = 480;
inline constexpr std::size_t MaximumScoreNotes = 100000;
inline constexpr double MinimumScoreBpm = 1.0;
inline constexpr double MaximumScoreBpm = 1000.0;
inline constexpr int MaximumNoteDurationTicks = TicksPerQuarter * 256;
inline constexpr int MaximumBeatsPerBar = 64;
inline constexpr int MaximumBeatUnit = 128;

struct SourceRect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct StaffSpelling
{
    char step = 'C';
    int alter = 0;
    int octave = 4;
};

struct WrittenMeasure
{
    std::int64_t startTick = 0;
    std::int64_t durationTicks = TicksPerQuarter * 4;
    int number = 0;
    int beatsPerBar = 4;
    int beatUnit = 4;
    int pageIndex = 0;
};

struct Note
{
    int id = 0;
    int degree = 1; // 0 is a rest, 1..7 are scale degrees.
    int octave = 0; // 0 places degree 1 at MIDI C4 (60), before key changes.
    int accidental = 0;
    int durationTicks = TicksPerQuarter;
    SourceRect source;
    int measure = 0;
    int line = 0;
    std::string lyric;
    double confidence = 1.0;
    int keyOverride = -1; // -1 retains the previous key; 0..11 changes tonic.
    bool tieToNext = false;
    // Explicit verse slots are authoritative, including empty A/B entries.
    // Empty vector uses the legacy lyric string (newline or slash separated).
    std::vector<std::string> verseLyrics;
    // Preserve enharmonic spelling when the written pitch still matches the sound.
    std::optional<StaffSpelling> staffSpelling;
    int pageIndex = 0;
    bool hasImageAnchor = true;
};

// Half-open source-note interval. A first ending is played on the first pass
// only; later passes stop at firstEndingNote, then continue at endNote.
// The second ending, when present, starts at endNote in normal source order.
struct RepeatSection
{
    std::size_t firstNote = 0;
    std::size_t endNote = 0;
    int count = 2;
    int firstEndingNote = -1;
};

struct Score
{
    std::string title;
    std::string imagePath;
    int tonic = 0;
    double bpm = 90.0; // Quarter notes per minute, including in compound meter.
    int beatsPerBar = 4;
    int beatUnit = 4;
    std::vector<Note> notes;
    std::vector<RepeatSection> repeats;
    std::vector<int> versePrograms{0, 4}; // General MIDI: piano A, electric piano B.
    int baseVelocity = 88;                // User-controlled attack strength, not recognized notation.
    // Written-measure downbeat +8, beat +0, subdivision -4; first short pickup +0.
    bool accentBeats = true;
    std::vector<WrittenMeasure> writtenMeasures;
};

int midiPitch(const Note &note, int tonic, int transpose = 0);
int ticksPerBar(const Score &score);
int ticksPerBar(const WrittenMeasure &measure);
// Compound meters pulse on a dotted quarter, simple meters on the denominator.
int ticksPerMetronomeBeat(const Score &score);
int ticksPerMetronomeBeat(const WrittenMeasure &measure);
std::vector<std::string> lyricVerses(const Note &note);
// A singleton is shared; a missing/empty slot in multiple verses stays empty.
std::string lyricForVerse(const Note &note, int verseIndex);
// Higher verses retain the final configured instrument; negative index uses A.
int programForVerse(const Score &score, int verseIndex);

} // namespace singlilt

// Score types, pitch conversion, and musical-range validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "Score.h"

#include <algorithm>
#include <array>
#include <utility>

namespace singlilt
{

int midiPitch(const Note &note, int tonic, int transpose)
{
    static constexpr std::array<int, 7> majorScale{0, 2, 4, 5, 7, 9, 11};
    if (note.degree < 1 || note.degree > 7 || tonic < 0 || tonic > 11)
        return -1;
    // Use a wide intermediate so even malformed imported integers are harmless.
    const auto pitch = 60LL + tonic + majorScale[static_cast<std::size_t>(note.degree - 1)] + 12LL * note.octave +
                       note.accidental + static_cast<long long>(transpose);
    return pitch >= 0 && pitch <= 127 ? static_cast<int>(pitch) : -1;
}

int ticksPerBar(const Score &score)
{
    WrittenMeasure measure;
    measure.beatsPerBar = score.beatsPerBar;
    measure.beatUnit = score.beatUnit;
    return ticksPerBar(measure);
}

int ticksPerBar(const WrittenMeasure &measure)
{
    if (measure.beatsPerBar <= 0 || measure.beatsPerBar > MaximumBeatsPerBar || measure.beatUnit <= 0 ||
        measure.beatUnit > MaximumBeatUnit || (measure.beatUnit & (measure.beatUnit - 1)) != 0)
        return 0;
    return measure.beatsPerBar * (TicksPerQuarter * 4 / measure.beatUnit);
}

int ticksPerMetronomeBeat(const Score &score)
{
    WrittenMeasure measure;
    measure.beatsPerBar = score.beatsPerBar;
    measure.beatUnit = score.beatUnit;
    return ticksPerMetronomeBeat(measure);
}

int ticksPerMetronomeBeat(const WrittenMeasure &measure)
{
    if (ticksPerBar(measure) == 0)
        return 0;
    const auto unitTicks = TicksPerQuarter * 4 / measure.beatUnit;
    // 6/8, 9/8, 12/8 (and corresponding compound subdivisions) have triplet pulses.
    const auto compound = measure.beatsPerBar >= 6 && measure.beatsPerBar % 3 == 0 && measure.beatUnit >= 8;
    return unitTicks * (compound ? 3 : 1);
}

std::vector<std::string> lyricVerses(const Note &note)
{
    if (!note.verseLyrics.empty())
        return note.verseLyrics;
    std::vector<std::string> verses;
    std::size_t first = 0;
    while (true)
    {
        const auto separator = note.lyric.find_first_of("\n/", first);
        auto part = note.lyric.substr(first, separator == std::string::npos ? separator : separator - first);
        if (!part.empty() && part.back() == '\r')
            part.pop_back(); // Preserve verse slots when migrating Windows CRLF text.
        verses.push_back(std::move(part));
        if (separator == std::string::npos)
            break;
        first = separator + 1;
    }
    return verses;
}

std::string lyricForVerse(const Note &note, int verseIndex)
{
    const auto select = [verseIndex](const std::vector<std::string> &verses) -> std::string
    {
        if (verseIndex < 0 || verses.empty())
            return {};
        if (verses.size() == 1)
            return verses.front();
        return static_cast<std::size_t>(verseIndex) < verses.size() ? verses[verseIndex] : std::string{};
    };
    return note.verseLyrics.empty() ? select(lyricVerses(note)) : select(note.verseLyrics);
}

int programForVerse(const Score &score, int verseIndex)
{
    if (score.versePrograms.empty())
        return 0; // Invalid scores are rejected by buildTimeline, not by this accessor.
    const auto index = std::min(static_cast<std::size_t>(std::max(verseIndex, 0)), score.versePrograms.size() - 1);
    return score.versePrograms[index];
}

} // namespace singlilt

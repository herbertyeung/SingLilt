// Mappings between original-audio time and written-score time.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace singlilt
{
struct Score;
std::string audioTimingFingerprint(const Score &score);
struct AudioNoteTiming
{
    int sourceNoteIndex = 0;
    double startSeconds = 0;
    double endSeconds = 0;
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
};

struct AudioLyricTiming
{
    std::string text;
    double startSeconds = 0;
    double endSeconds = 0;
    int sourceNoteIndex = -1;
};

struct AudioSourceInfo
{
    std::string path;
    double durationSeconds = 0;
    double selectedStartSeconds = 0;
    double selectedEndSeconds = 0;
    std::vector<AudioNoteTiming> timings;
    std::vector<AudioLyricTiming> lyricTimings;
    std::string timingFingerprint;
    std::string vocalsPath;
    std::string instrumentalPath;
    std::string separationModel;
    bool vocalsSeparated = false;
};
} // namespace singlilt

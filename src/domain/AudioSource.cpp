// Mappings between original-audio time and written-score time.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AudioSource.h"
#include "Score.h"
#include <iomanip>
#include <sstream>

namespace singlilt
{
std::string audioTimingFingerprint(const Score &score)
{
    std::uint64_t hash = 14695981039346656037ULL;
    const auto append = [&hash](std::uint64_t number)
    {
        for (int byte = 0; byte < 8; ++byte)
        {
            hash ^= (number >> (byte * 8)) & 0xff;
            hash *= 1099511628211ULL;
        }
    };
    append(score.notes.size());
    for (const auto &note : score.notes)
        append(static_cast<std::uint64_t>(note.durationTicks));
    append(score.repeats.size());
    for (const auto &repeat : score.repeats)
    {
        append(repeat.firstNote);
        append(repeat.endNote);
        append(static_cast<std::uint64_t>(repeat.count));
        append(static_cast<std::uint64_t>(repeat.firstEndingNote));
    }
    std::ostringstream text;
    text << "audio1:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return text.str();
}
} // namespace singlilt

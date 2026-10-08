// Bounded, cancellable Linux audio decoding to mono PCM.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <atomic>
#include <vector>

namespace singlilt
{
struct AudioDecodeCancelled
{
};

struct LinuxDecodedAudio
{
    std::vector<float> samples;
    double duration = 0.0;
    double start = 0.0;
    double end = 0.0;
};

LinuxDecodedAudio decodeLinuxAudio(const QString &path, double start, double end,
                                   const std::atomic_bool *cancellation = nullptr);
} // namespace singlilt

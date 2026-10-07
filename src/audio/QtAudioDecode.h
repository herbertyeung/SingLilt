// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
#include <atomic>
#include <memory>
#include <vector>
namespace singlilt
{
std::vector<float> decodeMonoAudio(const QString &path, int sampleRate, double startSeconds, double endSeconds,
                                   double &durationSeconds, const std::shared_ptr<std::atomic_bool> &cancellation);
}

// Local vocal separation and stem-cache validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QtGlobal>
#include <atomic>
#include <functional>
#include <memory>

namespace singlilt
{

struct VocalSeparationOptions
{
    QString pythonExecutable;
    QString workerScript;
    QString modelDirectory;
    QString cacheDirectory;
};

struct VocalSeparationResult
{
    QString originalPath;
    QString vocalsPath;
    QString instrumentalPath;
    QString model;
    QString manifestPath;
    QString inputSha256;
    double durationSeconds = 0.0;
    double separationSeconds = 0.0; // This invocation, including cache validation when reused.
    int sampleRate = 0;
    int channels = 0;
    qint64 frames = 0;
    bool cacheHit = false;
    bool cancelled = false;
};

using VocalSeparationProgress = std::function<void(int, const QString &)>;

// A worker-thread operation. No mixed-audio fallback follows separation failure.
// A complete, validated manifest is required before returning either stem path.
VocalSeparationResult separateVocals(const QString &path, const VocalSeparationOptions &options = {},
                                     const std::shared_ptr<std::atomic_bool> &cancellation = {},
                                     const VocalSeparationProgress &progress = {});

} // namespace singlilt

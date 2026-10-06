// Offline WAV rendering with the current practice settings.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/AccompanimentTimeline.h"
#include "domain/Score.h"

#include <QString>
#include <QtGlobal>

namespace singlilt
{

struct WaveRenderResult
{
    qint64 frames = 0;
    qint64 musicFrames = 0;
    int sampleRate = 48000;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    bool fragment = false;
    double peak = 0.0;
    double rms = 0.0;
    quint64 clippedSamples = 0;
    QString engine;
    QString pianoPath;
    QString gmSoundFontPath;
};

struct WaveRenderOptions
{
    double maxSeconds = 30.0;
    // Practice-time seconds, after speed adjustment. Negative end uses maxSeconds.
    double startSeconds = 0.0;
    double endSeconds = -1.0;
    bool metronome = false;
    int transpose = 0;
    double speed = 1.0;
    PracticeMix mix;
    AccompanimentSettings settings;
    // Null uses JIANPU_GM_SOUNDFONT; empty uses bundled GM (or the OS bank if absent).
    QString gmSoundFontPath;
};

// Offline sampled audio: stereo 48 kHz, signed 16-bit little-endian PCM WAV.
// maxSeconds must be 1..120. Music is capped there, then a 1.5-second release
// tail is appended, so output never exceeds maxSeconds + 1.5 seconds.
// Peak/RMS/clippedSamples describe raw sampler output before PCM saturation.
// Throws std::runtime_error on invalid input, sampler, or atomic-file failure.
WaveRenderResult renderWave(const Score &score, const QString &output, double maxSeconds = 30.0,
                            bool metronome = false);

// The explicit renderer accepts maxSeconds 1..600 (10 minutes).
// Explicit ranges must lie within the performed timeline and be at most maxSeconds.
// Notes spanning the start sound immediately; metronome beats retain their source phase.
WaveRenderResult renderWave(const Score &score, const Timeline &timeline, const AccompanimentPlan &plan,
                            const QString &output, const WaveRenderOptions &options = {});

} // namespace singlilt

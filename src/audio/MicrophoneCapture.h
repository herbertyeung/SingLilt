// Microphone sample capture and recording lifetime.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <memory>
#include <vector>

namespace singlilt
{
struct MicrophoneDevice
{
    QString id;
    QString name;
};
struct MicrophoneBlock
{
    double clockSeconds = 0.0;
    int sampleRate = 48000;
    bool discontinuity = false;
    std::vector<float> samples;
};

class MicrophoneCapture final
{
  public:
    enum class State
    {
        Stopped,
        Starting,
        Running,
        Failed
    };
    MicrophoneCapture();
    ~MicrophoneCapture();
    MicrophoneCapture(const MicrophoneCapture &) = delete;
    MicrophoneCapture &operator=(const MicrophoneCapture &) = delete;
    std::vector<MicrophoneDevice> devices();
    void start(const QString &deviceId);
    void stop();
    State state() const;
    QString errorString() const;
    std::vector<MicrophoneBlock> takeBlocks();
    static double clockSeconds();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace singlilt

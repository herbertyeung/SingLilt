// FluidSynth sound-bank loading and sampled note rendering.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <memory>

namespace singlilt {

// Single-owner sampler; the transport serializes commands. FluidSynth owns its
// realtime audio thread. Offline rendering and realtime output are exclusive.
class SoundFontInstrument final {
public:
    SoundFontInstrument();
    ~SoundFontInstrument();
    SoundFontInstrument(const SoundFontInstrument&) = delete;
    SoundFontInstrument& operator=(const SoundFontInstrument&) = delete;

    bool open(bool realtime = true);
    void close();
    bool isOpen() const;
    bool noteOn(int channel, int pitch, int velocity);
    bool noteOff(int channel, int pitch);
    bool setProgram(int program);
    bool setProgram(int channel, int program);
    // Empty selects bundled GM, or the OS-owned bank when absent. Loading stays lazy.
    bool setGmSoundFontPath(const QString &path);
    QString gmSoundFontPath() const;
    QString effectiveGmSoundFontPath() const;
    bool setChannelVolume(int channel, double volume);
    void setOutputBoost(bool enabled);
    bool silenceChannel(int channel);
    bool allNotesOff();
    // The caller owns a buffer of at least frames * 2 floats. 48 kHz stereo.
    bool render(float *interleaved, int frames);
    QString errorString() const;
    QString deviceName() const;
    QString pianoPath() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace singlilt

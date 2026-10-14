// Shared routing for sampled instruments and Windows MIDI.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "MidiInstrument.h"
#include "SoundFontInstrument.h"

namespace singlilt {

enum class AudioBackend { SampledPiano, WindowsMidi };

// Explicit backend selection: an error never silently substitutes another sound.
class InstrumentOutput final {
public:
    bool setBackend(AudioBackend backend);
    AudioBackend backend() const;
    bool open();
    void close();
    bool isOpen() const;
    bool noteOn(int channel, int pitch, int velocity);
    bool noteOff(int channel, int pitch);
    bool setProgram(int program);
    bool setProgram(int channel, int program);
    bool setGmSoundFontPath(const QString &path);
    QString gmSoundFontPath() const;
    bool setChannelVolume(int channel, double volume);
    void setOutputBoost(bool enabled);
    bool silenceChannel(int channel);
    bool allNotesOff();
    QString errorString() const;
    QString deviceName() const;

private:
    AudioBackend backend_ = AudioBackend::SampledPiano;
    MidiInstrument midi_;
    SoundFontInstrument sampled_;
    QString selectionError_;
};

} // namespace singlilt

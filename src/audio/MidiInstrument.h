// Windows MIDI device ownership and note output.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <memory>

namespace singlilt {

// A single-owner MIDI endpoint. PlaybackEngine serializes all access.
class MidiInstrument final {
public:
    MidiInstrument();
    ~MidiInstrument();
    MidiInstrument(const MidiInstrument&) = delete;
    MidiInstrument& operator=(const MidiInstrument&) = delete;

    bool open();
    void close();
    bool isOpen() const;
    bool noteOn(int channel, int pitch, int velocity);
    bool noteOff(int channel, int pitch);
    bool setProgram(int program); // Zero-based General MIDI, 0 = piano.
    bool setProgram(int channel, int program);
    bool setChannelVolume(int channel, double volume);
    bool silenceChannel(int channel);
    bool allNotesOff();
    QString errorString() const;
    QString deviceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace singlilt

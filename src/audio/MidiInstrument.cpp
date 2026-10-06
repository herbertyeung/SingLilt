// Windows MIDI device ownership and note output.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MidiInstrument.h"
#include "i18n/LanguageManager.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>
#endif

namespace singlilt {

struct MidiInstrument::Impl {
    QString error;
    QString device;
#ifdef _WIN32
    HMIDIOUT handle = nullptr;

    bool check(MMRESULT result, const char* operation)
    {
        if (result == MMSYSERR_NOERROR)
            return true;
        wchar_t message[MAXERRORLENGTH]{};
        midiOutGetErrorTextW(result, message, MAXERRORLENGTH);
        error = trText("messages.audio.operation_failed")
                    .arg(QString::fromLatin1(operation)).arg(result)
                    .arg(QString::fromWCharArray(message));
        return false;
    }
#endif

    bool send(int status, int first, int second = 0)
    {
        if (status < 0x80 || status > 0xef || first < 0 || first > 127 ||
            second < 0 || second > 127) {
            error = trText("messages.audio.invalid_midi_data");
            return false;
        }
#ifdef _WIN32
        if (!handle) {
            error = trText("messages.audio.midi_not_open");
            return false;
        }
        const DWORD message = static_cast<DWORD>(status | (first << 8) | (second << 16));
        return check(midiOutShortMsg(handle, message), "midiOutShortMsg");
#else
        error = trText("messages.audio.windows_required");
        return false;
#endif
    }

    bool validChannel(int channel)
    {
        if (channel >= 0 && channel < 16)
            return true;
        error = trText("messages.audio.channel_range");
        return false;
    }
};

MidiInstrument::MidiInstrument() : impl_(std::make_unique<Impl>()) {}
MidiInstrument::~MidiInstrument() { close(); }

bool MidiInstrument::open()
{
    if (isOpen())
        return true;
    impl_->error.clear();
#ifdef _WIN32
    if (!impl_->check(midiOutOpen(&impl_->handle, MIDI_MAPPER, 0, 0, CALLBACK_NULL),
                      "midiOutOpen (Windows MIDI Mapper)")) {
        impl_->handle = nullptr;
        return false;
    }
    MIDIOUTCAPSW caps{};
    if (midiOutGetDevCapsW(reinterpret_cast<UINT_PTR>(impl_->handle), &caps,
                         sizeof(caps)) == MMSYSERR_NOERROR) {
        impl_->device = QString::fromWCharArray(caps.szPname);
    } else {
        impl_->device = QStringLiteral("Windows MIDI Mapper");
    }
    return true;
#else
    impl_->error = trText("messages.audio.windows_required");
    return false;
#endif
}

void MidiInstrument::close()
{
#ifdef _WIN32
    if (impl_->handle) {
        impl_->check(midiOutReset(impl_->handle), "midiOutReset");
        impl_->check(midiOutClose(impl_->handle), "midiOutClose");
        impl_->handle = nullptr;
    }
#endif
}

bool MidiInstrument::isOpen() const
{
#ifdef _WIN32
    return impl_->handle != nullptr;
#else
    return false;
#endif
}

bool MidiInstrument::noteOn(int channel, int pitch, int velocity)
{
    return impl_->validChannel(channel) && impl_->send(0x90 | channel, pitch, velocity);
}

bool MidiInstrument::noteOff(int channel, int pitch)
{
    return impl_->validChannel(channel) && impl_->send(0x80 | channel, pitch);
}

bool MidiInstrument::setProgram(int program)
{
    return setProgram(0, program);
}

bool MidiInstrument::setProgram(int channel, int program)
{
    if (!impl_->validChannel(channel))
        return false;
    if (program < 0 || program > 127)
    {
        impl_->error = trText("messages.audio.program_range");
        return false;
    }
    // Select the standard GM bank rather than inheriting a previous bank state.
    return impl_->send(0xb0 | channel, 0, 0) && impl_->send(0xb0 | channel, 32, 0) &&
           impl_->send(0xc0 | channel, program);
}

bool MidiInstrument::setChannelVolume(int channel, double volume)
{
    if (!std::isfinite(volume) || !impl_->validChannel(channel)) {
        impl_->error = trText("messages.audio.invalid_channel_volume");
        return false;
    }
    const int value = static_cast<int>(std::lround(std::clamp(volume, 0.0, 1.0) * 127.0));
    return impl_->send(0xb0 | channel, 7, value);
}

bool MidiInstrument::silenceChannel(int channel)
{
    if (!impl_->validChannel(channel))
        return false;
    // Release sustain, notes, and their release tails. Attempt every message.
    bool success = impl_->send(0xb0 | channel, 64, 0);
    success = impl_->send(0xb0 | channel, 123, 0) && success;
    success = impl_->send(0xb0 | channel, 120, 0) && success;
    return success;
}

bool MidiInstrument::allNotesOff()
{
    if (!isOpen())
        return true;
    bool success = silenceChannel(0);
    success = silenceChannel(1) && success;
    success = silenceChannel(2) && success;
    success = silenceChannel(3) && success; // Clicked-note audition owns channel 3.
    success = silenceChannel(9) && success;
    return success;
}

QString MidiInstrument::errorString() const { return impl_->error; }
QString MidiInstrument::deviceName() const { return impl_->device; }

} // namespace singlilt

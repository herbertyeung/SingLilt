// Shared routing for sampled instruments and Windows MIDI.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "InstrumentOutput.h"
#include "i18n/LanguageManager.h"

namespace singlilt {

bool InstrumentOutput::setBackend(AudioBackend backend)
{
    if (backend != AudioBackend::SampledPiano && backend != AudioBackend::WindowsMidi) {
        selectionError_ = trText("messages.audio.unknown_backend");
        return false;
    }
    selectionError_.clear();
    if (backend_ == backend)
        return true;
    close();
    backend_ = backend;
    return true;
}

AudioBackend InstrumentOutput::backend() const { return backend_; }

bool InstrumentOutput::open()
{
    selectionError_.clear();
    return backend_ == AudioBackend::SampledPiano ? sampled_.open() : midi_.open();
}

void InstrumentOutput::close()
{
    if (backend_ == AudioBackend::SampledPiano) sampled_.close(); else midi_.close();
}

bool InstrumentOutput::isOpen() const
{
    return backend_ == AudioBackend::SampledPiano ? sampled_.isOpen() : midi_.isOpen();
}

bool InstrumentOutput::noteOn(int channel, int pitch, int velocity)
{
    return backend_ == AudioBackend::SampledPiano ? sampled_.noteOn(channel, pitch, velocity)
                                                : midi_.noteOn(channel, pitch, velocity);
}

bool InstrumentOutput::noteOff(int channel, int pitch)
{
    return backend_ == AudioBackend::SampledPiano ? sampled_.noteOff(channel, pitch) : midi_.noteOff(channel, pitch);
}

bool InstrumentOutput::setProgram(int program)
{
    return setProgram(0, program);
}

bool InstrumentOutput::setProgram(int channel, int program)
{
    return backend_ == AudioBackend::SampledPiano ? sampled_.setProgram(channel, program)
                                                  : midi_.setProgram(channel, program);
}

bool InstrumentOutput::setGmSoundFontPath(const QString &path)
{
    selectionError_.clear();
    if (sampled_.setGmSoundFontPath(path))
        return true;
    selectionError_ = sampled_.errorString();
    return false;
}

QString InstrumentOutput::gmSoundFontPath() const
{
    return sampled_.gmSoundFontPath();
}

bool InstrumentOutput::setChannelVolume(int channel, double volume)
{
    return backend_ == AudioBackend::SampledPiano ? sampled_.setChannelVolume(channel, volume)
                                                : midi_.setChannelVolume(channel, volume);
}

bool InstrumentOutput::silenceChannel(int channel)
{
    return backend_ == AudioBackend::SampledPiano ? sampled_.silenceChannel(channel) : midi_.silenceChannel(channel);
}

void InstrumentOutput::setOutputBoost(bool enabled)
{
    sampled_.setOutputBoost(enabled);
}

bool InstrumentOutput::allNotesOff()
{
    return backend_ == AudioBackend::SampledPiano ? sampled_.allNotesOff() : midi_.allNotesOff();
}

QString InstrumentOutput::errorString() const
{
    if (!selectionError_.isEmpty())
        return selectionError_;
    return backend_ == AudioBackend::SampledPiano ? sampled_.errorString() : midi_.errorString();
}

QString InstrumentOutput::deviceName() const
{
    if (backend_ == AudioBackend::SampledPiano)
        return sampled_.deviceName().isEmpty() ? trText("messages.audio.sampled_closed") : sampled_.deviceName();
    return trText("messages.audio.windows_device").arg(midi_.deviceName().isEmpty()
        ? trText("messages.audio.mapper_closed") : midi_.deviceName());
}

} // namespace singlilt

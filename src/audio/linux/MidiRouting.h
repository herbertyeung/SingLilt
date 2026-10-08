// Endpoint policy for automatic ALSA MIDI routing.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <alsa/asoundlib.h>

namespace singlilt
{
inline int midiDestinationPriority(unsigned int capabilities, unsigned int type, const QString &client,
                                   const QString &port)
{
    constexpr unsigned int Required = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
    if ((capabilities & Required) != Required || client.contains("Midi Through", Qt::CaseInsensitive) ||
        port.contains("Midi Through", Qt::CaseInsensitive))
        return 0;
    if (type & SND_SEQ_PORT_TYPE_SYNTHESIZER)
        return 2;
    return (type & SND_SEQ_PORT_TYPE_MIDI_GENERIC) ? 1 : 0;
}
} // namespace singlilt

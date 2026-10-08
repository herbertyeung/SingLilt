// ALSA sequencer endpoint ownership and General MIDI note output.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "audio/MidiInstrument.h"
#include "MidiRouting.h"
#include "i18n/LanguageManager.h"
#include <algorithm>
#include <alsa/asoundlib.h>
#include <cmath>
#include <poll.h>
#include <vector>

namespace singlilt
{
struct MidiInstrument::Impl
{
    std::unique_ptr<snd_seq_t, decltype(&snd_seq_close)> handle{nullptr, snd_seq_close};
    int port = -1;
    QString error;
    QString device;

    bool check(int status)
    {
        if (status >= 0)
            return true;
        error = QString("ALSA MIDI: %1").arg(QString::fromUtf8(snd_strerror(status)));
        return false;
    }

    bool validChannel(int channel)
    {
        if (channel >= 0 && channel < 16)
            return true;
        error = trText("messages.audio.channel_range");
        return false;
    }

    bool send(int status, int first, int second = 0)
    {
        if (status < 0x80 || status > 0xef || first < 0 || first > 127 || second < 0 || second > 127)
        {
            error = trText("messages.audio.invalid_midi_data");
            return false;
        }
        if (!handle)
        {
            error = trText("messages.audio.midi_not_open");
            return false;
        }
        snd_seq_event_t event;
        snd_seq_ev_clear(&event);
        snd_seq_ev_set_source(&event, port);
        snd_seq_ev_set_subs(&event);
        snd_seq_ev_set_direct(&event);
        const int channel = status & 0x0f;
        switch (status & 0xf0)
        {
        case 0x80:
            snd_seq_ev_set_noteoff(&event, channel, first, second);
            break;
        case 0x90:
            snd_seq_ev_set_noteon(&event, channel, first, second);
            break;
        case 0xb0:
            snd_seq_ev_set_controller(&event, channel, first, second);
            break;
        case 0xc0:
            snd_seq_ev_set_pgmchange(&event, channel, first);
            break;
        default:
            error = trText("messages.audio.invalid_midi_data");
            return false;
        }
        int statusCode = snd_seq_event_output_direct(handle.get(), &event);
        if (statusCode == -EAGAIN)
        {
            const int count = snd_seq_poll_descriptors_count(handle.get(), POLLOUT);
            if (count > 0)
            {
                std::vector<pollfd> descriptors(static_cast<std::size_t>(count));
                if (snd_seq_poll_descriptors(handle.get(), descriptors.data(), count, POLLOUT) == count &&
                    poll(descriptors.data(), descriptors.size(), 20) > 0)
                    statusCode = snd_seq_event_output_direct(handle.get(), &event);
            }
        }
        return check(statusCode);
    }
};

MidiInstrument::MidiInstrument() : impl_(std::make_unique<Impl>()) {}
MidiInstrument::~MidiInstrument()
{
    close();
}

bool MidiInstrument::open()
{
    if (isOpen())
        return true;
    impl_->error.clear();
    snd_seq_t *rawHandle = nullptr;
    if (!impl_->check(snd_seq_open(&rawHandle, "default", SND_SEQ_OPEN_OUTPUT, SND_SEQ_NONBLOCK)))
        return false;
    impl_->handle.reset(rawHandle);
    if (!impl_->check(snd_seq_set_client_name(rawHandle, "SingLilt")))
    {
        close();
        return false;
    }
    impl_->port = snd_seq_create_simple_port(
        rawHandle, "Playback", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_APPLICATION);
    if (!impl_->check(impl_->port))
    {
        close();
        return false;
    }
    snd_seq_client_info_t *client;
    snd_seq_port_info_t *port;
    snd_seq_client_info_alloca(&client);
    snd_seq_port_info_alloca(&port);
    snd_seq_client_info_set_client(client, -1);
    struct Destination
    {
        int client;
        int port;
        int priority;
        QString name;
    };
    std::vector<Destination> destinations;
    while (snd_seq_query_next_client(rawHandle, client) >= 0)
    {
        const int destination = snd_seq_client_info_get_client(client);
        if (destination == snd_seq_client_id(rawHandle))
            continue;
        snd_seq_port_info_set_client(port, destination);
        snd_seq_port_info_set_port(port, -1);
        while (snd_seq_query_next_port(rawHandle, port) >= 0)
        {
            const unsigned int capabilities = snd_seq_port_info_get_capability(port);
            const unsigned int type = snd_seq_port_info_get_type(port);
            const QString clientName = QString::fromUtf8(snd_seq_client_info_get_name(client));
            const QString portName = QString::fromUtf8(snd_seq_port_info_get_name(port));
            const int priority = midiDestinationPriority(capabilities, type, clientName, portName);
            if (priority == 0)
                continue;
            destinations.push_back(
                {destination, snd_seq_port_info_get_port(port), priority, clientName + " / " + portName});
        }
    }
    std::stable_sort(destinations.begin(), destinations.end(),
                     [](const Destination &first, const Destination &second)
                     { return first.priority > second.priority; });
    for (const auto &destination : destinations)
    {
        if (snd_seq_connect_to(rawHandle, impl_->port, destination.client, destination.port) >= 0)
        {
            impl_->device = destination.name;
            return true;
        }
    }
    impl_->error = trText("messages.audio.linux_midi_destination");
    close();
    return false;
}

void MidiInstrument::close()
{
    if (impl_->handle && !impl_->device.isEmpty())
        allNotesOff();
    impl_->handle.reset();
    impl_->port = -1;
    impl_->device.clear();
}
bool MidiInstrument::isOpen() const
{
    return impl_->handle && !impl_->device.isEmpty();
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
    return impl_->send(0xb0 | channel, 0, 0) && impl_->send(0xb0 | channel, 32, 0) &&
           impl_->send(0xc0 | channel, program);
}
bool MidiInstrument::setChannelVolume(int channel, double volume)
{
    if (!impl_->validChannel(channel))
        return false;
    if (!std::isfinite(volume))
    {
        impl_->error = trText("messages.audio.invalid_channel_volume");
        return false;
    }
    const int level = static_cast<int>(std::lround(std::clamp(volume, 0.0, 1.0) * 127.0));
    return impl_->send(0xb0 | channel, 7, level);
}
bool MidiInstrument::silenceChannel(int channel)
{
    if (!impl_->validChannel(channel))
        return false;
    bool success = impl_->send(0xb0 | channel, 64, 0);
    success = impl_->send(0xb0 | channel, 123, 0) && success;
    success = impl_->send(0xb0 | channel, 120, 0) && success;
    return success;
}
bool MidiInstrument::allNotesOff()
{
    if (!isOpen())
        return true;
    bool success = true;
    for (const int channel : {0, 1, 2, 3, 9})
        success = silenceChannel(channel) && success;
    return success;
}
QString MidiInstrument::errorString() const
{
    return impl_->error;
}
QString MidiInstrument::deviceName() const
{
    return impl_->device;
}
} // namespace singlilt

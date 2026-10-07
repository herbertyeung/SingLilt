// FluidSynth sound-bank loading and sampled note rendering.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "SoundFontInstrument.h"
#include "i18n/LanguageManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <fluidsynth.h>

#include <algorithm>
#include <cmath>
#include <limits>
#ifdef Q_OS_LINUX
#include "platform/LinuxRuntime.h"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace singlilt {
namespace {
QString defaultPianoPath()
{
    const QString configured = qEnvironmentVariable("JIANPU_SOUNDFONT");
#ifdef Q_OS_LINUX
    const QString bundled = QCoreApplication::applicationDirPath() + "/assets/soundfonts/Salamander.sf2";
    if (configured.isEmpty() && !QFileInfo(bundled).isFile())
        return linuxGmSoundFontPath();
#endif
    return configured.isEmpty() ? QCoreApplication::applicationDirPath() + "/assets/soundfonts/Salamander.sf2"
                                : QFileInfo(configured).absoluteFilePath();
}

QString systemGmPath()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH]{};
    const auto count = GetSystemDirectoryW(path, MAX_PATH);
    if (count > 0 && count < MAX_PATH)
        return QDir::fromNativeSeparators(QString::fromWCharArray(path)) + "/drivers/gm.dls";
#elif defined(Q_OS_LINUX)
    return linuxGmSoundFontPath();
#endif
    return {};
}

QString defaultGmPath()
{
    const QString bundled = QCoreApplication::applicationDirPath() + "/assets/soundfonts/GeneralUser-GS.sf2";
    return QFileInfo(bundled).isFile() ? bundled : systemGmPath();
}

void collectDriver(void *data, const char *, const char *option)
{
    static_cast<QStringList*>(data)->append(QString::fromUtf8(option));
}
} // namespace

struct SoundFontInstrument::Impl {
    std::unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)> settings{nullptr, delete_fluid_settings};
    std::unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)> synth{nullptr, delete_fluid_synth};
    std::unique_ptr<fluid_audio_driver_t, decltype(&delete_fluid_audio_driver)> driver{nullptr, delete_fluid_audio_driver};
    int pianoFont = -1;
    int gmFont = -1;
    bool realtime = true;
    bool ready = false;
    QString path;
    QString gmPath = qEnvironmentVariable("JIANPU_GM_SOUNDFONT");
    QString activeGmPath;
    QString label;
    QString error;

    bool fail(const QString& message)
    {
        error = message;
        return false;
    }

    bool check(int result, const char* operation)
    {
        return result == FLUID_OK || fail(trText("messages.audio.fluid_operation_failed")
            .arg(trText(operation)).arg(result));
    }

    bool channelReady(int channel)
    {
        if (!ready)
            return fail(trText("messages.audio.sampled_not_open"));
        if (channel < 0 || channel > 15)
            return fail(trText("messages.audio.channel_range"));
        return true;
    }

    void release()
    {
        // Stop the callback before freeing its synth and backing sample memory.
        driver.reset();
        synth.reset();
        settings.reset();
        pianoFont = gmFont = -1;
        activeGmPath.clear();
        ready = false;
    }
};

SoundFontInstrument::SoundFontInstrument() : impl_(std::make_unique<Impl>()) {}
SoundFontInstrument::~SoundFontInstrument() { close(); }

bool SoundFontInstrument::open(bool realtime)
{
    if (impl_->ready) {
        if (impl_->realtime == realtime)
            return true;
        return impl_->fail(trText("messages.audio.close_before_mode_change"));
    }
    impl_->release();
    impl_->error.clear();
    impl_->label.clear();
    impl_->path = defaultPianoPath();
    impl_->realtime = realtime;
    const QString gmPath = impl_->gmPath.isEmpty() ? defaultGmPath() : impl_->gmPath;
    if (!QFileInfo(impl_->path).isFile())
        return impl_->fail(trText("messages.audio.missing_piano").arg(impl_->path));
    if (gmPath.isEmpty() || !QFileInfo(gmPath).isFile())
        return impl_->fail(trText(impl_->gmPath.isEmpty() ? "messages.audio.missing_system_gm"
                                                          : "messages.audio.missing_gm_soundfont")
                               .arg(gmPath));

    impl_->settings.reset(new_fluid_settings());
    if (!impl_->settings)
        return impl_->fail(trText("messages.audio.settings_allocation_failed"));
    auto* settings = impl_->settings.get();
    const bool configured =
        fluid_settings_setnum(settings, "synth.sample-rate", 48000.0) == FLUID_OK &&
        // Output gain, not key velocity: raise loudness without changing sample layers.
        // 0.5 retains headroom in the full-volume/velocity-127 dense-note regression.
        fluid_settings_setnum(settings, "synth.gain", 0.5) == FLUID_OK &&
        fluid_settings_setint(settings, "synth.polyphony", 128) == FLUID_OK &&
        fluid_settings_setint(settings, "synth.threadsafe-api", 1) == FLUID_OK &&
        fluid_settings_setint(settings, "synth.reverb.active", 1) == FLUID_OK &&
        fluid_settings_setint(settings, "synth.chorus.active", 0) == FLUID_OK &&
        fluid_settings_setnum(settings, "synth.reverb.room-size", 0.3) == FLUID_OK &&
        fluid_settings_setnum(settings, "synth.reverb.damp", 0.6) == FLUID_OK &&
        fluid_settings_setnum(settings, "synth.reverb.level", 0.12) == FLUID_OK &&
        fluid_settings_setint(settings, "audio.period-size", 256) == FLUID_OK &&
        fluid_settings_setint(settings, "audio.periods", 3) == FLUID_OK;
    if (!configured) {
        impl_->release();
        return impl_->fail(trText("messages.audio.settings_rejected"));
    }
    QString audioDriver = QStringLiteral("offline");
    if (realtime) {
        QStringList available;
        fluid_settings_foreach_option(settings, "audio.driver", &available, collectDriver);
#ifdef Q_OS_LINUX
        const QStringList candidates{QStringLiteral("pulseaudio"), QStringLiteral("alsa")};
#else
        const QStringList candidates{QStringLiteral("wasapi"), QStringLiteral("dsound"), QStringLiteral("waveout"),
                                     QStringLiteral("sdl3")};
#endif
        for (const auto &candidate : candidates)
        {
            if (available.contains(candidate)) {
                audioDriver = candidate;
                break;
            }
        }
        if (audioDriver == QStringLiteral("offline") ||
            fluid_settings_setstr(settings, "audio.driver", audioDriver.toUtf8().constData()) != FLUID_OK) {
            impl_->release();
            return impl_->fail(trText("messages.audio.no_driver")
                .arg(available.join(", ")));
        }
    }
    impl_->synth.reset(new_fluid_synth(settings));
    if (!impl_->synth) {
        impl_->release();
        return impl_->fail(trText("messages.audio.synth_allocation_failed"));
    }
    auto* synth = impl_->synth.get();
    // System gm.dls stays at its OS-owned path. Never copy it into application assets.
    impl_->gmFont = fluid_synth_sfload(synth, gmPath.toUtf8().constData(), 0);
    if (impl_->gmFont < 0) {
        impl_->release();
        return impl_->fail(trText(gmPath == systemGmPath() ? "messages.audio.system_gm_load_failed"
                                                           : "messages.audio.gm_soundfont_load_failed")
                               .arg(gmPath));
    }
    impl_->pianoFont = fluid_synth_sfload(synth, impl_->path.toUtf8().constData(), 0);
    if (impl_->pianoFont < 0)
    {
        impl_->release();
        return impl_->fail(trText("messages.audio.piano_load_failed").arg(impl_->path));
    }
    // Select by explicit font ID. The piano-only SoundFont must not capture
    // electric-piano or percussion requests through a fallback bank lookup.
    const bool selected =
        impl_->check(fluid_synth_set_channel_type(synth, 9, CHANNEL_TYPE_DRUM), "messages.audio.op.set_drum") &&
        impl_->check(fluid_synth_program_select(synth, 9, impl_->gmFont, 128, 0), "messages.audio.op.select_drums") &&
        impl_->check(fluid_synth_program_select(synth, 0, impl_->gmFont, 0, 4), "messages.audio.op.verify_electric") &&
        impl_->check(fluid_synth_program_select(synth, 0, impl_->pianoFont, 0, 0), "messages.audio.op.select_piano");
    if (!selected) {
        impl_->release();
        return false;
    }
    if (realtime) {
        impl_->driver.reset(new_fluid_audio_driver(settings, synth));
        if (!impl_->driver) {
            impl_->release();
            return impl_->fail(trText("messages.audio.output_open_failed").arg(audioDriver));
        }
    }
    impl_->label = audioDriver;
    impl_->activeGmPath = gmPath;
    impl_->ready = true;
    return true;
}

void SoundFontInstrument::close()
{
    if (impl_->ready)
        allNotesOff();
    impl_->release();
}

bool SoundFontInstrument::isOpen() const { return impl_->ready; }

bool SoundFontInstrument::noteOn(int channel, int pitch, int velocity)
{
    if (!impl_->channelReady(channel))
        return false;
    if (pitch < 0 || pitch > 127 || velocity < 0 || velocity > 127)
        return impl_->fail(trText("messages.audio.invalid_sample_note"));
    if (velocity == 0)
        return noteOff(channel, pitch);
    return impl_->check(fluid_synth_noteon(impl_->synth.get(), channel, pitch, velocity), "messages.audio.op.note_on");
}

bool SoundFontInstrument::noteOff(int channel, int pitch)
{
    if (!impl_->channelReady(channel))
        return false;
    if (pitch < 0 || pitch > 127)
        return impl_->fail(trText("messages.audio.invalid_sample_pitch"));
    const int result = fluid_synth_noteoff(impl_->synth.get(), channel, pitch);
    // FluidSynth also returns FLUID_FAILED when a voice has naturally decayed
    // or was already released. With a live synth and validated, never-disabled
    // channels, that is an idempotent note-off, not an audio-device failure.
    return result == FLUID_FAILED || impl_->check(result, "messages.audio.op.note_off");
}

bool SoundFontInstrument::setProgram(int program)
{
    return setProgram(0, program);
}

bool SoundFontInstrument::setProgram(int channel, int program)
{
    if (!impl_->channelReady(channel))
        return false;
    if (program < 0 || program > 127)
        return impl_->fail(trText("messages.audio.program_range"));
    const int font = channel != 9 && program == 0 ? impl_->pianoFont : impl_->gmFont;
    return impl_->check(
        fluid_synth_program_select(impl_->synth.get(), channel, font, channel == 9 ? 128 : 0, program),
        "messages.audio.op.select_melody");
}

bool SoundFontInstrument::setGmSoundFontPath(const QString &path)
{
    const QString selected = path.trimmed().isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
    if (!selected.isEmpty() &&
        (!QFileInfo(selected).isFile() || QFileInfo(selected).suffix().compare("sf2", Qt::CaseInsensitive) != 0))
        return impl_->fail(trText("messages.audio.missing_gm_soundfont").arg(selected));
    if (impl_->gmPath == selected)
        return true;
    close();
    impl_->gmPath = selected;
    impl_->error.clear();
    return true;
}

QString SoundFontInstrument::gmSoundFontPath() const
{
    return impl_->gmPath;
}

QString SoundFontInstrument::effectiveGmSoundFontPath() const
{
    if (!impl_->activeGmPath.isEmpty())
        return impl_->activeGmPath;
    return impl_->gmPath.isEmpty() ? defaultGmPath() : impl_->gmPath;
}

bool SoundFontInstrument::setChannelVolume(int channel, double volume)
{
    if (!impl_->channelReady(channel))
        return false;
    if (!std::isfinite(volume))
        return impl_->fail(trText("messages.audio.invalid_volume"));
    const int value = static_cast<int>(std::lround(std::clamp(volume, 0.0, 1.0) * 127.0));
    return impl_->check(fluid_synth_cc(impl_->synth.get(), channel, 7, value), "messages.audio.op.volume");
}

bool SoundFontInstrument::silenceChannel(int channel)
{
    if (!impl_->channelReady(channel))
        return false;
    bool success = impl_->check(fluid_synth_cc(impl_->synth.get(), channel, 64, 0), "messages.audio.op.sustain");
    success = impl_->check(fluid_synth_all_notes_off(impl_->synth.get(), channel), "messages.audio.op.notes_off") && success;
    success = impl_->check(fluid_synth_all_sounds_off(impl_->synth.get(), channel), "messages.audio.op.sounds_off") && success;
    return success;
}

bool SoundFontInstrument::allNotesOff()
{
    if (!impl_->ready)
        return true;
    bool success = silenceChannel(0);
    success = silenceChannel(1) && success;
    success = silenceChannel(2) && success;
    success = silenceChannel(3) && success; // Clicked-note audition owns channel 3.
    success = silenceChannel(9) && success;
    return success;
}

bool SoundFontInstrument::render(float* interleaved, int frames)
{
    if (!impl_->ready || impl_->realtime)
        return impl_->fail(trText("messages.audio.offline_requires_mode"));
    if (!interleaved || frames <= 0 || frames > std::numeric_limits<int>::max() / 2)
        return impl_->fail(trText("messages.audio.invalid_render_buffer"));
    return impl_->check(fluid_synth_write_float(impl_->synth.get(), frames,
        interleaved, 0, 2, interleaved, 1, 2), "messages.audio.op.render");
}

QString SoundFontInstrument::errorString() const { return impl_->error; }
QString SoundFontInstrument::deviceName() const
{
    if (impl_->label.isEmpty())
        return {};
    const QString bank = effectiveGmSoundFontPath();
    if (!impl_->gmPath.isEmpty() || bank != systemGmPath())
        return trText("messages.audio.sampler_device_custom")
            .arg(QString::fromLatin1(fluid_version_str()), QFileInfo(impl_->path).fileName(),
                 impl_->label == QStringLiteral("offline") ? trText("messages.audio.offline") : impl_->label,
                 QFileInfo(bank).fileName());
    return trText("messages.audio.sampler_device")
        .arg(QString::fromLatin1(fluid_version_str()), QFileInfo(impl_->path).fileName(),
             impl_->label == QStringLiteral("offline") ? trText("messages.audio.offline") : impl_->label);
}
QString SoundFontInstrument::pianoPath() const
{
    return impl_->path.isEmpty() ? defaultPianoPath() : impl_->path;
}

} // namespace singlilt

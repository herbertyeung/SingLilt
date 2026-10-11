// Timeline transport and melody/accompaniment event scheduling.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "InstrumentOutput.h"
#include "domain/AccompanimentTimeline.h"
#include "domain/Score.h"
#include "domain/Timeline.h"

#include <QString>
#include <cstdint>
#include <memory>
#include <vector>

namespace singlilt {

// Submitted note state, not an acoustic measurement of the audio device.
struct PlaybackVoiceState
{
    int melodyPitch = -1;
    std::vector<int> chordPitches;
    std::vector<int> bassPitches;
    std::uint64_t melodyNoteOns = 0;
    std::uint64_t chordNoteOns = 0;
    std::uint64_t bassNoteOns = 0;
    int previewPitch = -1;
    int previewProgram = 0;
    int previewVelocity = 0;
    std::uint64_t previewNoteOns = 0;
};

// Thread-safe transport. The UI polls positionTicks; it never drives audio.
class PlaybackEngine final {
public:
    PlaybackEngine();
    ~PlaybackEngine();
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    bool load(const Score &score);
    bool load(const Score &score, const Timeline &timeline);
    bool load(const Score &score, const Timeline &timeline, const AccompanimentPlan &plan,
              const AccompanimentSettings &settings = {});
    // Replaces only channels 1/2 without changing the transport or melody voice.
    bool setAccompanimentPlan(const AccompanimentPlan &plan, const AccompanimentSettings &settings = {});
    // Changes instruments without replacing the plan or restarting the unchanged hand.
    bool setAccompanimentSettings(const AccompanimentSettings &settings);
    bool setPracticeMix(const PracticeMix &mix);
    PracticeMix practiceMix() const;
    PlaybackVoiceState voiceState() const;
    bool play();
    // Absolute MIDI pitch, independent of the transport's transpose and mute.
    // A rest (-1) cancels only audition. Accepted requests are processed by the worker.
    bool previewNote(int midiPitch, int program, int velocity = 88, int durationMilliseconds = 650);
    bool isPreviewLoading() const;
    // Pauses at the current musical position. The next play opens the selected
    // backend lazily; there is no automatic fallback on a missing sample/device.
    bool setAudioBackend(AudioBackend backend);
    AudioBackend audioBackend() const;
    bool setGmSoundFontPath(const QString &path);
    QString gmSoundFontPath() const;
    void pause();
    // Keep the score and position, but relinquish a device before another window uses it.
    void releaseAudioDevice();
    void stop();
    void seek(std::int64_t tick);
    void setSpeed(double factor); // 0.25..2.0, independent of score BPM.
    bool setTranspose(int semitones); // Rejects values outside -24..24 or MIDI range.
    void setMetronome(bool enabled);
    void setVolume(double volume); // Melody only, 0..1.
    void setOutputBoost(bool enabled); // Sampled output only; MIDI velocity and mix stay unchanged.
    void setMetronomeVolume(double volume); // 0..1.
    // Explicit global override until the next load. Normal playback uses each
    // timeline event's program, including after seek and pause/resume.
    void setProgram(int program); // Zero-based General MIDI, 0..127.

    std::int64_t positionTicks() const;
    std::int64_t durationTicks() const;
    bool isPlaying() const;
    double speed() const;
    int transpose() const;
    // Resolve musical state at the current position, including while paused.
    // currentProgram respects a manual override and sustained-tie programs.
    int currentProgram() const;
    int currentVerseIndex() const;
    QString errorString() const;
    QString deviceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace singlilt

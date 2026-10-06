// Full-voice staff playback and instrument-routing checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffPlaybackCheck.h"

#include "audio/PlaybackEngine.h"
#include "audio/WaveRenderer.h"
#include "domain/StaffPerformance.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace singlilt
{
namespace
{

constexpr int SampleRate = 48000;

bool pitchesEqual(const std::vector<int> &pitches, std::initializer_list<int> expected)
{
    return pitches == std::vector<int>(expected);
}

QByteArray waveBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Staff playback WAV could not be read");
    const auto bytes = file.readAll();
    if (bytes.size() < 44 || bytes.left(4) != "RIFF" || bytes.mid(8, 8) != "WAVEfmt " ||
        bytes.mid(36, 4) != "data")
        throw std::runtime_error("Staff playback WAV header is invalid");
    const auto sampleBytes = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + 40));
    if (sampleBytes % 4 != 0 || bytes.size() != 44 + sampleBytes)
        throw std::runtime_error("Staff playback WAV size is invalid");
    return bytes;
}

double pcmRms(const QByteArray &wave, double first, double end)
{
    const qsizetype start = 44 + static_cast<qsizetype>(first * SampleRate) * 4;
    const qsizetype limit = std::min(wave.size(), 44 + static_cast<qsizetype>(end * SampleRate) * 4);
    if (limit <= start)
        throw std::runtime_error("Staff playback PCM interval is empty");
    long double sum = 0;
    for (qsizetype i = start; i < limit; i += 2)
    {
        const auto sample = qFromLittleEndian<qint16>(reinterpret_cast<const uchar *>(wave.constData() + i));
        sum += static_cast<long double>(sample) * sample;
    }
    return std::sqrt(static_cast<double>(sum / ((limit - start) / 2))) / 32768.0;
}

class StaffPlaybackProbe final : public QObject
{
  public:
    StaffPlaybackProbe(const QCommandLineParser &args, QApplication &app)
        : QObject(&app), args_(args), app_(app), folder_(QFileInfo(args.value("report")).absolutePath())
    {
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        timer_.setInterval(60);
        elapsed_.start();
        timer_.start();
    }

  private:
    void check(const QString &name, bool valid)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", valid}});
        passed_ &= valid;
    }

    void require(bool valid)
    {
        if (!valid)
            throw std::runtime_error(player_.errorString().toStdString());
    }

    void makeFixture()
    {
        score_.bpm = 120;
        Note guide;
        guide.id = 1;
        guide.degree = 6;
        guide.durationTicks = 16 * TicksPerQuarter;
        score_.notes.push_back(guide);
        timeline_ = buildTimeline(score_);
        StaffPerformance performance;
        performance.staffCount = 2;
        performance.primaryStaff = 1;
        performance.durationTicks = timeline_.durationTicks;
        performance.timingFingerprint = staffTimingFingerprint(score_);
        const std::vector<AccompanimentEvent> expected{
            {0, 4800, 72, 88, AccompanimentRole::Chord},   {0, 2400, 76, 82, AccompanimentRole::Chord},
            {480, 1920, 79, 76, AccompanimentRole::Chord}, {720, 720, 72, 70, AccompanimentRole::Chord},
            {0, 5760, 48, 80, AccompanimentRole::Bass},    {0, 2880, 55, 76, AccompanimentRole::Bass},
            {480, 1440, 60, 72, AccompanimentRole::Bass}};
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            const auto &event = expected[i];
            StaffPerformanceNote note;
            note.startTick = event.startTick;
            note.durationTicks = event.durationTicks;
            note.midiPitch = event.midiPitch;
            note.velocity = event.velocity;
            note.staff = event.role == AccompanimentRole::Chord ? 1 : 2;
            note.voice = std::to_string(i + 1);
            if (i == 1)
            {
                note.durationTicks = 1200;
                note.tieStart = true;
                performance.notes.push_back(note);
                note.startTick = 1200;
                note.tieStart = false;
                note.tieStop = true;
            }
            performance.notes.push_back(note);
        }
        plan_ = buildStaffPerformancePlan(score_, timeline_, performance);
        check("Full staff plan includes both hands and joins the primary tied note",
              plan_.valid() && plan_.events.size() == expected.size() &&
                  std::any_of(plan_.events.begin(), plan_.events.end(),
                              [](const AccompanimentEvent &event)
                              {
                                  return event.role == AccompanimentRole::Chord && event.midiPitch == 76 &&
                                         event.startTick == 0 && event.durationTicks == 2400;
                              }));
        settings_.originalStaff = true;
        mix_ = {true, true, 0.65, 0.55};
        unisonScore_.bpm = 120;
        Note unisonGuide;
        unisonGuide.octave = 1;
        unisonGuide.durationTicks = 1440;
        unisonScore_.notes.push_back(unisonGuide);
        StaffPerformance unison;
        unison.durationTicks = 1440;
        unison.timingFingerprint = staffTimingFingerprint(unisonScore_);
        for (int segment = 0; segment < 3; ++segment)
            for (int copy = 0; copy < 2; ++copy)
            {
                StaffPerformanceNote note;
                note.midiPitch = 72;
                note.voice = "2";
                note.startTick = segment * 480;
                note.durationTicks = 480;
                note.velocity = copy == 0 ? 80 : 70;
                note.tieStop = segment > 0;
                note.tieStart = segment < 2;
                note.pageIndex = segment;
                unison.notes.push_back(note);
            }
        const auto unisonTimeline = buildTimeline(unisonScore_);
        unisonPlan_ = buildStaffPerformancePlan(unisonScore_, unisonTimeline, unison);
        check("Two simultaneous same-voice unison tie chains retain both complete sounding events",
              unisonPlan_.valid() && unisonPlan_.events.size() == 2 &&
                  std::all_of(
                      unisonPlan_.events.begin(), unisonPlan_.events.end(), [](const AccompanimentEvent &event)
                      { return event.startTick == 0 && event.durationTicks == 1440 && event.midiPitch == 72; }));
        auto orphan = unison;
        orphan.notes.pop_back();
        check("A missing unison tie stop remains an error rather than losing an open source tone",
              !buildStaffPerformancePlan(unisonScore_, unisonTimeline, orphan).valid());
        auto extraStop = unison;
        extraStop.notes.push_back(unison.notes.back());
        check("A third unmatched same-tick unison stop is rejected",
              !buildStaffPerformancePlan(unisonScore_, unisonTimeline, extraStop).valid());
        Score gapGuide;
        gapGuide.bpm = 120;
        Note gapTone;
        gapTone.octave = 1;
        gapGuide.notes.push_back(gapTone);
        Note gapRest;
        gapRest.degree = 0;
        gapGuide.notes.push_back(gapRest);
        gapGuide.notes.push_back(gapTone);
        StaffPerformance gap;
        gap.durationTicks = 1440;
        gap.timingFingerprint = staffTimingFingerprint(gapGuide);
        StaffPerformanceNote brokenStart;
        brokenStart.midiPitch = 72;
        brokenStart.tieStart = true;
        brokenStart.unresolvedSoundTie = true;
        gap.notes.push_back(brokenStart);
        auto brokenStop = brokenStart;
        brokenStop.startTick = 960;
        brokenStop.tieStart = false;
        brokenStop.tieStop = true;
        gap.notes.push_back(brokenStop);
        const auto gapPlan = buildStaffPerformancePlan(gapGuide, buildTimeline(gapGuide), gap);
        check("Explicit unresolved OMR ties retain raw flags and perform only the original separated intervals",
              gapPlan.valid() && gapPlan.events.size() == 2 && gapPlan.events[0].startTick == 0 &&
                  gapPlan.events[0].durationTicks == 480 && gapPlan.events[1].startTick == 960 &&
                  gapPlan.events[1].durationTicks == 480 && gap.notes[0].tieStart && gap.notes[1].tieStop &&
                  gap.notes[0].durationTicks == 480 && gap.notes[1].startTick == 960 &&
                  std::any_of(gapPlan.diagnostics.begin(), gapPlan.diagnostics.end(), [](const Diagnostic &entry)
                              { return entry.severity == DiagnosticSeverity::Warning; }));
        for (auto &note : gap.notes)
            note.unresolvedSoundTie = false;
        check("The identical tie gap without explicit OMR review flags remains invalid",
              !buildStaffPerformancePlan(gapGuide, buildTimeline(gapGuide), gap).valid());
    }

    void checkWaves()
    {
        WaveRenderOptions options;
        options.maxSeconds = 600;
        options.gmSoundFontPath = QApplication::applicationDirPath() + "/assets/soundfonts/GeneralUser-GS.sf2";
        if (!QFileInfo(options.gmSoundFontPath).isFile())
            throw std::runtime_error("The explicit GM SoundFont for staff-instrument PCM validation is missing");
        options.startSeconds = 0.5;
        options.endSeconds = 1.5;
        options.settings = settings_;
        options.mix = mix_;
        const auto render = [&](const QString &name, const Score &score, const Timeline &timeline)
        {
            const QString path = folder_ + "/" + name + ".wav";
            const auto result = renderWave(score, timeline, plan_, path, options);
            const auto wave = waveBytes(path);
            artifacts_.append(path);
            check(name + " exact stereo PCM frame count and release tail",
                  result.musicFrames == SampleRate && result.frames == SampleRate * 5 / 2 &&
                      wave.size() == 44 + result.frames * 4);
            check(name + " records the actual selected SF2 bank",
                  QFileInfo(result.gmSoundFontPath).canonicalFilePath() ==
                      QFileInfo(options.gmSoundFontPath).canonicalFilePath());
            return wave;
        };
        const auto both = render("staff-both-hands", score_, timeline_);
        WaveRenderOptions unisonOptions = options;
        const auto unisonTimeline = buildTimeline(unisonScore_);
        const QString unisonPath = folder_ + "/staff-multiple-unison-ties.wav";
        renderWave(unisonScore_, unisonTimeline, unisonPlan_, unisonPath, unisonOptions);
        auto singleUnison = unisonPlan_;
        singleUnison.events.resize(1);
        const QString singlePath = folder_ + "/staff-single-unison-reference.wav";
        renderWave(unisonScore_, unisonTimeline, singleUnison, singlePath, unisonOptions);
        artifacts_.append(unisonPath);
        artifacts_.append(singlePath);
        check("Multiple unison lifetimes share one MIDI attack without dropping either source tie chain",
              waveBytes(unisonPath) == waveBytes(singlePath) && pcmRms(waveBytes(unisonPath), 0.01, 0.1) > 1e-5);
        check("Both original staves crossing the excerpt start are audible immediately",
              pcmRms(both, 0.01, 0.10) > 1e-5);
        Score otherGuide = score_;
        otherGuide.notes[0].degree = 2;
        const auto alternate = render("staff-guide-not-doubled", otherGuide, buildTimeline(otherGuide));
        check("Guide pitch does not enter original-staff PCM", both == alternate);
        options.settings.chordProgram = 40;
        options.settings.bassProgram = 35;
        const auto selectedInstruments = render("staff-selected-instruments", score_, timeline_);
        check("Selected original-staff GM instruments change real PCM while retaining all notes",
              selectedInstruments != both && pcmRms(selectedInstruments, 0.01, 0.10) > 1e-5);
        options.settings = settings_;
        options.mix.accompanimentEnabled = false;
        const auto right = render("staff-primary-only", score_, timeline_);
        options.mix.melodyEnabled = false;
        options.mix.accompanimentEnabled = true;
        const auto left = render("staff-other-only", score_, timeline_);
        check("Each hand has audible, distinct PCM", pcmRms(right, 0.01, 0.10) > 1e-5 &&
                                                         pcmRms(left, 0.01, 0.10) > 1e-5 && right != left &&
                                                         right != both && left != both);
        options.mix.accompanimentEnabled = false;
        const auto silent = render("staff-both-muted", score_, timeline_);
        check("Both hand mutes produce exact silent PCM", pcmRms(silent, 0.0, 2.0) == 0.0);
        options.mix = {true, false, 0.0, 0.55};
        const auto zeroPrimary = render("staff-primary-zero-volume", score_, timeline_);
        check("Primary volume controls the original right hand, not the muted guide",
              pcmRms(zeroPrimary, 0.0, 2.0) == 0.0);
        options.mix = mix_;
        options.transpose = 12;
        options.speed = 0.5;
        const auto shifted = render("staff-slow-transposed", score_, timeline_);
        check("Speed-adjusted, transposed excerpt has sound with unchanged range length",
              shifted != both && pcmRms(shifted, 0.01, 0.10) > 1e-5);
        auto explicitBeats = timeline_;
        explicitBeats.metronomeBeats = {{0, true}, {720, true}, {1440, false}};
        options.transpose = 0;
        options.speed = 1;
        options.startSeconds = 0.5;
        options.endSeconds = 1.5;
        options.mix = {false, false, 0.5, 0.5};
        options.metronome = true;
        const auto meterClicks = render("staff-explicit-meter-clicks", score_, explicitBeats);
        check("Dynamic metronome table preserves nonuniform beat positions across an excerpt start",
              pcmRms(meterClicks, 0.0, 0.20) == 0.0 && pcmRms(meterClicks, 0.27, 0.32) > 1e-5);
    }

    void advance()
    {
        if (elapsed_.elapsed() < nextAt_)
            return;
        if (elapsed_.elapsed() > 60000)
        {
            check("Staff playback probe completed before timeout", false);
            finish();
            return;
        }
        try
        {
            switch (stage_)
            {
            case 0:
                if (!QDir().mkpath(folder_))
                    throw std::runtime_error("Staff playback diagnostic folder could not be created");
                makeFixture();
                checkWaves();
                require(player_.setAudioBackend(args_.value("audio-backend") == "system"
                                                    ? AudioBackend::WindowsMidi
                                                    : AudioBackend::SampledPiano));
                require(player_.setGmSoundFontPath(QApplication::applicationDirPath() +
                                                   "/assets/soundfonts/GeneralUser-GS.sf2"));
                player_.setSpeed(0.25);
                require(player_.load(score_, timeline_, plan_, settings_));
                require(player_.setPracticeMix(mix_));
                player_.seek(1000);
                require(player_.play());
                break;
            case 1:
            {
                const auto voices = player_.voiceState();
                check("Original staff plays simultaneous primary chord and other-staff chord without channel 0",
                      voices.melodyPitch == -1 && voices.melodyNoteOns == 0 &&
                          pitchesEqual(voices.chordPitches, {72, 76, 79}) &&
                          pitchesEqual(voices.bassPitches, {48, 55, 60}) && voices.chordNoteOns == 3 &&
                          voices.bassNoteOns == 3);
                previous_ = voices;
                beforeTick_ = player_.positionTicks();
                mix_.melodyEnabled = false;
                require(player_.setPracticeMix(mix_));
                break;
            }
            case 2:
            {
                const auto voices = player_.voiceState();
                check("Primary mute releases only the right hand and keeps other hand's lifetimes and position",
                      voices.chordPitches.empty() && pitchesEqual(voices.bassPitches, {48, 55, 60}) &&
                          voices.bassNoteOns == previous_.bassNoteOns && player_.isPlaying() &&
                          player_.positionTicks() > beforeTick_);
                mix_.melodyEnabled = true;
                require(player_.setPracticeMix(mix_));
                break;
            }
            case 3:
            {
                const auto voices = player_.voiceState();
                check("Primary unmute restores held pitches once without restarting the other hand",
                      pitchesEqual(voices.chordPitches, {72, 76, 79}) && voices.chordNoteOns == 6 &&
                          voices.bassNoteOns == previous_.bassNoteOns);
                previous_ = voices;
                mix_.accompanimentEnabled = false;
                require(player_.setPracticeMix(mix_));
                break;
            }
            case 4:
            {
                const auto voices = player_.voiceState();
                check("Other-staff mute preserves the primary chord without reattack",
                      voices.bassPitches.empty() && pitchesEqual(voices.chordPitches, {72, 76, 79}) &&
                          voices.chordNoteOns == previous_.chordNoteOns);
                mix_.accompanimentEnabled = true;
                require(player_.setPracticeMix(mix_));
                break;
            }
            case 5:
            {
                const auto voices = player_.voiceState();
                check("Other-staff unmute restores every held chord tone without reattacking primary",
                      pitchesEqual(voices.bassPitches, {48, 55, 60}) && voices.bassNoteOns == 6 &&
                          voices.chordNoteOns == previous_.chordNoteOns);
                previous_ = voices;
                player_.setVolume(0.2);
                mix_.melodyVolume = 0.2;
                mix_.accompanimentVolume = 0.3;
                require(player_.setPracticeMix(mix_));
                break;
            }
            case 6:
            {
                const auto voices = player_.voiceState();
                check("Independent hand-volume updates do not restart any voice",
                      voices.chordNoteOns == previous_.chordNoteOns &&
                          voices.bassNoteOns == previous_.bassNoteOns &&
                          player_.practiceMix().melodyVolume == 0.2 &&
                          player_.practiceMix().accompanimentVolume == 0.3);
                previous_ = voices;
                settings_.chordProgram = 40;
                require(player_.setAccompanimentSettings(settings_));
                check("Changing primary instrument releases only the primary hand",
                      player_.voiceState().chordPitches.empty() &&
                          player_.voiceState().bassNoteOns == previous_.bassNoteOns && player_.isPlaying());
                beforeTick_ = player_.positionTicks();
                break;
            }
            case 7:
            {
                const auto voices = player_.voiceState();
                check("Primary instrument change restores held notes without reattacking the other hand",
                      pitchesEqual(voices.chordPitches, {72, 76, 79}) &&
                          voices.chordNoteOns == previous_.chordNoteOns + 3 &&
                          voices.bassNoteOns == previous_.bassNoteOns && player_.positionTicks() > beforeTick_ &&
                          voices.melodyNoteOns == 0);
                previous_ = voices;
                settings_.bassProgram = 35;
                require(player_.setAccompanimentSettings(settings_));
                break;
            }
            case 8:
            {
                const auto voices = player_.voiceState();
                check("Other-staff instrument change preserves the primary held-note lifetimes",
                      pitchesEqual(voices.bassPitches, {48, 55, 60}) &&
                          voices.bassNoteOns == previous_.bassNoteOns + 3 &&
                          voices.chordNoteOns == previous_.chordNoteOns);
                auto invalid = settings_;
                invalid.chordProgram = 128;
                check("Invalid GM program is rejected without releasing sounding original-staff notes",
                      !player_.setAccompanimentSettings(invalid) &&
                          player_.voiceState().chordNoteOns == voices.chordNoteOns &&
                          player_.voiceState().bassNoteOns == voices.bassNoteOns);
                require(player_.setAccompanimentSettings(settings_));
                player_.pause();
                beforeTick_ = player_.positionTicks();
                break;
            }
            case 9:
            {
                const auto voices = player_.voiceState();
                check("Pause releases both staves and preserves exact musical position",
                      !player_.isPlaying() && player_.positionTicks() == beforeTick_ &&
                          voices.chordPitches.empty() && voices.bassPitches.empty() && voices.melodyPitch == -1);
                player_.seek(3000);
                require(player_.play());
                break;
            }
            case 10:
            {
                const auto voices = player_.voiceState();
                check("Seek/resume respects independent note durations, including already-ended chord tones",
                      pitchesEqual(voices.chordPitches, {72}) && pitchesEqual(voices.bassPitches, {48}) &&
                          voices.melodyPitch == -1 && player_.positionTicks() >= 3000);
                require(player_.setTranspose(12));
                break;
            }
            case 11:
            {
                const auto voices = player_.voiceState();
                check("Transpose restores both original staves at shifted pitches with no guide duplication",
                      pitchesEqual(voices.chordPitches, {84}) && pitchesEqual(voices.bassPitches, {60}) &&
                          voices.melodyPitch == -1);
                player_.seek(1000);
                break;
            }
            case 12:
            case 13:
            case 14:
            {
                const auto voices = player_.voiceState();
                check(QString("Loop-style seek %1 does not accumulate shared-pitch or cross-hand voices")
                          .arg(stage_ - 11),
                      pitchesEqual(voices.chordPitches, {84, 88, 91}) &&
                          pitchesEqual(voices.bassPitches, {60, 67, 72}) && voices.melodyNoteOns == 0 &&
                          player_.isPlaying() && player_.positionTicks() < 1200);
                player_.seek(stage_ == 14 ? 1500 : 1000);
                break;
            }
            case 15:
            {
                const auto voices = player_.voiceState();
                check("An overlapping unison ending does not cut off its longer original-staff note",
                      pitchesEqual(voices.chordPitches, {84, 88, 91}));
                mix_.melodyEnabled = false;
                mix_.accompanimentEnabled = false;
                require(player_.setPracticeMix(mix_));
                beforeTick_ = player_.positionTicks();
                break;
            }
            case 16:
            {
                const auto voices = player_.voiceState();
                check("All-muted staff transport advances without any submitted sounding voices",
                      player_.isPlaying() && player_.positionTicks() > beforeTick_ &&
                          voices.chordPitches.empty() && voices.bassPitches.empty() && voices.melodyPitch == -1);
                mix_.melodyEnabled = true;
                require(player_.setPracticeMix(mix_));
                break;
            }
            case 17:
            {
                const auto voices = player_.voiceState();
                check("Primary recovers correctly after both hands were muted",
                      pitchesEqual(voices.chordPitches, {84, 88, 91}) && voices.bassPitches.empty());
                player_.setSpeed(2.0);
                player_.seek(player_.durationTicks() - 32);
                break;
            }
            case 18:
            {
                const auto voices = player_.voiceState();
                check("Timeline ending releases all original-staff voices",
                      !player_.isPlaying() && player_.positionTicks() == player_.durationTicks() &&
                          voices.chordPitches.empty() && voices.bassPitches.empty());
                player_.stop();
                require(player_.setTranspose(0));
                player_.setSpeed(0.25);
                settings_.originalStaff = false;
                require(player_.load(score_, timeline_, plan_, settings_));
                mix_ = {true, true, 0.65, 0.55};
                require(player_.setPracticeMix(mix_));
                player_.seek(1000);
                require(player_.play());
                break;
            }
            case 19:
            {
                const auto voices = player_.voiceState();
                check("Legacy generated-accompaniment routing still includes its single melody voice",
                      voices.melodyPitch == 69 && voices.melodyNoteOns == 1 &&
                          pitchesEqual(voices.chordPitches, {72, 76, 79}) &&
                          pitchesEqual(voices.bassPitches, {48, 55, 60}));
                mix_.accompanimentEnabled = false;
                require(player_.setPracticeMix(mix_));
                break;
            }
            case 20:
            {
                const auto voices = player_.voiceState();
                check("Legacy accompaniment mute still silences both generated roles but not melody",
                      voices.melodyPitch == 69 && voices.chordPitches.empty() && voices.bassPitches.empty());
                finish();
                return;
            }
            }
            ++stage_;
            nextAt_ = elapsed_.elapsed() + 160;
        }
        catch (const std::exception &error)
        {
            error_ = QString::fromUtf8(error.what());
            check(error_, false);
            finish();
        }
    }

    void finish()
    {
        timer_.stop();
        player_.stop();
        player_.releaseAudioDevice();
        QSaveFile report(args_.value("report"));
        const QByteArray bytes =
            QJsonDocument(QJsonObject{{"passed", passed_},
                                      {"checks", checks_},
                                      {"artifacts", artifacts_},
                                      {"elapsedMilliseconds", elapsed_.elapsed()},
                                      {"error", error_},
                                      {"validation", "submitted realtime voices and genuine offline sampled PCM"}})
                .toJson();
        const bool saved =
            report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() && report.commit();
        app_.exit(passed_ && saved ? 0 : 2);
    }

    const QCommandLineParser &args_;
    QApplication &app_;
    QString folder_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    PlaybackEngine player_;
    Score score_;
    Timeline timeline_;
    AccompanimentPlan plan_;
    Score unisonScore_;
    AccompanimentPlan unisonPlan_;
    AccompanimentSettings settings_;
    PracticeMix mix_;
    PlaybackVoiceState previous_;
    std::int64_t beforeTick_ = 0;
    int stage_ = 0;
    qint64 nextAt_ = 0;
    QJsonArray checks_;
    QJsonArray artifacts_;
    bool passed_ = true;
    QString error_;
};

} // namespace

void runStaffPlaybackCheck(const QCommandLineParser &args, QApplication &app)
{
    new StaffPlaybackProbe(args, app);
}

} // namespace singlilt

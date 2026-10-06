// Accompaniment generation, persistence, and playback checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AccompanimentCheck.h"
#include "domain/AccompanimentTimeline.h"
#include "i18n/LanguageManager.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ScoreView.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace singlilt
{
namespace
{
class AccompanimentProbe final : public QObject
{
  public:
    AccompanimentProbe(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                       QApplication &app)
        : QObject(&window), window_(window), languages_(languages), args_(args), app_(app)
    {
        timer_.setInterval(80);
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        elapsed_.start();
        timer_.start();
    }

  private:
    template <typename T> T *control(const char *name)
    {
        auto *widget = window_.findChild<T *>(name);
        check(QString("control:%1").arg(name), widget != nullptr);
        return widget;
    }

    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ = passed_ && passed;
    }

    void finish()
    {
        timer_.stop();
        window_.player().stop();
        QJsonObject report{{"passed", passed_},
                           {"checks", checks_},
                           {"audioDevice", window_.player().deviceName()},
                           {"language", languages_.language()},
                           {"elapsedMilliseconds", elapsed_.elapsed()}};
        if (args_.isSet("screenshot"))
            report.insert("screenshotSaved", window_.grab().save(args_.value("screenshot")));
        QFile file(args_.value("report"));
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0)
        {
            app_.exit(4);
            return;
        }
        app_.exit(passed_ ? 0 : 3);
    }

    bool cursorAndLyricMatch()
    {
        const auto tick = window_.player().positionTicks();
        const auto index = window_.timeline().eventIndexAtTick(tick);
        const auto *view = dynamic_cast<ScoreView *>(window_.findChild<QGraphicsView *>("scoreView"));
        const auto *lyric = window_.findChild<QLabel *>("lyric");
        if (!index || !view || !lyric)
            return false;
        const auto &event = window_.timeline().events[*index];
        return view->currentNoteIndex() == static_cast<int>(event.sourceNoteIndex) &&
               lyric->text() == QString::fromStdString(event.lyric);
    }

    void advance()
    {
        if (elapsed_.elapsed() > 45000)
        {
            check("deadline", false);
            finish();
            return;
        }
        if (window_.isAudioLoading() || window_.isRecognizing())
            return;
        try
        {
            runStage();
        }
        catch (const std::exception &error)
        {
            check(QString::fromUtf8(error.what()), false);
            finish();
        }
    }

    void runStage()
    {
        auto &player = window_.player();
        if (stage_ == 0)
        {
            Project fixture = makePracticeScore();
            fixture.score.title = "P0 transport fixture";
            fixture.score.bpm = 120;
            fixture.score.notes.clear();
            fixture.score.repeats = {{0, 2, 2, -1}};
            for (int i = 0; i < 8; ++i)
            {
                Note note;
                note.id = i;
                note.degree = i < 2 ? 1 : i == 5 ? 0 : (i % 3) * 2 + 1;
                note.durationTicks = 960;
                note.measure = i / 2;
                note.tieToNext = i == 0;
                note.verseLyrics = {"A" + std::to_string(i), "B" + std::to_string(i)};
                note.source = {double(40 + i * 80), 80, 30, 40};
                fixture.score.notes.push_back(note);
            }
            fixture.accompaniment = generateAccompaniment(fixture.score);
            fixture.practiceMix.accompanimentEnabled = true;
            fixture.practiceMix.accompanimentVolume = 0.45;
            window_.setProject(std::move(fixture));
            check("arrangement-generated", window_.project().accompaniment->valid());
            const auto output = QFileInfo(args_.value("report")).absolutePath() + "/ui-confirmed.jpp";
            saveProject(output, window_.project());
            const auto restored = loadProject(output);
            check("schema3-roundtrip", restored.accompaniment.has_value() &&
                                           restored.accompaniment->melodyFingerprint ==
                                               window_.project().accompaniment->melodyFingerprint &&
                                           restored.practiceMix.accompanimentEnabled);
            melody_ = control<QCheckBox>("melodyEnabled");
            accompaniment_ = control<QCheckBox>("accompanimentEnabled");
            auto *play = control<QPushButton>("play");
            if (!melody_ || !accompaniment_ || !play)
            {
                finish();
                return;
            }
            play->click();
            ++stage_;
            return;
        }
        if (stage_ == 1)
        {
            if (!player.isPlaying() || player.positionTicks() < 100)
                return;
            const auto voices = player.voiceState();
            check("simultaneous-three-voices",
                  voices.melodyPitch >= 0 && !voices.chordPitches.empty() && !voices.bassPitches.empty());
            beforeTick_ = player.positionTicks();
            melody_->setChecked(false);
            const auto muted = player.voiceState();
            check("mute-only-channel0", muted.melodyPitch == -1 && !muted.chordPitches.empty() &&
                                            !muted.bassPitches.empty() && player.isPlaying());
            nextAt_ = elapsed_.elapsed() + 400;
            ++stage_;
            return;
        }
        if (elapsed_.elapsed() < nextAt_)
            return;
        if (stage_ == 2)
        {
            check("muted-clock-advances", player.positionTicks() > beforeTick_ + 100);
            check("muted-cursor-lyrics", cursorAndLyricMatch());
            const auto before = player.voiceState();
            previousMelodyOns_ = before.melodyNoteOns;
            melody_->setChecked(true);
            nextAt_ = elapsed_.elapsed() + 120;
            stage_ = 20;
            return;
        }
        if (stage_ == 20)
        {
            const auto restored = player.voiceState();
            check("sustain-resumes-current-note", restored.melodyPitch >= 0 &&
                                                      restored.melodyNoteOns > previousMelodyOns_ &&
                                                      player.positionTicks() > beforeTick_);
            for (int i = 0; i < 8; ++i)
            {
                melody_->setChecked(false);
                melody_->setChecked(true);
            }
            nextAt_ = elapsed_.elapsed() + 100;
            stage_ = 21;
            return;
        }
        if (stage_ == 21)
        {
            check("rapid-toggle-no-voice-loss",
                  player.voiceState().melodyPitch >= 0 && !player.voiceState().chordPitches.empty());
            const auto melodyOns = player.voiceState().melodyNoteOns;
            accompaniment_->setChecked(false);
            check("accompaniment-mute-isolated",
                  player.voiceState().chordPitches.empty() && player.voiceState().bassPitches.empty() &&
                      player.voiceState().melodyPitch >= 0 && player.voiceState().melodyNoteOns == melodyOns);
            accompaniment_->setChecked(true);
            auto arrangement = *window_.project().accompaniment;
            arrangement.settings.pattern = AccompanimentPattern::Arpeggio;
            const auto plan = buildAccompanimentPlan(window_.project().score, window_.timeline(), arrangement);
            check("pattern-switch", player.setAccompanimentPlan(plan, arrangement.settings));
            check("pattern-preserves-melody-attack", player.voiceState().melodyNoteOns == melodyOns);
            check("transpose-accepted", player.setTranspose(2) && player.transpose() == 2);
            check("invalid-transpose-retains-last", !player.setTranspose(25) && player.transpose() == 2);
            check("transpose-restored", player.setTranspose(0));
            player.setSpeed(2);
            check("speed-shared-clock", player.speed() == 2);
            player.setSpeed(1);
            player.pause();
            const auto paused = player.voiceState();
            check("pause-clears-all",
                  paused.melodyPitch == -1 && paused.chordPitches.empty() && paused.bassPitches.empty());
            const auto &events = window_.timeline().events;
            const auto b = std::find_if(events.begin(), events.end(),
                                        [](const auto &event) { return event.verseIndex == 1; });
            check("repeat-b-exists", b != events.end());
            if (b != events.end())
                player.seek(b->startTick + 180);
            check("resume-after-seek", player.play());
            nextAt_ = elapsed_.elapsed() + 180;
            stage_ = 3;
            return;
        }
        if (stage_ == 3)
        {
            check("repeat-b-cursor-lyrics", player.currentVerseIndex() == 1 && cursorAndLyricMatch());
            check("b-melody-program-independent", player.currentProgram() == 4);
            const auto tick = player.positionTicks();
            const auto oldMix = player.practiceMix();
            check("language-switch", languages_.setLanguage(languages_.language() == "en_US" ? "zh_CN" : "en_US"));
            check("language-preserves-mix-position",
                  player.positionTicks() >= tick && player.practiceMix().melodyEnabled == oldMix.melodyEnabled &&
                      player.practiceMix().accompanimentEnabled == oldMix.accompanimentEnabled);
            auto *generate = control<QPushButton>("generateAccompaniment");
            if (generate)
                generate->click();
            nextAt_ = elapsed_.elapsed() + 100;
            ++stage_;
            return;
        }
        if (stage_ == 4)
        {
            auto *audition = control<QPushButton>("accompanimentAudition");
            if (audition)
                audition->click();
            beforeTick_ = player.positionTicks();
            nextAt_ = elapsed_.elapsed() + 200;
            ++stage_;
            return;
        }
        if (stage_ == 5)
        {
            auto *cancel = control<QPushButton>("accompanimentCancel");
            const auto mix = window_.project().practiceMix;
            if (cancel)
                cancel->click();
            check("audition-cancel-keeps-current-tick",
                  player.positionTicks() >= beforeTick_ && player.isPlaying());
            check("audition-cancel-restores-mix",
                  player.practiceMix().melodyEnabled == mix.melodyEnabled &&
                      player.practiceMix().accompanimentEnabled == mix.accompanimentEnabled);
            player.pause();
            const auto &events = window_.timeline().events;
            const auto rest =
                std::find_if(events.begin(), events.end(), [](const auto &event) { return event.midiPitch < 0; });
            if (rest != events.end())
                player.seek(rest->startTick + 20);
            check("rest-resume", player.play());
            melody_->setChecked(false);
            melody_->setChecked(true);
            check("rest-does-not-create-note", player.voiceState().melodyPitch == -1);
            melody_->setChecked(false);
            accompaniment_->setChecked(false);
            beforeTick_ = player.positionTicks();
            nextAt_ = elapsed_.elapsed() + 160;
            ++stage_;
            return;
        }
        if (stage_ == 6)
        {
            check("all-muted-clock-advances", player.isPlaying() && player.positionTicks() > beforeTick_);
            const auto silent = player.voiceState();
            check("all-muted-no-voices",
                  silent.melodyPitch == -1 && silent.chordPitches.empty() && silent.bassPitches.empty());
            player.seek(player.durationTicks() - 120);
            nextAt_ = elapsed_.elapsed() + 300;
            ++stage_;
            return;
        }
        if (stage_ == 7)
        {
            check("all-muted-ends", !player.isPlaying() && player.positionTicks() == player.durationTicks());
            player.stop();
            check("stop-resets-position", player.positionTicks() == 0);
            auto *start = control<QPushButton>("practiceLoopStart");
            auto *end = control<QPushButton>("practiceLoopEnd");
            auto *loop = control<QCheckBox>("practiceLoop");
            if (!start || !end || !loop)
            {
                finish();
                return;
            }
            player.seek(0);
            start->click();
            player.seek(480);
            end->click();
            player.seek(0);
            melody_->setChecked(true);
            accompaniment_->setChecked(true);
            loop->setChecked(true);
            previousMelodyOns_ = player.voiceState().melodyNoteOns;
            control<QPushButton>("play")->click();
            nextAt_ = elapsed_.elapsed() + 1250;
            ++stage_;
            return;
        }
        if (stage_ == 8)
        {
            const auto voices = player.voiceState();
            check("loop-shared-position", player.isPlaying() && player.positionTicks() < 640 &&
                                              player.currentVerseIndex() == 0 &&
                                              voices.melodyNoteOns >= previousMelodyOns_ + 2);
            check("loop-no-accumulating-voices",
                  voices.chordPitches.size() <= 3 && voices.bassPitches.size() <= 1);
            control<QCheckBox>("practiceLoop")->setChecked(false);
            player.stop();
            Score boundary;
            boundary.notes = {Note{}};
            boundary.notes[0].degree = 5;
            boundary.notes[0].octave = 4;
            check("high-boundary-load", player.load(boundary));
            check("high-boundary-legal", player.setTranspose(12) && player.transpose() == 12);
            check("high-boundary-reject-retain", !player.setTranspose(13) && player.transpose() == 12);
            player.setTranspose(0);
            boundary.notes[0].degree = 1;
            boundary.notes[0].octave = -5;
            check("low-boundary-load", player.load(boundary));
            check("low-boundary-reject-retain", !player.setTranspose(-1) && player.transpose() == 0);
            Score overlap;
            overlap.bpm = 400;
            overlap.notes = {Note{}};
            overlap.notes[0].durationTicks = 3840;
            AccompanimentPlan plan;
            plan.durationTicks = 3840;
            plan.events = {{0, 1920, 64, 62, AccompanimentRole::Chord},
                           {960, 1920, 64, 62, AccompanimentRole::Chord},
                           {0, 3840, 43, 68, AccompanimentRole::Bass}};
            check("overlap-plan-load", player.load(overlap, buildTimeline(overlap), plan));
            player.setSpeed(1);
            player.setPracticeMix({true, true, 0.5, 0.4});
            check("overlap-start", player.play());
            stage_ = 9;
            nextAt_ = 0;
            return;
        }
        if (stage_ == 9)
        {
            const auto tick = player.positionTicks();
            if (tick < 2050)
                return;
            const auto voices = player.voiceState();
            check("overlap-off-does-not-kill-shared-pitch",
                  tick < 2880 && voices.chordPitches == std::vector<int>{64} && voices.chordNoteOns == 1);
            player.seek(2200);
            stage_ = 10;
            nextAt_ = elapsed_.elapsed() + 80;
            return;
        }
        if (stage_ == 10)
        {
            check("overlap-seek-restores-remaining-note",
                  player.voiceState().chordPitches == std::vector<int>{64});
            player.seek(3000);
            stage_ = 11;
            nextAt_ = elapsed_.elapsed() + 80;
            return;
        }
        const auto ended = player.voiceState();
        check("last-overlap-off-bass-continues",
              ended.chordPitches.empty() && ended.bassPitches == std::vector<int>{43});
        player.pause();
        check("overlap-pause-clears", player.voiceState().melodyPitch == -1 &&
                                          player.voiceState().chordPitches.empty() &&
                                          player.voiceState().bassPitches.empty());
        finish();
    }

    MainWindow &window_;
    LanguageManager &languages_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QJsonArray checks_;
    QCheckBox *melody_ = nullptr;
    QCheckBox *accompaniment_ = nullptr;
    int stage_ = 0;
    qint64 nextAt_ = 0;
    std::int64_t beforeTick_ = 0;
    std::uint64_t previousMelodyOns_ = 0;
    bool passed_ = true;
};
} // namespace

void runAccompanimentCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                           QApplication &app)
{
    new AccompanimentProbe(window, languages, args, app);
}
} // namespace singlilt

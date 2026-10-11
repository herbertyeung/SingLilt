// Instrument selection and note-audition checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "NotePreviewCheck.h"
#include "audio/SoundFontInstrument.h"
#include "i18n/LanguageManager.h"
#include "ui/ClassroomDialog.h"
#include "ui/InstrumentNames.h"
#include "ui/MainWindow.h"
#include "ui/NotationRenderer.h"
#include "ui/OptionsDialog.h"
#include "ui/ScoreView.h"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPointer>
#include <QSpinBox>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
void clickNote(ScoreView &view, const Note &note)
{
    const auto &box = note.source;
    view.ensureVisible(QRectF(box.x, box.y, box.width, box.height), 10, 10);
    const QPoint local = view.mapFromScene(QPointF(box.x + box.width / 2, box.y + box.height / 2));
    const QPoint global = view.viewport()->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
}
class NotePreviewProbe final : public QObject
{
  public:
    NotePreviewProbe(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                     QApplication &app)
        : QObject(&window), window_(window), languages_(languages), args_(args), app_(app),
          folder_(QFileInfo(args.value("report")).absolutePath())
    {
        QDir().mkpath(folder_);
        elapsed_.start();
        timer_.setInterval(30);
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        timer_.start();
    }

  private:
    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ = passed_ && passed;
    }
    void finish(const QString &error = {})
    {
        timer_.stop();
        if (!error.isEmpty())
            check(error, false);
        if (classroom_)
            classroom_->close();
        window_.player().stop();
        QFile file(args_.value("report"));
        if (!file.open(QIODevice::WriteOnly))
        {
            app_.exit(2);
            return;
        }
        file.write(QJsonDocument(QJsonObject{{"passed", passed_},
                                             {"checks", checks_},
                                             {"elapsedMilliseconds", elapsed_.elapsed()},
                                             {"submittedVoiceStateNotAcousticRecording", true},
                                             {"instrumentAudio", instruments_}})
                       .toJson());
        app_.exit(passed_ ? 0 : 2);
    }
    bool ready(PlaybackEngine &player)
    {
        return !player.isPreviewLoading();
    }
    void advance()
    {
        try
        {
            if (elapsed_.elapsed() > 60000)
            {
                finish("Preview diagnostic timeout");
                return;
            }
            auto &player = window_.player();
            if (!ready(player))
                return;
            if (step_ == 0)
            {
                if (args_.isSet("instrument-check"))
                {
                    auto *a = window_.findChild<QComboBox *>("programA");
                    auto *b = window_.findChild<QComboBox *>("programB");
                    bool catalog = a->count() == int(CommonInstruments.size()) && b->count() == a->count();
                    for (const auto &entry : CommonInstruments)
                        catalog &= a->findData(entry.program) >= 0 && b->findData(entry.program) >= 0 &&
                                   a->itemText(a->findData(entry.program)) == trText(entry.key);
                    check("38 named instruments share the A/B playback catalog", catalog);
                    OptionsDialog options(window_.appSettings(), window_.optionsContext(), &window_);
                    auto *defaults = options.findChild<QComboBox *>("optionsProgramA");
                    bool same = defaults->count() == 128;
                    for (const auto &entry : CommonInstruments)
                        same &= defaults->itemText(defaults->findData(entry.program)) == trText(entry.key);
                    check("Options has matching named programs and preserves all128 GM IDs", same);
                    defaults->setCurrentIndex(defaults->findData(40));
                    languages_.setLanguage("en_US");
                    QApplication::processEvents();
                    check("Named instruments translate without changing selection",
                          defaults->currentData().toInt() == 40 && defaults->currentText() == "Violin");
                    check("Other GM IDs retain numbered fallback",
                          defaults->itemText(defaults->findData(127)).contains("128") &&
                              !defaults->itemText(defaults->findData(127)).contains("%1"));
                    languages_.setLanguage("zh_CN");
                    QApplication::processEvents();
                    auto test = window_.project();
                    test.score.versePrograms = {40, 71};
                    const QString saved = folder_ + "/instrument-selection.jpp";
                    saveProject(saved, test);
                    check("Added A/B programs survive saving and reopening",
                          loadProject(saved).score.versePrograms == test.score.versePrograms);
                    SoundFontInstrument sampler;
                    if (args_.isSet("gm-soundfont"))
                        sampler.setGmSoundFontPath(args_.value("gm-soundfont"));
                    if (!sampler.open(false))
                        throw std::runtime_error(sampler.errorString().toStdString());
                    std::vector<float> samples(48000 * 2);
                    bool audible = true;
                    for (const auto &entry : CommonInstruments)
                    {
                        if (!sampler.silenceChannel(0) || !sampler.render(samples.data(), 48000) ||
                            !sampler.setProgram(0, entry.program) || !sampler.setChannelVolume(0, 0.9) ||
                            !sampler.noteOn(0, 60, 88) || !sampler.render(samples.data(), 48000))
                            throw std::runtime_error(sampler.errorString().toStdString());
                        double squares = 0;
                        for (float sample : samples)
                            squares += double(sample) * sample;
                        const double rms = std::sqrt(squares / samples.size());
                        audible &= std::isfinite(rms) && rms > 0.000001;
                        instruments_.append(QJsonObject{{"program", entry.program}, {"rms", rms}});
                        sampler.noteOff(0, 60);
                    }
                    check("All38 actual presets render nonzero sampled audio", audible);
                    if (!sampler.silenceChannel(0) || !sampler.render(samples.data(), 48000) ||
                        !sampler.setProgram(0, 0) || !sampler.setProgram(1, 0) ||
                        !sampler.setChannelVolume(0, 1.0) || !sampler.setChannelVolume(1, 1.0))
                        throw std::runtime_error(sampler.errorString().toStdString());
                    sampler.setOutputBoost(true);
                    for (int pitch = 48; pitch < 84; ++pitch)
                        if (!sampler.noteOn(pitch % 2, pitch, 127))
                            throw std::runtime_error(sampler.errorString().toStdString());
                    double densePeak = 0;
                    bool denseFinite = true;
                    for (int frame = 0; frame < 48000; frame += 512)
                    {
                        const int count = std::min(512, 48000 - frame);
                        if (!sampler.render(samples.data(), count))
                            throw std::runtime_error(sampler.errorString().toStdString());
                        for (int sample = 0; sample < count * 2; ++sample)
                        {
                            denseFinite &= std::isfinite(samples[std::size_t(sample)]);
                            densePeak = std::max(densePeak, double(std::abs(samples[std::size_t(sample)])));
                        }
                    }
                    check("Enhanced 36-note fortissimo stereo output stays finite below PCM saturation",
                          denseFinite && densePeak > 0.01 && densePeak <= 0.980001);
                    instruments_.append(QJsonObject{{"densePeak", densePeak}, {"outputBoost", true}});
                    sampler.allNotesOff();
                }
                window_.setProject(makePracticeScore());
                check("External lesson opts out of beat accents", !window_.project().score.accentBeats);
                const auto &events = window_.timeline().events;
                check("Scale 1 and 2 share attack velocity", events[0].velocity == events[1].velocity);
                check("All scale tones use the configured equal velocity",
                      std::all_of(events.begin(), events.end(), [&](const auto &event)
                                  { return event.velocity == window_.project().score.baseVelocity; }));
                auto *view = dynamic_cast<ScoreView *>(window_.findChild<QGraphicsView *>("scoreView"));
                view->fitWidth();
                clickNote(*view, window_.project().score.notes[0]);
                check("Cold click queues a short preview",
                      player.isPreviewLoading() || player.voiceState().previewPitch == 60);
                nextAt_ = elapsed_.elapsed() + 120;
                ++step_;
                return;
            }
            if (elapsed_.elapsed() < nextAt_)
                return;
            if (step_ == 1)
            {
                const auto state = player.voiceState();
                check("Main viewport click sounds do", state.previewPitch == 60 && state.previewVelocity == 88);
                check("Preview does not start the transport", !player.isPlaying() && player.positionTicks() == 0);
                check("Preview keeps the source score intact",
                      window_.project().score.notes.size() == 16 && !window_.hasUnsavedChanges());
                player.previewNote(62, 4, 88, 250);
                nextAt_ = elapsed_.elapsed() + 80;
                ++step_;
                return;
            }
            if (step_ == 2)
            {
                check("A second request replaces the prior preview and timbre",
                      player.voiceState().previewPitch == 62 && player.voiceState().previewProgram == 4);
                player.previewNote(64, 0, 88, 200);
                player.previewNote(67, 0, 88, 200);
                nextAt_ = elapsed_.elapsed() + 70;
                ++step_;
                return;
            }
            if (step_ == 3)
            {
                check("Rapid requests settle on the latest note", player.voiceState().previewPitch == 67);
                nextAt_ = elapsed_.elapsed() + 230;
                ++step_;
                return;
            }
            if (step_ == 4)
            {
                check("Idle preview releases on its deadline",
                      player.voiceState().previewPitch == -1 && !player.isPlaying());
                player.previewNote(72, 0);
                nextAt_ = elapsed_.elapsed() + 50;
                ++step_;
                return;
            }
            if (step_ == 5)
            {
                player.previewNote(-1, 0);
                nextAt_ = elapsed_.elapsed() + 50;
                ++step_;
                return;
            }
            if (step_ == 6)
            {
                check("Rest cancels audition without advancing time",
                      player.voiceState().previewPitch == -1 && player.positionTicks() == 0);
                check("Invalid preview is rejected", !player.previewNote(128, 0) && !player.previewNote(60, 128) &&
                                                         !player.previewNote(60, 0, 0));
                player.setMetronome(false);
                check("Existing full-song playback still starts", player.play());
                player.previewNote(77, 0, 88, 300);
                beforeTick_ = player.positionTicks();
                nextAt_ = elapsed_.elapsed() + 100;
                ++step_;
                return;
            }
            if (step_ == 7)
            {
                check("Preview channel coexists with the melody",
                      player.isPlaying() && player.positionTicks() > beforeTick_ &&
                          player.voiceState().melodyPitch >= 0 && player.voiceState().previewPitch == 77);
                player.stop();
                check("Stop silences preview as well as the transport",
                      player.voiceState().previewPitch == -1 && !player.isPlaying());
                auto *transpose = window_.findChild<QSpinBox *>("transpose");
                if (!transpose)
                    throw std::runtime_error("Missing main transpose control");
                transpose->setValue(2);
                auto *view = dynamic_cast<ScoreView *>(window_.findChild<QGraphicsView *>("scoreView"));
                clickNote(*view, window_.project().score.notes[1]);
                nextAt_ = elapsed_.elapsed() + 100;
                ++step_;
                return;
            }
            if (step_ == 8)
            {
                check("Main click follows global transpose", player.voiceState().previewPitch == 64);
                player.stop();
                auto project = window_.project();
                project.score.notes[0].octave = 1;
                project.score.notes[0].accidental = -1;
                project.score.notes[0].keyOverride = 2;
                project.score.versePrograms = {4};
                project.practiceSettings = ProjectPracticeSettings{};
                project.practiceSettings->transpose = 2;
                window_.setProject(std::move(project));
                auto *view = dynamic_cast<ScoreView *>(window_.findChild<QGraphicsView *>("scoreView"));
                clickNote(*view, window_.project().score.notes[0]);
                nextAt_ = elapsed_.elapsed() + 100;
                ++step_;
                return;
            }
            if (step_ == 9)
            {
                check("Clicked octave/accidental/key override and timbre match the score",
                      player.voiceState().previewPitch == 75 && player.voiceState().previewProgram == 4);
                player.stop();
                const auto savedTick = player.positionTicks();
                player.releaseAudioDevice();
                check("Device handoff preserves the main score and position",
                      player.positionTicks() == savedTick &&
                          player.durationTicks() == window_.timeline().durationTicks);
                classroom_ = new ClassroomDialog(languages_, player.audioBackend(),
                                                 QApplication::applicationDirPath() + "/assets/lessons",
                                                 folder_ + "/history.json", &window_);
                classroom_->show();
                nextAt_ = elapsed_.elapsed() + 100;
                ++step_;
                return;
            }
            auto &lessonPlayer = classroom_->player();
            if (!ready(lessonPlayer))
                return;
            if (step_ == 10)
            {
                auto *view = dynamic_cast<ScoreView *>(classroom_->findChild<QGraphicsView *>("classroomScore"));
                auto score = classroom_->lessons()[0].score;
                // Use the view's real rendered anchors, not the JSON's placeholder rectangles.
                const auto image = renderNumberedScore(score);
                view->setScore(image, score);
                view->fitWidth();
                clickNote(*view, score.notes[1]);
                nextAt_ = elapsed_.elapsed() + 100;
                ++step_;
                return;
            }
            if (step_ == 11)
            {
                check("Classroom viewport click sounds re", lessonPlayer.voiceState().previewPitch == 62 &&
                                                                lessonPlayer.voiceState().previewVelocity == 88);
                check("Classroom click keeps full playback paused", !lessonPlayer.isPlaying());
                nextAt_ = elapsed_.elapsed() + 700;
                ++step_;
                return;
            }
            check("Classroom preview also releases automatically", lessonPlayer.voiceState().previewPitch == -1);
            finish();
        }
        catch (const std::exception &error)
        {
            finish(QString::fromUtf8(error.what()));
        }
    }
    MainWindow &window_;
    LanguageManager &languages_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QString folder_;
    QPointer<ClassroomDialog> classroom_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QJsonArray checks_;
    QJsonArray instruments_;
    int step_ = 0;
    qint64 nextAt_ = 0, beforeTick_ = 0;
    bool passed_ = true;
};
} // namespace
void runNotePreviewCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                         QApplication &app)
{
    new NotePreviewProbe(window, languages, args, app);
}
} // namespace singlilt

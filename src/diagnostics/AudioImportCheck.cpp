// Audio-analysis task and candidate-review checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AudioImportCheck.h"
#include "ui/MainWindow.h"
#include "ui/ScoreView.h"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <functional>

namespace singlilt
{
namespace
{
class AudioProbe final : public QObject
{
  public:
    AudioProbe(MainWindow &window, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), args_(args), app_(app)
    {
        timer_.setInterval(80);
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        elapsed_.start();
        timer_.start();
    }

  private:
    void check(const char *name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ = passed_ && passed;
    }
    bool awaitPlayback(const std::function<bool()> &ready)
    {
        timer_.stop();
        QElapsedTimer elapsed;
        elapsed.start();
        while (!ready() && elapsed.elapsed() < 5000)
        {
            QApplication::processEvents();
            QThread::msleep(5);
        }
        timer_.start();
        return ready();
    }
    void finish()
    {
        timer_.stop();
        window_.audioTranscriptionTask().cancel();
        window_.originalAudioPlayer().stop();
        QJsonObject report{{"passed", passed_},
                           {"checks", checks_},
                           {"error", error_},
                           {"elapsedMilliseconds", elapsed_.elapsed()}};
        report.insert("playbackObservations", observations_);
        if (args_.isSet("screenshot"))
            report["screenshotSaved"] = window_.grab().save(args_.value("screenshot"));
        QFile file(args_.value("report"));
        if (!file.open(QIODevice::WriteOnly))
        {
            app_.exit(4);
            return;
        }
        file.write(QJsonDocument(report).toJson());
        app_.exit(passed_ ? 0 : 3);
    }
    void advance()
    {
        if (elapsed_.elapsed() > 60000)
        {
            check("deadline", false);
            finish();
            return;
        }
        if (elapsed_.elapsed() < nextAt_)
            return;
        try
        {
            runStage();
        }
        catch (const std::exception &exception)
        {
            error_ = QString::fromUtf8(exception.what());
            check("exception", false);
            finish();
        }
    }
    void runStage()
    {
        auto &task = window_.audioTranscriptionTask();
        auto &original = window_.originalAudioPlayer();
        const auto path = args_.value("audio-task-check");
        if (stage_ == 0)
        {
            baseline_ = scoreToJson(window_.project().score);
            check("original-open", original.open(path));
            options_.endSeconds = std::min(8.0, original.durationSeconds());
            options_.bpm = 120;
            options_.tonic = 0;
            options_.separateVocals = !args_.isSet("input-isolated-vocals");
            options_.separatorCacheDirectory = args_.value("separator-cache-dir");
            check("background-start", task.start(path, options_));
            check("main-controls-remain-enabled", window_.centralWidget()->isEnabled());
            ++stage_;
            return;
        }
        if (stage_ == 1)
        {
            if (task.isRunning())
                return;
            if (task.state() != AudioTranscriptionTask::State::Ready)
                throw std::runtime_error(task.errorString().toStdString());
            check("completion-does-not-replace-score", scoreToJson(window_.project().score) == baseline_);
            window_.findChild<QPushButton *>("audioTaskPreview")->click();
            auto *preview = window_.findChild<QDialog *>("audioRecognitionPreview");
            check("nonmodal-audio-preview", preview && !preview->isModal());
            if (!preview)
            {
                finish();
                return;
            }
            if (task.result()->vocalsSeparated)
            {
                check("real-stem-paths-returned", QFileInfo::exists(task.result()->vocalsPath) &&
                                                      QFileInfo::exists(task.result()->instrumentalPath));
                preview->findChild<QPushButton *>("previewSeparatedVocals")->click();
                auto *audition = preview->findChild<QObject *>("audioStemPreviewAudition");
                check("real-vocal-preview-playing",
                      awaitPlayback(
                          [&]
                          {
                              return audition && !audition->property("auditionLoading").toBool() &&
                                     audition->property("auditionPlaying").toBool();
                          }));
                check("vocal-preview-keeps-accepted-score", scoreToJson(window_.project().score) == baseline_);
                preview->findChild<QPushButton *>("previewSeparatedInstrumental")->click();
                check("real-instrumental-preview-playing",
                      awaitPlayback(
                          [&]
                          {
                              return audition && !audition->property("auditionLoading").toBool() &&
                                     audition->property("auditionPlaying").toBool();
                          }));
                preview->findChild<QPushButton *>("previewSeparatedStop")->click();
                check("stem-preview-keeps-accepted-source",
                      window_.project().score.title == baseline_.value("title").toString().toStdString());
                selectedAudioSource_ = 2;
            }
            QTimer::singleShot(0, &window_,
                               [this]
                               {
                                   if (auto *confirmation =
                                           window_.findChild<QMessageBox *>("audioReplaceConfirmation"))
                                       confirmation->done(QMessageBox::Yes);
                               });
            preview->findChild<QPushButton *>("recognitionApply")->click();
            check("confirmed-generated-notation",
                  window_.project().generatedNotation && window_.project().audioSource.has_value());
            const auto saved = QFileInfo(args_.value("report")).absolutePath() + "/audio-roundtrip.jpp";
            saveProject(saved, window_.project());
            const auto restored = loadProject(saved);
            check("audio-mapping-roundtrip",
                  restored.generatedNotation && restored.audioSource &&
                      restored.audioSource->timings.size() == restored.score.notes.size());
            check("actual-digit-anchors",
                  !restored.image.isNull() && restored.score.notes.front().source.width > 0);
            window_.findChild<QComboBox *>("playbackSource")->setCurrentIndex(selectedAudioSource_);
            check("original-source-loading-completes", awaitPlayback([&] { return !original.isLoading(); }));
            if (selectedAudioSource_ == 2)
                check("confirmed-vocal-source-selected",
                      original.sourcePath() == QString::fromStdString(window_.project().audioSource->vocalsPath));
            window_.findChild<QPushButton *>("play")->click();
            beforeSeconds_ = original.positionSeconds();
            sourceStartedAt_ = elapsed_.elapsed();
            nextAt_ = elapsed_.elapsed() + 260;
            ++stage_;
            return;
        }
        if (stage_ == 2)
        {
            if (original.isPlaying() && original.positionSeconds() <= beforeSeconds_ + 0.1 &&
                elapsed_.elapsed() - sourceStartedAt_ < 2000)
                return;
            observations_.append(QJsonObject{{"beforeSeconds", beforeSeconds_},
                                             {"observedSeconds", original.positionSeconds()},
                                             {"isPlaying", original.isPlaying()},
                                             {"waitMilliseconds", elapsed_.elapsed() - sourceStartedAt_}});
            check("original-clock-advances",
                  original.isPlaying() && original.positionSeconds() > beforeSeconds_ + 0.1);
            check("sources-not-simultaneous", !window_.player().isPlaying());
            auto *view = dynamic_cast<ScoreView *>(window_.findChild<QGraphicsView *>("scoreView"));
            check("original-clock-drives-score-cursor", view && view->currentNoteIndex() >= 0);
            beforeSeconds_ = original.positionSeconds();
            window_.findChild<QPushButton *>("generateAccompaniment")->click();
            window_.findChild<QPushButton *>("accompanimentAudition")->click();
            stage_ = 20;
            nextAt_ = elapsed_.elapsed() + 180;
            return;
        }
        if (stage_ == 20)
        {
            if (window_.isAudioLoading())
                return;
            check("accompaniment-audition-selects-synth",
                  window_.player().isPlaying() && !original.isPlaying() &&
                      window_.findChild<QComboBox *>("playbackSource")->currentData().toInt() == 0);
            window_.findChild<QPushButton *>("accompanimentCancel")->click();
            check("audition-cancel-restores-original-current-position",
                  original.isPlaying() && !window_.player().isPlaying() &&
                      original.positionSeconds() >= beforeSeconds_ &&
                      window_.findChild<QComboBox *>("playbackSource")->currentData().toInt() ==
                          selectedAudioSource_);
            window_.findChild<QPushButton *>("play")->click();
            check("original-pause", !original.isPlaying());
            const auto &timing = window_.project().audioSource->timings.back();
            auto *slider = window_.findChild<QSlider *>("position");
            slider->setValue(int(timing.startTick));
            slider->sliderReleased();
            check("mapped-original-seek", std::abs(original.positionSeconds() - timing.startSeconds) < 0.1);
            if (selectedAudioSource_ == 2)
            {
                window_.findChild<QComboBox *>("playbackSource")->setCurrentIndex(3);
                check("confirmed-instrumental-source-selected",
                      original.sourcePath() ==
                          QString::fromStdString(window_.project().audioSource->instrumentalPath));
            }
            window_.findChild<QComboBox *>("playbackSource")->setCurrentIndex(0);
            check("source-switch-keeps-paused-position",
                  !original.isPlaying() &&
                      std::abs(double(window_.player().positionTicks() - timing.startTick)) < 5);
            task.discard();
            check("second-background-start", task.start(path, options_));
            task.cancel();
            stage_ = 3;
            return;
        }
        if (stage_ == 3)
        {
            if (task.isRunning())
                return;
            check("cancel-terminal-no-result",
                  task.state() == AudioTranscriptionTask::State::Cancelled && !task.result());
            task.discard();
            check("error-path-start", task.start(path + ".missing", options_));
            ++stage_;
            return;
        }
        if (task.isRunning())
            return;
        check("missing-audio-failure-not-overwrite", task.state() == AudioTranscriptionTask::State::Failed &&
                                                         window_.project().generatedNotation && !task.result());
        finish();
    }
    MainWindow &window_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QJsonArray checks_;
    QJsonArray observations_;
    QJsonObject baseline_;
    AudioTranscriptionOptions options_;
    QString error_;
    int stage_ = 0;
    int selectedAudioSource_ = 1;
    qint64 nextAt_ = 0;
    qint64 sourceStartedAt_ = 0;
    double beforeSeconds_ = 0;
    bool passed_ = true;
};
} // namespace
void runAudioImportCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    new AudioProbe(window, args, app);
}
} // namespace singlilt

// WAV export range, timing, and failure-handling checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "WaveExportCheck.h"
#include "audio/WaveRenderer.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/PracticeScore.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
constexpr int SampleRate = 48000;
constexpr int ReleaseFrames = SampleRate * 3 / 2;

double pcmRms(const QString &path, double startSeconds, double endSeconds)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(44 + qint64(startSeconds * SampleRate) * 4))
        throw std::runtime_error("Could not read rendered WAV samples");
    const auto bytes = file.read(qint64((endSeconds - startSeconds) * SampleRate) * 4);
    if (bytes.isEmpty() || bytes.size() % 2 != 0)
        throw std::runtime_error("Rendered WAV sample interval is empty");
    long double squareSum = 0.0;
    for (qsizetype offset = 0; offset < bytes.size(); offset += 2)
    {
        const auto sample = qFromLittleEndian<qint16>(reinterpret_cast<const uchar *>(bytes.constData() + offset));
        squareSum += static_cast<long double>(sample) * sample;
    }
    return std::sqrt(double(squareSum / (bytes.size() / 2))) / 32768.0;
}

qint64 waveFrames(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Rendered WAV is missing");
    const auto header = file.read(44);
    if (header.size() != 44 || header.left(4) != "RIFF" || header.mid(8, 8) != "WAVEfmt " ||
        header.mid(36, 4) != "data")
        throw std::runtime_error("Rendered WAV header is invalid");
    const auto bytes = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(header.constData() + 40));
    if (file.size() != 44 + bytes || bytes % 4 != 0)
        throw std::runtime_error("Rendered WAV length is inconsistent");
    return bytes / 4;
}

class WaveExportProbe final : public QObject
{
  public:
    WaveExportProbe(MainWindow &window, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), args_(args), app_(app),
          folder_(QFileInfo(args.value("report")).absolutePath()),
          previousNativeDialogs_(QApplication::testAttribute(Qt::AA_DontUseNativeDialogs))
    {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        if (!QDir().mkpath(folder_))
            throw std::runtime_error("Could not create WAV export check folder");
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        timer_.setInterval(50);
        elapsed_.start();
        QTimer::singleShot(100, this, [this] { run(); });
    }

  private:
    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ = passed_ && passed;
    }

    QString output(const QString &name)
    {
        const QString path = folder_ + "/" + name + ".wav";
        if (QFile::exists(path) && !QFile::remove(path))
            throw std::runtime_error("Could not replace an old WAV check artifact");
        return path;
    }

    void checkRenderer()
    {
        Score score;
        score.bpm = 60;
        Note note;
        note.id = 1;
        note.tieToNext = true;
        score.notes.push_back(note);
        note.id = 2;
        note.tieToNext = false;
        score.notes.push_back(note);
        const auto originalScore = scoreToJson(score);
        const auto timeline = buildTimeline(score);
        AccompanimentPlan plan;
        plan.durationTicks = timeline.durationTicks;
        plan.events.push_back({0, timeline.durationTicks, 48, 70, AccompanimentRole::Chord});
        plan.events.push_back({0, timeline.durationTicks, 36, 70, AccompanimentRole::Bass});
        WaveRenderOptions options;
        options.startSeconds = 0.75;
        options.endSeconds = 1.25;
        const QString melody = output("tied-boundary");
        const auto rendered = renderWave(score, timeline, plan, melody, options);
        check("explicit sub-second range has exact music frames and release tail",
              rendered.musicFrames == SampleRate / 2 && rendered.frames == SampleRate / 2 + ReleaseFrames &&
                  rendered.startSeconds == 0.75 && rendered.endSeconds == 1.25 && rendered.fragment &&
                  waveFrames(melody) == rendered.frames);
        check("tie crossing selected start is audible immediately", pcmRms(melody, 0.01, 0.10) > 1e-5);
        artifacts_.append(melody);
        options.mix.melodyEnabled = false;
        options.mix.accompanimentEnabled = true;
        const QString accompaniment = output("accompaniment-boundary");
        const auto accompanimentResult = renderWave(score, timeline, plan, accompaniment, options);
        check("chord and bass crossing selected start remain audible",
              pcmRms(accompaniment, 0.01, 0.10) > 1e-5 && accompanimentResult.musicFrames == SampleRate / 2);
        artifacts_.append(accompaniment);
        options.mix.accompanimentEnabled = false;
        options.metronome = true;
        options.startSeconds = 0.25;
        options.endSeconds = 1.25;
        const QString metronome = output("metronome-phase");
        renderWave(score, timeline, plan, metronome, options);
        check("metronome retains source beat rather than accenting range start",
              pcmRms(metronome, 0.0, 0.70) == 0.0 && pcmRms(metronome, 0.76, 0.80) > 1e-5);
        artifacts_.append(metronome);
        check("renderer never mutates source score", scoreToJson(score) == originalScore);
        const QString invalid = output("invalid-range");
        for (const auto &range : {std::pair{-1.0, 1.0}, std::pair{0.0, 3.0}, std::pair{1.0, 0.5},
                                  std::pair{2.0, 2.0}, std::pair{std::numeric_limits<double>::quiet_NaN(), 1.0}})
        {
            options.startSeconds = range.first;
            options.endSeconds = range.second;
            bool rejected = false;
            try
            {
                renderWave(score, timeline, plan, invalid, options);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(QString("invalid range %1/%2 rejected without output").arg(range.first).arg(range.second),
                  rejected && !QFile::exists(invalid));
        }
        for (auto &longNote : score.notes)
            longNote.durationTicks = 64 * TicksPerQuarter;
        const auto longTimeline = buildTimeline(score);
        AccompanimentPlan silentPlan;
        silentPlan.durationTicks = longTimeline.durationTicks;
        options.maxSeconds = 120;
        options.startSeconds = 0;
        options.endSeconds = 121;
        bool capped = false;
        try
        {
            renderWave(score, longTimeline, silentPlan, invalid, options);
        }
        catch (const std::exception &)
        {
            capped = true;
        }
        check("explicit range above 120 seconds is rejected without output", capped && !QFile::exists(invalid));
        for (auto &longNote : score.notes)
            longNote.durationTicks = 256 * TicksPerQuarter;
        auto additionalNote = score.notes.back();
        additionalNote.id = 3;
        score.notes.push_back(additionalNote);
        const auto extendedTimeline = buildTimeline(score);
        silentPlan.durationTicks = extendedTimeline.durationTicks;
        options.maxSeconds = 600;
        options.endSeconds = 601;
        capped = false;
        try
        {
            renderWave(score, extendedTimeline, silentPlan, invalid, options);
        }
        catch (const std::exception &)
        {
            capped = true;
        }
        check("explicit range above the 600-second selected cap is rejected without output",
              capped && !QFile::exists(invalid));
        options.maxSeconds = 601;
        options.endSeconds = 600;
        capped = false;
        try
        {
            renderWave(score, extendedTimeline, silentPlan, invalid, options);
        }
        catch (const std::exception &)
        {
            capped = true;
        }
        check("explicit renderer cap above 600 seconds is rejected without output",
              capped && !QFile::exists(invalid));
        capped = false;
        try
        {
            renderWave(score, invalid, 121);
        }
        catch (const std::exception &)
        {
            capped = true;
        }
        check("Legacy simple renderer retains its 120-second bound", capped && !QFile::exists(invalid));
    }

    void startExport(bool cancel)
    {
        optionsSeen_ = false;
        fileSeen_ = false;
        cancel_ = cancel;
        auto *action = window_.findChild<QAction *>("menuExportWave");
        if (!action)
            throw std::runtime_error("WAV export menu action is missing");
        timer_.start();
        action->trigger();
        if (!optionsSeen_)
            throw std::runtime_error("WAV export range dialog did not open");
    }

    void scheduleExport(bool cancel)
    {
        // A modal loop must not run inside the timer that operates its controls.
        QTimer::singleShot(0, this,
                           [this, cancel]
                           {
                               try
                               {
                                   startExport(cancel);
                               }
                               catch (const std::exception &error)
                               {
                                   error_ = QString::fromUtf8(error.what());
                                   finish();
                               }
                           });
    }

    void run()
    {
        try
        {
            checkRenderer();
            auto project = makePracticeScore();
            project.practiceSettings.reset();
            project.accompaniment = generateAccompaniment(project.score);
            project.accompaniment->melodyFingerprint += "-before-correction";
            project.practiceMix.accompanimentEnabled = false;
            window_.setProject(std::move(project));
            window_.player().setSpeed(0.5);
            baselineScore_ = scoreToJson(window_.project().score);
            expectedSeconds_ = window_.timeline().durationSeconds() / 0.5;
            uiOutput_ = output("slow-full-ui");
            stage_ = 0;
            startExport(false);
        }
        catch (const std::exception &error)
        {
            error_ = QString::fromUtf8(error.what());
            finish();
        }
    }

    void operateModal(QDialog &dialog)
    {
        if (dialog.objectName() == "waveExportOptions" && !optionsSeen_)
        {
            optionsSeen_ = true;
            auto *start = dialog.findChild<QDoubleSpinBox *>("waveExportStart");
            auto *end = dialog.findChild<QDoubleSpinBox *>("waveExportEnd");
            auto *range = dialog.findChild<QLabel *>("waveExportRange");
            if (!start || !end || !range)
                throw std::runtime_error("WAV export range fields are missing");
            if (stage_ == 0)
                check("slow UI export defaults to complete performed length",
                      start->value() == 0 && std::abs(end->value() - expectedSeconds_) < 1e-5);
            if (stage_ >= 2)
            {
                check("A score beyond ten minutes defaults to the explicit 600-second cap", end->value() == 600);
                start->setValue(125.25);
                end->setValue(126.75);
                check("long range can choose a later practice interval",
                      start->value() == 125.25 && end->value() == 126.75 &&
                          end->maximum() - start->value() <= 600);
                if (args_.isSet("screenshot"))
                    check("range screenshot saved", dialog.grab().save(args_.value("screenshot")));
            }
            if (cancel_)
                dialog.reject();
            else
                dialog.accept();
            return;
        }
        if (dialog.objectName() == "waveExportFile" && !fileSeen_)
        {
            auto *file = qobject_cast<QFileDialog *>(&dialog);
            if (!file)
                throw std::runtime_error("WAV export file chooser is invalid");
            if (stage_ == 3)
            {
                fileSeen_ = true;
                dialog.reject();
                return;
            }
            file->setDirectory(folder_);
            auto *name = file->findChild<QLineEdit *>("fileNameEdit");
            if (!name)
                throw std::runtime_error("Non-native WAV file chooser filename field is missing");
            name->setText(QFileInfo(uiOutput_).fileName());
            metrics_.insert("lastChosenFile",
                            file->selectedFiles().isEmpty() ? QString() : file->selectedFiles().first());
            dialog.accept();
            fileSeen_ = !dialog.isVisible();
            if (!fileSeen_)
                throw std::runtime_error("WAV file chooser did not accept its filename");
        }
    }

    void advance()
    {
        try
        {
            if (elapsed_.elapsed() > 90000)
                throw std::runtime_error("WAV export check timed out");
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            {
                metrics_.insert("lastModal", dialog->objectName());
                metrics_.insert("lastModalTitle", dialog->windowTitle());
                operateModal(*dialog);
                return;
            }
            auto *action = window_.findChild<QAction *>("menuExportWave");
            if (!optionsSeen_ || !action || action->property("waveExportRunning").toBool())
                return;
            timer_.stop();
            if (stage_ == 0)
            {
                const auto musicFrames = waveFrames(uiOutput_) - ReleaseFrames;
                check("slow UI WAV contains all music rather than original-speed cap",
                      std::abs(musicFrames - std::llround(expectedSeconds_ * SampleRate)) <= 1);
                metrics_.insert("slowMusicSeconds", double(musicFrames) / SampleRate);
                artifacts_.append(uiOutput_);
                check("UI export preserves source score", scoreToJson(window_.project().score) == baselineScore_);
                check("stale disabled arrangement doesn't block melody UI export",
                      window_.project().accompaniment && !window_.project().practiceMix.accompanimentEnabled &&
                          !buildAccompanimentPlan(window_.project().score, window_.timeline(),
                                                  *window_.project().accompaniment)
                               .valid() &&
                          musicFrames > 0);
                ++stage_;
                uiOutput_ = output("cancelled-ui");
                scheduleExport(true);
                return;
            }
            if (stage_ == 1)
            {
                check("cancel leaves no output and never opens file chooser",
                      !QFile::exists(uiOutput_) && !fileSeen_);
                Project project;
                project.generatedNotation = true;
                project.score.bpm = 60;
                for (int index = 0; index < 612; ++index)
                {
                    Note note;
                    note.id = index + 1;
                    note.measure = index / 4;
                    project.score.notes.push_back(note);
                }
                window_.setProject(std::move(project));
                window_.player().setSpeed(1.0);
                ++stage_;
                uiOutput_ = output("long-selected-ui");
                scheduleExport(false);
                return;
            }
            if (stage_ == 3)
            {
                check("file chooser cancel leaves no output", fileSeen_ && !QFile::exists(uiOutput_));
                finish();
                return;
            }
            check("long UI WAV exports the chosen later interval", waveFrames(uiOutput_) == SampleRate * 3);
            metrics_.insert("longRangeStartSeconds", 125.25);
            metrics_.insert("longRangeEndSeconds", 126.75);
            artifacts_.append(uiOutput_);
            auto *status = window_.findChild<QLabel *>("statusMessage");
            check("completion status reports actual range and release tail",
                  status && status->text().contains("125.250") && status->text().contains("126.750") &&
                      status->text().contains("1.5"));
            ++stage_;
            uiOutput_ = output("cancelled-file-ui");
            scheduleExport(false);
        }
        catch (const std::exception &error)
        {
            error_ = QString::fromUtf8(error.what());
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
            finish();
        }
    }

    void finish()
    {
        if (finished_)
            return;
        finished_ = true;
        timer_.stop();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previousNativeDialogs_);
        passed_ = passed_ && error_.isEmpty();
        QFile report(args_.value("report"));
        if (!report.open(QIODevice::WriteOnly) ||
            report.write(QJsonDocument(QJsonObject{{"passed", passed_},
                                                   {"checks", checks_},
                                                   {"metrics", metrics_},
                                                   {"artifacts", artifacts_},
                                                   {"error", error_},
                                                   {"elapsedMilliseconds", elapsed_.elapsed()}})
                             .toJson()) < 0)
            passed_ = false;
        app_.exit(passed_ ? 0 : 2);
    }

    MainWindow &window_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QString folder_;
    QString uiOutput_;
    QString error_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QJsonArray checks_;
    QJsonArray artifacts_;
    QJsonObject metrics_;
    QJsonObject baselineScore_;
    double expectedSeconds_ = 0;
    int stage_ = 0;
    bool cancel_ = false;
    bool optionsSeen_ = false;
    bool fileSeen_ = false;
    bool previousNativeDialogs_ = false;
    bool passed_ = true;
    bool finished_ = false;
};
} // namespace

void runWaveExportCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    new WaveExportProbe(window, args, app);
}
} // namespace singlilt

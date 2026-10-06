// Local recognition checks using an explicit score image.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "LocalStaffImageCheck.h"
#include "audio/WaveRenderer.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QException>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace singlilt
{
namespace
{
using LocalState = LocalStaffRecognitionTask::State;

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Integration artifact could not be read");
    return file.readAll();
}

QJsonObject musicalMaterial(const StaffPerformance &performance)
{
    auto object = staffPerformanceToJson(performance);
    object.remove("primaryProgram");
    object.remove("otherProgram");
    return object;
}

bool displaysImage(QGraphicsView *view, const QImage &image)
{
    if (!view || !view->scene())
        return false;
    for (auto *item : view->scene()->items())
        if (auto *pixmap = qgraphicsitem_cast<QGraphicsPixmapItem *>(item))
            if (pixmap->pixmap().toImage().convertToFormat(QImage::Format_RGB32) ==
                image.convertToFormat(QImage::Format_RGB32))
                return true;
    return false;
}

struct ImageCheckAudio
{
    WaveRenderResult changed;
    WaveRenderResult piano;
    bool differentPcm = false;
};

class LocalStaffImageProbe final : public QObject
{
  public:
    LocalStaffImageProbe(MainWindow &window, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), app_(app), sourcePath_(args.value("local-staff-image")),
          reportPath_(args.value("report")), folder_(QFileInfo(reportPath_).absolutePath()), audio_(this)
    {
        timer_.setInterval(100);
        confirmation_.setInterval(5);
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        connect(&confirmation_, &QTimer::timeout, this,
                [this]
                {
                    auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                    if (message && message->objectName() == "localStaffReplaceConfirmation")
                    {
                        confirmationAnswered_ = true;
                        message->done(QMessageBox::Yes);
                    }
                });
        QTimer::singleShot(0, this, [this] { begin(); });
    }

  private:
    enum class Phase
    {
        Starting,
        Recognition,
        Audio,
        Finished
    };

    void check(const QString &name, bool condition)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", condition}});
        if (!condition)
            throw std::runtime_error(name.toStdString());
    }

    QDialog *preview() const
    {
        for (auto *dialog : window_.findChildren<QDialog *>("localStaffRecognitionPreview"))
            if (dialog->isVisible())
                return dialog;
        return nullptr;
    }

    bool noOptionsDialog() const
    {
        for (auto *dialog : window_.findChildren<QDialog *>())
            if (dialog->isVisible() && dynamic_cast<OptionsDialog *>(dialog))
                return false;
        return true;
    }

    bool protectedProject() const
    {
        return scoreToJson(window_.project().score) == protectedScore_ &&
               window_.project().image == protectedImage_ && !window_.hasUnsavedChanges();
    }

    void screenshot(QWidget &widget, const QString &name)
    {
        const auto path = QDir(folder_).filePath(name + ".png");
        check(name + " screenshot saved", widget.grab().save(path));
        artifacts_.append(path);
    }

    void begin()
    {
        try
        {
            check("Integration arguments identify a real source image",
                  !sourcePath_.isEmpty() && QFileInfo(sourcePath_).isFile() && !reportPath_.isEmpty());
            check("Report directory exists", QDir().mkpath(folder_));
            original_ = QImage(sourcePath_);
            check("Original source image decodes", !original_.isNull());
            sourceHash_ = QCryptographicHash::hash(fileBytes(sourcePath_), QCryptographicHash::Sha256);
            auto protectedCopy = window_.project();
            protectedCopy.score.title = "Protected project before real local OMR";
            window_.setProject(std::move(protectedCopy), false);
            protectedScore_ = scoreToJson(window_.project().score);
            protectedImage_ = window_.project().image;
            check("Local workflow starts with an empty API key", window_.appSettings().vision.apiKey.isEmpty());
            elapsed_.start();
            timer_.start();
            phase_ = Phase::Recognition;
            window_.importStaffImage(sourcePath_);
            check("User image starts the real local task", window_.localStaffRecognitionTask().isRunning());
            check("Local import does not open cloud Options", noOptionsDialog());
            auto *raw = preview();
            check("Raw source preview appears immediately", raw && raw->property("rawLocalStaffPreview").toBool());
            auto *apply = raw->findChild<QPushButton *>("recognitionApply");
            check("Pending raw preview cannot replace the score",
                  apply && !apply->isEnabled() && protectedProject());
            check("Pending preview displays the original pixels",
                  displaysImage(raw->findChild<QGraphicsView *>("previewScoreView"), original_));
            screenshot(*raw, "local-source-pending");
        }
        catch (const std::exception &error)
        {
            finish(false, QString::fromUtf8(error.what()));
        }
    }

    void ready()
    {
        auto &task = window_.localStaffRecognitionTask();
        check("Real local engine returns a complete candidate",
              task.state() == LocalState::Ready && task.result() && task.result()->valid());
        check("Ready does not silently replace the current project", protectedProject());
        check("Ready still does not require cloud Options", noOptionsDialog());
        const auto &candidate = *task.result()->project;
        const auto &performance = *candidate.staffPerformance;
        check("Both printed staves have recognized sounding notes",
              performance.staffCount == 2 &&
                  std::any_of(performance.notes.begin(), performance.notes.end(),
                              [](const auto &n) { return n.staff == 1; }) &&
                  std::any_of(performance.notes.begin(), performance.notes.end(),
                              [](const auto &n) { return n.staff == 2; }));
        const auto timeline = buildTimeline(candidate.score);
        const auto plan = buildStaffPerformancePlan(candidate.score, timeline, performance);
        check("Recognized full performance has a valid shared playback timeline",
              timeline.valid() && plan.valid());
        check("Detected anchors are bounded and missing anchors do not fabricate image positions",
              std::all_of(performance.notes.begin(), performance.notes.end(),
                          [&](const auto &n)
                          {
                              if (!n.hasImageAnchor)
                                  return n.source.x == 0 && n.source.y == 0 && n.source.width == 0 &&
                                         n.source.height == 0;
                              return n.source.width > 0 && n.source.height > 0 && n.source.x >= 0 &&
                                     n.source.y >= 0 &&
                                     n.source.x + n.source.width <= candidate.image.width() + 2 &&
                                     n.source.y + n.source.height <= candidate.image.height() + 2;
                          }));
        metrics_ = {{"actualWrittenNotes", int(performance.notes.size())},
                    {"actualGuideNotes", int(candidate.score.notes.size())},
                    {"actualPlaybackEvents", int(plan.events.size())},
                    {"bpm", candidate.score.bpm},
                    {"writtenTicks", double(performance.durationTicks)},
                    {"performedTicks", double(timeline.durationTicks)},
                    {"scoreSeconds", timeline.durationSeconds()},
                    {"warnings", QJsonArray::fromStringList(candidate.warnings)},
                    {"engineLog", task.result()->engineLog},
                    {"processing", candidate.processing}};
        realEngineCompleted_ = true;
        auto *dialog = preview();
        check("Ready preview replaces the pending display, not the active project",
              dialog && !dialog->property("rawLocalStaffPreview").toBool());
        auto *tabs = dialog->findChild<QTabWidget *>("localStaffImageTabs");
        auto *table = dialog->findChild<QTableWidget *>("previewNotes");
        check("Ready preview only shows the original image for playback",
              !tabs && candidate.staffImagePlayback && !candidate.generatedNotation &&
                  candidate.image == original_ &&
                  displaysImage(dialog->findChild<QGraphicsView *>("previewScoreView"), candidate.image));
        check("Ready preview lists every recognized full-performance event",
              table && table->rowCount() == int(performance.notes.size()));
        screenshot(*dialog, "local-ready-original");
        screenshot(*dialog, "local-ready-generated");
        const auto expectedCount = performance.notes.size();
        const auto expectedMaterial = musicalMaterial(performance);
        auto *apply = dialog->findChild<QPushButton *>("recognitionApply");
        if (auto *tempo = dialog->findChild<QDoubleSpinBox *>("staffTempoReview"))
        {
            auto *confirmed = dialog->findChild<QCheckBox *>("staffTempoConfirmed");
            check("Unknown tempo must be explicitly reviewed before applying",
                  confirmed && apply && !apply->isEnabled());
            tempo->setValue(60);
            confirmed->setChecked(true);
        }
        check("A complete candidate enables explicit confirmation", apply && apply->isEnabled());
        confirmation_.start();
        apply->click();
        confirmation_.stop();
        check("Only the local replacement confirmation was accepted", confirmationAnswered_);
        check("Explicit confirmation applies all recognized events",
              window_.project().staffPerformance &&
                  window_.project().staffPerformance->notes.size() == expectedCount &&
                  musicalMaterial(*window_.project().staffPerformance) == expectedMaterial);
        check("Applied candidate is consumed", task.state() == LocalState::Idle && !task.result());
        window_.setProject(window_.project(), false);
        const auto material = musicalMaterial(*window_.project().staffPerformance);
        const auto score = scoreToJson(window_.project().score);
        auto *a = window_.findChild<QComboBox *>("programA");
        auto *b = window_.findChild<QComboBox *>("programB");
        check("Both hand selectors expose all 128 General MIDI programs",
              a && b && a->isEnabled() && b->isEnabled() && a->count() == 128 && b->count() == 128);
        for (int program = 0; program < 128; ++program)
            check("Every GM program is selectable for both hands",
                  a->findData(program) >= 0 && b->findData(program) >= 0);
        a->setCurrentIndex(a->findData(40));
        b->setCurrentIndex(b->findData(35));
        check("UI instrument changes select violin and fretless bass",
              window_.project().staffPerformance->primaryProgram == 40 &&
                  window_.project().staffPerformance->otherProgram == 35);
        check("Instrument changes preserve every pitch and written time",
              musicalMaterial(*window_.project().staffPerformance) == material &&
                  scoreToJson(window_.project().score) == score && window_.hasUnsavedChanges());
        const QString projectPath = QDir(folder_).filePath("local-staff-violin-bass.jpp");
        saveProject(projectPath, window_.project());
        const auto loaded = loadProject(projectPath);
        check("Saved project retains both instruments and the complete musical material",
              loaded.staffPerformance && loaded.staffPerformance->primaryProgram == 40 &&
                  loaded.staffPerformance->otherProgram == 35 &&
                  musicalMaterial(*loaded.staffPerformance) == material);
        artifacts_.append(projectPath);
        screenshot(window_, "local-applied-gm");
        startAudio(loaded);
    }

    void startAudio(Project project)
    {
        const auto timeline = buildTimeline(project.score);
        const auto plan = buildStaffPerformancePlan(project.score, timeline, *project.staffPerformance);
        check("Complete recognized music fits the 120-second export boundary",
              timeline.durationSeconds() > 0 && timeline.durationSeconds() <= 120);
        WaveRenderOptions options;
        options.maxSeconds = 120;
        options.endSeconds = timeline.durationSeconds();
        options.mix = {true, true, 0.25, 0.25};
        options.settings.originalStaff = true;
        options.settings.chordProgram = 40;
        options.settings.bassProgram = 35;
        options.gmSoundFontPath = QApplication::applicationDirPath() + "/assets/soundfonts/GeneralUser-GS.sf2";
        check("An explicit real GM bank is available", QFileInfo(options.gmSoundFontPath).isFile());
        const auto changedPath = QDir(folder_).filePath("local-staff-violin-bass.wav");
        const auto pianoPath = QDir(folder_).filePath("local-staff-piano-comparison.wav");
        artifacts_.append(changedPath);
        artifacts_.append(pianoPath);
        audio_.setFuture(QtConcurrent::run(
            [project = std::move(project), timeline, plan, options, changedPath, pianoPath]() mutable
            {
                ImageCheckAudio result;
                result.changed = renderWave(project.score, timeline, plan, changedPath, options);
                options.endSeconds = std::min(10.0, timeline.durationSeconds());
                options.settings.chordProgram = 0;
                options.settings.bassProgram = 0;
                result.piano = renderWave(project.score, timeline, plan, pianoPath, options);
                const qsizetype bytes = qsizetype(result.piano.musicFrames * 4);
                result.differentPcm = fileBytes(changedPath).mid(44, bytes) != fileBytes(pianoPath).mid(44, bytes);
                return result;
            }));
        phase_ = Phase::Audio;
        elapsed_.restart();
    }

    void advance()
    {
        if (phase_ == Phase::Starting || phase_ == Phase::Finished || advancing_)
            return;
        advancing_ = true;
        try
        {
            if (phase_ == Phase::Recognition)
            {
                checkIfChanged();
                if (elapsed_.elapsed() > 180000)
                    throw std::runtime_error("Real local OMR integration exceeded 180 seconds");
                if (!window_.localStaffRecognitionTask().isRunning())
                    ready();
            }
            else if (phase_ == Phase::Audio)
            {
                if (elapsed_.elapsed() > 150000)
                    throw std::runtime_error("Offline audio verification exceeded its deadline");
                if (audio_.isFinished())
                {
                    const auto result = audio_.result();
                    check("Real full-score changed-instrument PCM is audible",
                          std::isfinite(result.changed.rms) && result.changed.rms > 1e-5);
                    check("Full changed-instrument export has no clipped samples",
                          result.changed.clippedSamples == 0);
                    check("Changed instruments produce different PCM from piano", result.differentPcm);
                    metrics_.insert("musicSeconds",
                                    double(result.changed.musicFrames) / result.changed.sampleRate);
                    metrics_.insert("rms", result.changed.rms);
                    metrics_.insert("peak", result.changed.peak);
                    metrics_.insert("clippedSamples", double(result.changed.clippedSamples));
                    metrics_.insert("audioEngine", result.changed.engine);
                    check("Source image file remains byte-identical",
                          QCryptographicHash::hash(fileBytes(sourcePath_), QCryptographicHash::Sha256) ==
                              sourceHash_);
                    finish(true);
                }
            }
        }
        catch (const QUnhandledException &exception)
        {
            QString message = "Offline audio verification failed";
            try
            {
                if (exception.exception())
                    std::rethrow_exception(exception.exception());
            }
            catch (const std::exception &cause)
            {
                message = QString::fromUtf8(cause.what());
            }
            catch (...)
            {
            }
            finish(false, message);
        }
        catch (const std::exception &error)
        {
            finish(false, QString::fromUtf8(error.what()));
        }
        advancing_ = false;
    }

    void checkIfChanged()
    {
        if (!protectedProject())
            check("Current project is protected throughout asynchronous recognition", false);
        if (!noOptionsDialog())
            check("Local workflow never opens cloud configuration", false);
        if (window_.localStaffRecognitionTask().state() == LocalState::Failed)
            throw std::runtime_error(window_.localStaffRecognitionTask().errorString().toStdString());
    }

    void finish(bool passed, const QString &error = {})
    {
        if (phase_ == Phase::Finished)
            return;
        phase_ = Phase::Finished;
        timer_.stop();
        confirmation_.stop();
        window_.localStaffRecognitionTask().cancel();
        const QJsonObject report{{"passed", passed},
                                 {"checks", checks_},
                                 {"error", error},
                                 {"engineLog", window_.localStaffRecognitionTask().engineLog()},
                                 {"metrics", metrics_},
                                 {"artifacts", artifacts_},
                                 {"realLocalEngineCompleted", realEngineCompleted_},
                                 {"recognitionAccuracyVerified", false}};
        const auto bytes = QJsonDocument(report).toJson();
        QTextStream(stdout) << bytes;
        QFile file(reportPath_);
        const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        app_.exit(written ? (passed ? 0 : 3) : 4);
    }

    MainWindow &window_;
    QApplication &app_;
    QString sourcePath_;
    QString reportPath_;
    QString folder_;
    QTimer timer_;
    QTimer confirmation_;
    QElapsedTimer elapsed_;
    QFutureWatcher<ImageCheckAudio> audio_;
    QJsonArray checks_;
    QJsonArray artifacts_;
    QJsonObject metrics_;
    QJsonObject protectedScore_;
    QImage protectedImage_;
    QImage original_;
    QByteArray sourceHash_;
    Phase phase_ = Phase::Starting;
    bool confirmationAnswered_ = false;
    bool realEngineCompleted_ = false;
    bool advancing_ = false;
};
} // namespace

void runLocalStaffImageCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    new LocalStaffImageProbe(window, args, app);
}
} // namespace singlilt

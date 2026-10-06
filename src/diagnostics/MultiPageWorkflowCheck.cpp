// Multi-page import, navigation, and playback checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MultiPageWorkflowCheck.h"
#include "recognition/StaffPageSplitter.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/ScoreView.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <stdexcept>

namespace singlilt
{
namespace
{
using LocalState = LocalStaffRecognitionTask::State;

QByteArray bytesFrom(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Workflow source or artifact could not be read");
    return file.readAll();
}

void writeOwnedCopy(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Owned workflow source copy could not be written");
}

bool showsPixels(QGraphicsView *view, const QImage &image)
{
    if (!view || !view->scene())
        return false;
    const auto expected = image.convertToFormat(QImage::Format_RGB32);
    for (auto *item : view->scene()->items())
        if (auto *pixmap = qgraphicsitem_cast<QGraphicsPixmapItem *>(item))
            if (pixmap->pixmap().toImage().convertToFormat(QImage::Format_RGB32) == expected)
                return true;
    return false;
}

QJsonObject material(const StaffPerformance &performance)
{
    auto json = staffPerformanceToJson(performance);
    json.remove("primaryProgram");
    json.remove("otherProgram");
    return json;
}

class MultiPageWorkflowProbe final : public QObject
{
  public:
    MultiPageWorkflowProbe(MainWindow &window, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), app_(app), inputFolder_(args.value("multi-page-inputs")),
          reportPath_(args.value("report")), folder_(QFileInfo(reportPath_).absolutePath())
    {
        timer_.setInterval(60);
        modalTimer_.setInterval(10);
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        connect(&modalTimer_, &QTimer::timeout, this, [this] { handleModal(); });
        QTimer::singleShot(0, this, [this] { begin(); });
    }

  private:
    enum class Phase
    {
        Starting,
        Recognition,
        Playback,
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

    bool noCloudOptions() const
    {
        for (auto *dialog : window_.findChildren<QDialog *>())
            if (dialog->isVisible() && dynamic_cast<OptionsDialog *>(dialog))
                return false;
        return !window_.cloudRecognitionTask().isRunning();
    }

    bool protectedProject() const
    {
        return scoreToJson(window_.project().score) == protectedScore_ &&
               window_.project().image == protectedImage_ && !window_.hasUnsavedChanges();
    }

    void capture(QWidget &widget, const QString &name)
    {
        const auto path = QDir(folder_).filePath(name + ".png");
        check(name + " screenshot saved", widget.grab().save(path));
        artifacts_.append(path);
    }

    void handleModal()
    {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        try
        {
            if (dialog->objectName() == "staffPageImportDialog" && !ordered_)
            {
                auto *list = dialog->findChild<QListWidget *>("staffPageOrder");
                auto *up = dialog->findChild<QPushButton *>("staffPageMoveUp");
                auto *down = dialog->findChild<QPushButton *>("staffPageMoveDown");
                auto *buttons = dialog->findChild<QDialogButtonBox *>("staffPageImportButtons");
                check("The user can confirm four independent page items",
                      list && list->count() == 4 && up && down && buttons);
                for (int index = 0; index < 4; ++index)
                    check("Initial page order follows source and left-right order",
                          list->item(index)->data(Qt::UserRole).toInt() == index);
                list->setCurrentRow(3);
                up->click();
                check("Move-up changes the selected page identity, not its source data",
                      list->item(2)->data(Qt::UserRole).toInt() == 3 &&
                          list->item(3)->data(Qt::UserRole).toInt() == 2);
                down->click();
                for (int index = 0; index < 4; ++index)
                    check("Page order is restored before real recognition",
                          list->item(index)->data(Qt::UserRole).toInt() == index);
                capture(*dialog, "workflow-page-order");
                ordered_ = true;
                buttons->button(QDialogButtonBox::Ok)->click();
            }
            else if (dialog->objectName() == "localStaffReplaceConfirmation" && allowApply_)
            {
                auto *message = qobject_cast<QMessageBox *>(dialog);
                check("Explicit apply reaches only the local replacement confirmation", message != nullptr);
                appliedConfirmation_ = true;
                message->done(QMessageBox::Yes);
            }
            else if (allowTempoCalibration_ && dynamic_cast<OptionsDialog *>(dialog))
            {
                auto *tempo = dialog->findChild<QDoubleSpinBox *>("optionsCurrentTempo");
                auto *buttons = dialog->findChild<QDialogButtonBox *>("optionsButtons");
                check("Manual source-tempo calibration uses the current-song UI control",
                      tempo && tempo->isEnabled() && buttons);
                tempo->setValue(62);
                capture(*dialog, "workflow-manual-tempo-62");
                tempoCalibrationConfirmed_ = true;
                buttons->button(QDialogButtonBox::Ok)->click();
                check("The manual tempo edit commits without a validation failure", !dialog->isVisible());
            }
        }
        catch (const std::exception &error)
        {
            dialog->reject();
            finish(false, QString::fromUtf8(error.what()));
        }
    }

    void begin()
    {
        try
        {
            check("Real workflow has an input folder and report destination",
                  !inputFolder_.isEmpty() && !reportPath_.isEmpty());
            check("Workflow output directory exists", QDir().mkpath(folder_));
            ownedInputs_ = QDir(folder_).filePath("owned-inputs");
            check("Owned input directory exists", QDir().mkpath(ownedInputs_));
            check("Owned copies are separate from the actual input folder",
                  QDir(ownedInputs_)
                          .canonicalPath()
                          .compare(QDir(inputFolder_).canonicalPath(), Qt::CaseInsensitive) != 0);
            for (const auto &name : {"spread-01-02.png", "spread-03-04.png"})
            {
                const auto source = QDir(inputFolder_).filePath(name);
                const auto bytes = bytesFrom(source);
                originals_.append(source);
                originalHashes_.push_back(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
                const auto copy = QDir(ownedInputs_).filePath(name);
                writeOwnedCopy(copy, bytes);
                copies_.append(copy);
                QImageReader reader(copy);
                reader.setAutoTransform(true);
                const auto image = reader.read();
                check("Real spread copy decodes", !image.isNull());
                auto pages = splitStaffPageInputs(image, name, int(copies_.size()) - 1);
                check("Each real spread proposes two distinct pages", pages.size() == 2);
                expectedInputs_.insert(expectedInputs_.end(), pages.begin(), pages.end());
            }
            auto protectedCopy = window_.project();
            protectedCopy.score.title = "Protected project before real four-page workflow";
            window_.setProject(std::move(protectedCopy), false);
            protectedScore_ = scoreToJson(window_.project().score);
            protectedImage_ = window_.project().image;
            check("The real local workflow requires no API key", window_.appSettings().vision.apiKey.isEmpty());
            elapsed_.start();
            timer_.start();
            modalTimer_.start();
            phase_ = Phase::Recognition;
            window_.importStaffImages(copies_);
            if (phase_ == Phase::Finished)
                return;
            check("Page ordering is explicitly confirmed before starting OMR", ordered_);
            check("Four-page recognition starts in the local background task",
                  window_.localStaffRecognitionTask().isRunning() &&
                      window_.localStaffRecognitionTask().pageInputs().size() == 4);
            recognitionStarted_ = true;
            check("No cloud configuration opens during local import", noCloudOptions());
            auto *pending = preview();
            check("Pending preview exposes four independent original pages",
                  pending && pending->property("rawLocalStaffPreview").toBool());
            auto *selector = pending->findChild<QComboBox *>("staffPreviewPageSelector");
            auto *apply = pending->findChild<QPushButton *>("recognitionApply");
            check("Pending page selection cannot apply a result",
                  selector && selector->count() == 4 && apply && !apply->isEnabled() && protectedProject());
            for (int page = 0; page < 4; ++page)
            {
                selector->setCurrentIndex(page);
                check("Pending preview shows the corresponding cropped original",
                      showsPixels(pending->findChild<QGraphicsView *>("previewScoreView"),
                                  expectedInputs_[page].image));
                capture(*pending, QString("workflow-pending-page-%1").arg(page + 1));
            }
        }
        catch (const std::exception &error)
        {
            finish(false, QString::fromUtf8(error.what()));
        }
    }

    void ready()
    {
        auto &task = window_.localStaffRecognitionTask();
        check("The real engine returns all four pages as one candidate",
              task.state() == LocalState::Ready && task.result() && task.result()->valid() &&
                  task.result()->project->staffPages.size() == 4);
        check("Recognition completion leaves the previous project untouched", protectedProject());
        const auto &candidate = *task.result()->project;
        const auto &performance = *candidate.staffPerformance;
        check("Real book includes full performance events", !performance.notes.empty());
        auto *dialog = preview();
        check("A ready nonmodal four-page preview is shown",
              dialog && !dialog->isModal() && !dialog->property("rawLocalStaffPreview").toBool());
        auto *selector = dialog->findChild<QComboBox *>("staffPreviewPageSelector");
        auto *tabs = dialog->findChild<QTabWidget *>("localStaffImageTabs");
        auto *table = dialog->findChild<QTableWidget *>("previewNotes");
        check("Ready preview has independent original pages and every event row",
              selector && selector->count() == 4 && candidate.staffImagePlayback && !candidate.generatedNotation &&
                  !tabs && table && table->rowCount() == int(performance.notes.size()));
        QJsonArray pageMetrics;
        for (int page = 0; page < 4; ++page)
        {
            const auto &stored = candidate.staffPages[page];
            check("Each page retains its label and cropped original pixels",
                  stored.label == expectedInputs_[page].label &&
                      stored.sourceImage == expectedInputs_[page].image && !stored.renderedImage.isNull() &&
                      stored.endTick > stored.startTick);
            selector->setCurrentIndex(page);
            check("Original preview follows the selected page without redraw",
                  showsPixels(dialog->findChild<QGraphicsView *>("previewScoreView"), stored.renderedImage));
            check("No second generated bitmap replaces the source",
                  stored.sourceImage == stored.renderedImage &&
                      candidate.image == candidate.staffPages[0].sourceImage);
            capture(*dialog, QString("workflow-ready-source-%1").arg(page + 1));
            capture(*dialog, QString("workflow-ready-rendered-%1").arg(page + 1));
            pageMetrics.append(QJsonObject{{"label", stored.label},
                                           {"startTick", double(stored.startTick)},
                                           {"endTick", double(stored.endTick)}});
        }
        const auto fourth = std::find_if(performance.notes.begin(), performance.notes.end(), [](const auto &note)
                                         { return note.pageIndex == 3 && note.hasImageAnchor; });
        check("The full event table includes fourth-page notes", fourth != performance.notes.end());
        selector->setCurrentIndex(0);
        const int row = int(std::distance(performance.notes.begin(), fourth));
        table->setCurrentCell(row, 0);
        check("Selecting a fourth-page row switches the correct generated image",
              selector->currentIndex() == 3 && showsPixels(dialog->findChild<QGraphicsView *>("previewScoreView"),
                                                           candidate.staffPages[3].renderedImage));
        QGraphicsRectItem *selection = nullptr;
        for (auto *item : dialog->findChild<QGraphicsView *>("previewScoreView")->scene()->items())
            if (item->data(0).toString() == "staffPreviewSelection")
                selection = qgraphicsitem_cast<QGraphicsRectItem *>(item);
        const auto &box = fourth->source;
        check("Cross-page table selection uses the fourth-page bounding box",
              selection && selection->isVisible() &&
                  selection->rect() == QRectF(box.x - 4, box.y - 6, box.width + 8, box.height + 12));
        const auto expectedMaterial = material(performance);
        QJsonArray measures;
        for (const auto &measure : candidate.score.writtenMeasures)
            measures.append(QJsonObject{{"number", measure.number},
                                        {"pageIndex", measure.pageIndex},
                                        {"startTick", double(measure.startTick)},
                                        {"durationTicks", double(measure.durationTicks)},
                                        {"beatsPerBar", measure.beatsPerBar},
                                        {"beatUnit", measure.beatUnit}});
        metrics_ = {{"actualNotes", int(performance.notes.size())},
                    {"bpm", candidate.score.bpm},
                    {"writtenTicks", double(performance.durationTicks)},
                    {"pages", pageMetrics},
                    {"measures", measures},
                    {"clefChanges", staffPerformanceToJson(performance).value("clefChanges")},
                    {"warnings", QJsonArray::fromStringList(candidate.warnings)},
                    {"engineLog", task.result()->engineLog}};
        auto *apply = dialog->findChild<QPushButton *>("recognitionApply");
        if (auto *tempo = dialog->findChild<QDoubleSpinBox *>("staffTempoReview"))
        {
            auto *confirmed = dialog->findChild<QCheckBox *>("staffTempoConfirmed");
            check("Unknown native tempo blocks application until reviewed",
                  confirmed && apply && !apply->isEnabled());
            metrics_.insert("nativeImportedBpm", candidate.score.bpm);
            tempo->setValue(62);
            confirmed->setChecked(true);
            metrics_.insert("previewTempoConfirmedFromSource", 62);
        }
        check("Ready requires explicit apply", apply && apply->isEnabled());
        allowApply_ = true;
        apply->click();
        allowApply_ = false;
        check("Only explicit local replacement was confirmed", appliedConfirmation_);
        check("Applied project retains all four pages and the whole performance",
              window_.project().staffPages.size() == 4 && window_.project().staffPerformance &&
                  material(*window_.project().staffPerformance) == expectedMaterial &&
                  task.state() == LocalState::Idle);
        window_.setProject(window_.project(), false);
        setProgramsAndPersist();
        testManualPages();
        startAutomaticPage(0);
    }

    void setProgramsAndPersist()
    {
        const auto before = material(*window_.project().staffPerformance);
        const auto originalScore = window_.project().score;
        std::vector<std::pair<std::int64_t, std::int64_t>> pageIntervals;
        for (const auto &page : window_.project().staffPages)
            pageIntervals.emplace_back(page.startTick, page.endTick);
        allowTempoCalibration_ = true;
        window_.openOptions(1);
        allowTempoCalibration_ = false;
        check("The known printed quarter-note tempo is explicitly calibrated to 62",
              tempoCalibrationConfirmed_ && window_.project().score.bpm == 62 && window_.timeline().bpm == 62);
        auto calibratedScore = window_.project().score;
        calibratedScore.bpm = originalScore.bpm;
        check("Manual BPM calibration changes no musical ticks, pitches or page-qualified events",
              scoreToJson(calibratedScore) == scoreToJson(originalScore) &&
                  material(*window_.project().staffPerformance) == before);
        for (int page = 0; page < 4; ++page)
            check("Manual BPM calibration preserves each written page interval",
                  window_.project().staffPages[page].startTick == pageIntervals[page].first &&
                      window_.project().staffPages[page].endTick == pageIntervals[page].second);
        metrics_.insert("importedBpmBeforeCalibration", originalScore.bpm);
        metrics_.insert("bpm", window_.project().score.bpm);
        metrics_.insert("manualTempoCalibration", true);
        metrics_.insert("tempoCalibrationSource", "Printed quarter=62, explicit current-song UI edit");
        auto *a = window_.findChild<QComboBox *>("programA");
        auto *b = window_.findChild<QComboBox *>("programB");
        auto *primary = window_.findChild<QCheckBox *>("melodyEnabled");
        auto *other = window_.findChild<QCheckBox *>("accompanimentEnabled");
        check("Both hand instruments and mutes remain available on a multi-page score",
              a && b && a->isEnabled() && b->isEnabled() && primary && other);
        a->setCurrentIndex(a->findData(40));
        b->setCurrentIndex(b->findData(35));
        primary->setChecked(false);
        other->setChecked(true);
        check("Primary-hand mute changes only the mix",
              !window_.project().practiceMix.melodyEnabled && window_.project().practiceMix.accompanimentEnabled &&
                  material(*window_.project().staffPerformance) == before);
        primary->setChecked(true);
        other->setChecked(false);
        check("Other-hand mute changes only the mix", window_.project().practiceMix.melodyEnabled &&
                                                          !window_.project().practiceMix.accompanimentEnabled &&
                                                          material(*window_.project().staffPerformance) == before);
        other->setChecked(true);
        check("Instruments do not alter page-qualified music or source clocks",
              window_.project().staffPerformance->primaryProgram == 40 &&
                  window_.project().staffPerformance->otherProgram == 35 &&
                  material(*window_.project().staffPerformance) == before && window_.hasUnsavedChanges());
        const QString path = QDir(folder_).filePath("four-page-real-workflow.jpp");
        saveProject(path, window_.project());
        artifacts_.append(path);
        for (const auto &copy : copies_)
        {
            check("External cleanup target belongs only to owned workflow inputs",
                  QFileInfo(copy).absolutePath() == QFileInfo(ownedInputs_).absoluteFilePath());
            check("Only an owned source copy is removed for self-contained verification", QFile::remove(copy));
        }
        QTemporaryDir isolated;
        check("An isolated package-only directory is available", isolated.isValid());
        const auto package = isolated.filePath("only-project.jpp");
        check("A package-only verification copy is created", QFile::copy(path, package));
        const auto loaded = loadProject(package);
        check("JPP loads without the external source copies",
              loaded.staffPages.size() == 4 && loaded.staffPerformance &&
                  staffPerformanceToJson(*loaded.staffPerformance) ==
                      staffPerformanceToJson(*window_.project().staffPerformance));
        for (int page = 0; page < 4; ++page)
        {
            const auto &expected = window_.project().staffPages[page];
            const auto &actual = loaded.staffPages[page];
            check("JPP restores each source image, rendered image and page interval",
                  actual.sourceImage == expected.sourceImage && actual.renderedImage == expected.renderedImage &&
                      actual.startTick == expected.startTick && actual.endTick == expected.endTick &&
                      actual.label == expected.label);
        }
        check("Saved hand instruments remain 40 and 35",
              loaded.staffPerformance->primaryProgram == 40 && loaded.staffPerformance->otherProgram == 35);
        check("The manually calibrated BPM persists in the self-contained JPP", loaded.score.bpm == 62);
    }

    void testManualPages()
    {
        auto *native = window_.findChild<QGraphicsView *>("scoreView");
        check("The main score view is available", native != nullptr);
        auto *view = static_cast<ScoreView *>(native);
        for (int page = 0; page < 4; ++page)
        {
            window_.setStaffPage(page);
            check("Manual page navigation displays the correct raster",
                  window_.staffPageIndex() == page &&
                      showsPixels(view, window_.project().staffPages[page].renderedImage));
            const auto &notes = window_.project().score.notes;
            const auto matching = std::find_if(notes.begin(), notes.end(), [page](const auto &note)
                                               { return note.pageIndex == page && note.hasImageAnchor; });
            const auto foreign = std::find_if(notes.begin(), notes.end(),
                                              [page](const auto &note) { return note.pageIndex != page; });
            check("Each rendered page has a corresponding guide interval",
                  matching != notes.end() && foreign != notes.end());
            view->setCurrent(int(std::distance(notes.begin(), matching)), false);
            QGraphicsRectItem *cursor = nullptr;
            for (auto *item : view->scene()->items())
                if (item->zValue() == 10)
                    cursor = qgraphicsitem_cast<QGraphicsRectItem *>(item);
            const auto &box = matching->source;
            check("A same-page guide cursor uses its own page coordinates",
                  cursor && cursor->isVisible() &&
                      cursor->rect() == QRectF(box.x - 4, box.y - 6, box.width + 8, box.height + 12));
            view->setCurrent(int(std::distance(notes.begin(), foreign)), false);
            check("A guide bounding box from another page is not drawn on this page",
                  cursor && !cursor->isVisible());
            capture(window_, QString("workflow-main-page-%1").arg(page + 1));
        }
    }

    std::int64_t firstOccurrence(int page) const
    {
        const auto source = window_.project().staffPages[page].startTick;
        std::int64_t start = 0;
        for (std::size_t guide = 0; guide < window_.project().score.notes.size(); ++guide)
        {
            const auto end = start + window_.project().score.notes[guide].durationTicks;
            if (source >= start && source < end)
            {
                const auto event =
                    std::find_if(window_.timeline().events.begin(), window_.timeline().events.end(),
                                 [guide](const auto &event) { return event.sourceNoteIndex == guide; });
                if (event != window_.timeline().events.end())
                    return event->startTick + source - start;
                break;
            }
            start = end;
        }
        throw std::runtime_error("No performed occurrence exists for the source page start");
    }

    void startAutomaticPage(int page)
    {
        auto &player = window_.player();
        player.pause();
        check("System MIDI is selected without another audio backend fallback",
              player.setAudioBackend(AudioBackend::WindowsMidi));
        auto *autoPage = window_.findChild<QCheckBox *>("staffAutoPage");
        check("Automatic page turning is available", autoPage != nullptr);
        autoPage->setChecked(true);
        window_.setStaffPage((page + 1) % 4);
        player.seek(firstOccurrence(page));
        check("Actual system-MIDI transport starts", player.play());
        automaticPage_ = page;
        elapsed_.restart();
        phase_ = Phase::Playback;
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
                if (!recognitionStarted_)
                {
                    advancing_ = false;
                    return;
                }
                if (!protectedProject())
                    check("The previous project is protected throughout real recognition", false);
                if (!noCloudOptions())
                    check("Real local recognition never asks for cloud configuration", false);
                if (elapsed_.elapsed() > 600000)
                    throw std::runtime_error("Real multi-page recognition exceeded 600 seconds");
                if (window_.localStaffRecognitionTask().state() == LocalState::Failed)
                    throw std::runtime_error(window_.localStaffRecognitionTask().errorString().toStdString());
                if (!window_.localStaffRecognitionTask().isRunning())
                    ready();
            }
            else if (phase_ == Phase::Playback)
            {
                if (elapsed_.elapsed() > 5000)
                    throw std::runtime_error("Automatic source-page turning did not follow the transport");
                if (window_.staffPageIndex() == automaticPage_)
                {
                    window_.player().pause();
                    check("The UI follows the performed first occurrence of each source page", true);
                    check("Page changes preserve both hand programs",
                          window_.project().staffPerformance->primaryProgram == 40 &&
                              window_.project().staffPerformance->otherProgram == 35);
                    if (++automaticPage_ < 4)
                        startAutomaticPage(automaticPage_);
                    else
                    {
                        for (int source = 0; source < originals_.size(); ++source)
                            check("Actual user source images remain byte-identical",
                                  QCryptographicHash::hash(bytesFrom(originals_[source]),
                                                           QCryptographicHash::Sha256) == originalHashes_[source]);
                        finish(true);
                    }
                }
            }
        }
        catch (const std::exception &error)
        {
            finish(false, QString::fromUtf8(error.what()));
        }
        advancing_ = false;
    }

    void finish(bool passed, const QString &error = {})
    {
        if (phase_ == Phase::Finished)
            return;
        phase_ = Phase::Finished;
        timer_.stop();
        modalTimer_.stop();
        window_.player().stop();
        window_.localStaffRecognitionTask().cancel();
        if (!metrics_.contains("engineLog"))
            metrics_.insert("engineLog", window_.localStaffRecognitionTask().engineLog());
        const QJsonObject report{{"passed", passed},
                                 {"checks", checks_},
                                 {"metrics", metrics_},
                                 {"artifacts", artifacts_},
                                 {"error", error},
                                 {"realLocalWorkflow", true},
                                 {"recognitionAccuracyVerified", false}};
        const auto bytes = QJsonDocument(report).toJson();
        QTextStream(stdout) << bytes;
        QFile file(reportPath_);
        const bool saved = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        app_.exit(saved ? (passed ? 0 : 3) : 4);
    }

    MainWindow &window_;
    QApplication &app_;
    QString inputFolder_;
    QString reportPath_;
    QString folder_;
    QString ownedInputs_;
    QStringList originals_;
    QStringList copies_;
    std::vector<QByteArray> originalHashes_;
    std::vector<StaffPageInput> expectedInputs_;
    QTimer timer_;
    QTimer modalTimer_;
    QElapsedTimer elapsed_;
    QJsonArray checks_;
    QJsonArray artifacts_;
    QJsonObject metrics_;
    QJsonObject protectedScore_;
    QImage protectedImage_;
    Phase phase_ = Phase::Starting;
    int automaticPage_ = 0;
    bool ordered_ = false;
    bool recognitionStarted_ = false;
    bool allowApply_ = false;
    bool appliedConfirmation_ = false;
    bool allowTempoCalibration_ = false;
    bool tempoCalibrationConfirmed_ = false;
    bool advancing_ = false;
};
} // namespace

void runMultiPageWorkflowCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    new MultiPageWorkflowProbe(window, args, app);
}
} // namespace singlilt

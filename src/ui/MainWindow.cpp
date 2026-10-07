// Score workspace, playback controls, and import coordination.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MainWindow.h"
#include "AccompanimentPanel.h"
#include "ClassroomDialog.h"
#include "NotationRenderer.h"
#include "PracticeScore.h"
#include "RecognitionPreviewDialog.h"
#include "ScoreView.h"
#include "StaffPageImportDialog.h"
#include "StaffRenderer.h"
#include "i18n/LanguageManager.h"
#include "recognition/LocalStaffRecognizer.h"
#include "recognition/StaffPageSplitter.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QEvent>
#include <QException>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>

namespace singlilt
{
namespace
{
QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}
QString clockText(double seconds)
{
    int s = std::max(0, int(seconds));
    return QString(trText("ui.format.clock")).arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

} // namespace

void MainWindow::bindText(QObject *object, const char *key, const char *property)
{
    const QByteArray binding = QByteArray("_ui_") + property;
    object->setProperty(binding.constData(), QByteArray(key));
    object->setProperty(property, trText(key));
}
QLabel *MainWindow::label(const char *key)
{
    auto *result = new QLabel;
    bindText(result, key);
    return result;
}
QPushButton *MainWindow::button(const char *key, QBoxLayout *layout)
{
    auto *result = new QPushButton;
    bindText(result, key);
    result->setCursor(Qt::PointingHandCursor);
    layout->addWidget(result);
    return result;
}
QCheckBox *MainWindow::checkBox(const char *key)
{
    auto *result = new QCheckBox;
    bindText(result, key);
    return result;
}
void MainWindow::addTranslatedItem(QComboBox *combo, const char *key, const QVariant &value,
                                   const QStringList &arguments)
{
    QString text = trText(key);
    for (const auto &argument : arguments)
        text = text.arg(argument);
    combo->addItem(text, value);
    combo->setItemData(combo->count() - 1, QByteArray(key), Qt::UserRole + 1);
    combo->setItemData(combo->count() - 1, arguments, Qt::UserRole + 2);
}
void MainWindow::setStatus(const char *key, const QStringList &arguments)
{
    statusSource_ = QString::fromUtf8(key);
    statusArguments_ = arguments;
    statusIsKey_ = true;
    QString text = trText(key);
    for (const auto &argument : arguments)
        text = text.arg(argument);
    status_->setText(text);
}
void MainWindow::setStatusMessage(const QString &message)
{
    statusSource_ = message;
    statusArguments_.clear();
    statusIsKey_ = false;
    status_->setText(localizeMessage(message));
}
void MainWindow::refreshIssues()
{
    QStringList messages;
    for (const auto &warning : project_.warnings)
        messages.append(localizeMessage(warning));
    for (const auto &diagnostic : timeline_.diagnostics)
        messages.append(localizeMessage(qs(diagnostic.message)));
    if (project_.accompaniment)
    {
        for (const auto &diagnostic : project_.accompaniment->diagnostics)
            messages.append(localizeMessage(qs(diagnostic.message)));
        for (const auto &diagnostic : validateAccompaniment(project_.score, *project_.accompaniment))
            messages.append(localizeMessage(qs(diagnostic.message)));
    }
    messages.removeDuplicates();
    issues_->setPlainText(messages.isEmpty() ? trText("ui.validation.ok") : messages.join("\n\n"));
}
void MainWindow::refreshPlaybackText()
{
    play_->setText(trText(playbackActive_ ? "ui.transport.pause" : "ui.transport.play"));
    if (playingNote_ < 0 || playingNote_ >= int(project_.score.notes.size()))
    {
        lyric_->setText(trText("ui.transport.ready"));
        verseStatus_->clear();
        return;
    }
    const QString verse = playingVerse_ == 0   ? trText("ui.transport.part_a")
                          : playingVerse_ == 1 ? trText("ui.transport.part_b")
                                               : trText("ui.transport.pass").arg(playingVerse_ + 1);
    const int item = programA_->findData(playingProgram_);
    const QString instrument =
        item >= 0 ? programA_->itemText(item) : trText("ui.instrument.gm").arg(playingProgram_ + 1);
    verseStatus_->setText(trText("ui.format.verse_instrument").arg(verse, instrument));
    const auto &note = project_.score.notes[std::size_t(playingNote_)];
    QString text = playingLyric_; // Imported lyrics never pass through translation.
    if (text.isEmpty())
        text = note.degree == 0 ? trText("ui.transport.rest")
                                : trText("ui.format.syllable")
                                      .arg(note.degree)
                                      .arg(trText(qPrintable(QString("ui.note.syllable.%1").arg(note.degree))));
    lyric_->setText(text);
}
void MainWindow::retranslateUi()
{
    if (!languageSelector_ || !status_)
        return;
    // Updating labels must never emit score-edit or seek signals. In particular,
    // do not query PlaybackEngine here: its load worker may hold the audio lock.
    std::vector<std::unique_ptr<QSignalBlocker>> blockers;
    auto objects = findChildren<QObject *>();
    objects.prepend(this);
    for (auto *object : objects)
        if (qobject_cast<QComboBox *>(object))
            blockers.push_back(std::make_unique<QSignalBlocker>(object));
    for (auto *object : objects)
    {
        for (const auto &property : object->dynamicPropertyNames())
            if (property.startsWith("_ui_"))
                object->setProperty(property.mid(4).constData(),
                                    trText(object->property(property.constData()).toByteArray().constData()));
        if (auto *combo = qobject_cast<QComboBox *>(object))
            for (int i = 0; i < combo->count(); ++i)
            {
                const auto key = combo->itemData(i, Qt::UserRole + 1).toByteArray();
                if (key.isEmpty())
                    continue;
                QString text = trText(key.constData());
                for (const auto &argument : combo->itemData(i, Qt::UserRole + 2).toStringList())
                    text = text.arg(argument);
                combo->setItemText(i, text);
            }
        if (auto *table = qobject_cast<QTableWidget *>(object))
        {
            QStringList translated;
            for (const auto &key : table->property("translationHeaders").toStringList())
                translated.append(trText(qPrintable(key)));
            if (!translated.isEmpty())
                table->setHorizontalHeaderLabels(translated);
        }
        if (auto *dialog = qobject_cast<QFileDialog *>(object))
        {
            const bool saving = dialog->property("fileDialogSaving").toBool();
            const bool projectOnly = dialog->property("projectOnly").toBool();
            const int filterIndex = dialog->nameFilters().indexOf(dialog->selectedNameFilter());
            const auto filters =
                trText((saving || projectOnly) ? "ui.dialog.project_filter" : "ui.dialog.open_filter").split(";;");
            dialog->setNameFilters(filters);
            if (filterIndex >= 0 && filterIndex < filters.size())
                dialog->selectNameFilter(filters[filterIndex]);
            dialog->setLabelText(QFileDialog::Accept, trText(saving ? "ui.button.save" : "ui.button.open"));
            dialog->setLabelText(QFileDialog::Reject, trText("ui.button.cancel"));
            dialog->setLabelText(QFileDialog::LookIn, trText("ui.file.look_in"));
            dialog->setLabelText(QFileDialog::FileName, trText("ui.file.name"));
            dialog->setLabelText(QFileDialog::FileType, trText("ui.file.type"));
        }
        if (auto *message = qobject_cast<QMessageBox *>(object))
            if (message->property("messageSource").isValid())
                message->setText(localizeMessage(message->property("messageSource").toString()));
    }
    // Toolbar combos must not retain widths cached for the previous language.
    for (auto *combo : {verseView_, programA_, programB_, audioSource_, languageSelector_})
        combo->updateGeometry();
    if (accompanimentPattern_)
    {
        int width = 0;
        for (int i = 0; i < accompanimentPattern_->count(); ++i)
            width = std::max(
                width, accompanimentPattern_->fontMetrics().horizontalAdvance(accompanimentPattern_->itemText(i)));
        accompanimentPattern_->setMinimumWidth(width + 40);
    }
    languageSelector_->setCurrentIndex(languageSelector_->findData(languageManager_.language()));
    setWindowTitle(trText("ui.window.title").arg(QApplication::applicationVersion()));
    subtitle_->setText(trText("ui.subtitle.score").arg(project_.score.notes.size()));
    noteTitle_->setText(selected_ >= 0
                            ? trText("ui.inspector.note").arg(selected_ + 1).arg(project_.score.notes.size())
                            : trText("ui.inspector.title"));
    refreshStaffNoteInspector();
    refreshIssues();
    refreshPlaybackText();
    refreshNotationControls();
    QString status = statusIsKey_ ? trText(qPrintable(statusSource_)) : localizeMessage(statusSource_);
    for (const auto &argument : statusArguments_)
        status = status.arg(argument);
    status_->setText(status);
    refreshCloudTaskUi();
    refreshPracticeControls();
    refreshAudioTaskUi();
    refreshProjectIdentity();
    refreshReviewStatus();
    refreshRecentProjects();
    if (accompanimentPanel_)
        accompanimentPanel_->retranslateUi();
    refreshStaffPageControls();
}
void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
}
QString MainWindow::chooseFile(bool saving, bool projectOnly)
{
    QFileDialog dialog(this);
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setProperty("fileDialogSaving", saving);
    dialog.setProperty("projectOnly", projectOnly);
    bindText(&dialog,
             saving ? "ui.dialog.save_title" : (projectOnly ? "ui.menu.open_project" : "ui.dialog.open_title"),
             "windowTitle");
    dialog.setAcceptMode(saving ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(saving ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    dialog.setNameFilters(
        trText((saving || projectOnly) ? "ui.dialog.project_filter" : "ui.dialog.open_filter").split(";;"));
    dialog.setLabelText(QFileDialog::Accept, trText(saving ? "ui.button.save" : "ui.button.open"));
    dialog.setLabelText(QFileDialog::Reject, trText("ui.button.cancel"));
    dialog.setLabelText(QFileDialog::LookIn, trText("ui.file.look_in"));
    dialog.setLabelText(QFileDialog::FileName, trText("ui.file.name"));
    dialog.setLabelText(QFileDialog::FileType, trText("ui.file.type"));
    if (saving)
    {
        dialog.setDefaultSuffix("jpp");
        dialog.selectFile(qs(project_.score.title) + ".jpp");
    }
    return dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty() ? dialog.selectedFiles().front()
                                                                                   : QString();
}
void MainWindow::showMessage(const QString &text, const char *titleKey, bool warning)
{
    QMessageBox message(warning ? QMessageBox::Warning : QMessageBox::Information, {}, {}, QMessageBox::Ok, this);
    bindText(&message, titleKey, "windowTitle");
    message.setProperty("messageSource", text);
    message.setText(localizeMessage(text));
    bindText(message.button(QMessageBox::Ok), "ui.button.ok");
    message.exec();
}

MainWindow::MainWindow(LanguageManager &languageManager, ThemeManager &themes, QWidget *parent)
    : QMainWindow(parent), languageManager_(languageManager), themes_(themes)
{
    setWindowTitle(trText("ui.window.title").arg(QApplication::applicationVersion()));
    resize(1320, 940);
    setMinimumSize(980, 700);
    setAcceptDrops(true);
    QSettings settings;
    settings_ = loadAppSettings();
    vision_ = settings_.vision;
    materialUndo_.setUndoLimit(100);
    staffRecoverySession_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    staffRecoveryTimer_ = new QTimer(this);
    staffRecoveryTimer_->setSingleShot(true);
    connect(staffRecoveryTimer_, &QTimer::timeout, this, [this] { saveStaffEditRecoverySnapshot(); });
    createUi();
    connect(&materialUndo_, &QUndoStack::indexChanged, this,
            [this]
            {
                refreshProjectIdentity();
                scheduleStaffEditRecovery();
            });
    metronome_->setChecked(settings_.metronome);
    view_->showUncertain(settings_.showMarkers);
    const QString gmPath = settings_.gmSoundFontPath;
    if (!gmPath.isEmpty() && !player_.setGmSoundFontPath(gmPath))
        setStatusMessage(player_.errorString());
    cloudTask_.stateChanged = [this]
    {
        refreshCloudTaskUi();
        if (cloudTask_.state() == CloudRecognitionTask::State::Ready ||
            cloudTask_.state() == CloudRecognitionTask::State::Failed)
            QApplication::alert(this, 3000);
    };
    refreshCloudTaskUi();
    audioSource_->setCurrentIndex(settings_.audioBackend == 1 ? 1 : 0);
    localStaffTask_.stateChanged = [this]
    {
        refreshCloudTaskUi();
        using State = LocalStaffRecognitionTask::State;
        if (localStaffTask_.state() == State::Ready)
        {
            if (localStaffPreview_ && localStaffPreview_->property("rawLocalStaffPreview").toBool())
            {
                localStaffPreview_->close();
                previewLocalStaffResult();
            }
            QApplication::alert(this, 3000);
        }
        else if (localStaffTask_.state() == State::Failed)
            QApplication::alert(this, 3000);
    };
    QObject::connect(&audioWatcher_, &QFutureWatcher<bool>::finished, this,
                     [this]
                     {
                         audioLoading_ = false;
                         centralWidget()->setEnabled(!busy_);
                         refreshCloudTaskUi();
                         try
                         {
                             playIntent_ = audioWatcher_.result();
                             if (originalAudioMode())
                             {
                                 player_.pause();
                                 playIntent_ = originalAudio_.isPlaying();
                             }
                         }
                         catch (...)
                         {
                             playIntent_ = false;
                         }
                         if (!playIntent_)
                             showError(player_.errorString().isEmpty() ? QStringLiteral("ui.error.audio_load")
                                                                       : player_.errorString());
                         else
                             setStatusMessage(player_.deviceName());
                         updatePlayback();
                     });
    QObject::connect(&watcher_, &QFutureWatcher<RecognitionResult>::finished, this,
                     [this]
                     {
                         busy_ = false;
                         centralWidget()->setEnabled(true);
                         import_->setEnabled(true);
                         refreshCloudTaskUi();
                         try
                         {
                             auto result = watcher_.result();
                             if (result.score.notes.empty())
                                 throw std::runtime_error("ui.error.no_notes");
                             setProject({result.score, pendingImage_, result.warnings}, true);
                             debugText_ = result.debugText;
                             setStatus("ui.status.recognized", {QString::number(project_.score.notes.size())});
                         }
                         catch (const QUnhandledException &e)
                         {
                             if (const auto original = e.exception())
                             {
                                 try
                                 {
                                     std::rethrow_exception(original);
                                 }
                                 catch (const std::exception &cause)
                                 {
                                     showError(QString::fromUtf8(cause.what()));
                                 }
                                 catch (...)
                                 {
                                     showError(QStringLiteral("ui.error.recognition"));
                                 }
                             }
                             else
                             {
                                 showError(QStringLiteral("ui.error.recognition"));
                             }
                         }
                         catch (const std::exception &e)
                         {
                             showError(QString::fromUtf8(e.what()));
                         }
                         catch (...)
                         {
                             showError(QStringLiteral("ui.error.recognition"));
                         }
                     });
    auto *timer = new QTimer(this);
    QObject::connect(timer, &QTimer::timeout, this, [this] { updatePlayback(); });
    timer->start(25);
    auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    QObject::connect(space, &QShortcut::activated, play_, &QPushButton::click);
    {
        try
        {
            setProject(makePracticeScore());
        }
        catch (const std::exception &error)
        {
            Project empty;
            empty.image = QImage(640, 360, QImage::Format_RGB32);
            empty.image.fill(Qt::white);
            setProject(std::move(empty));
            setStatusMessage(QString::fromUtf8(error.what()));
        }
    }
}
MainWindow::~MainWindow()
{
    if (staffRecoveryPending_)
    {
        staffRecoveryTimer_->stop();
        saveStaffEditRecoverySnapshot();
    }
    if (classroom_)
        delete classroom_.data();
    audioTask_.stateChanged = {};
    audioTask_.progress = {};
    audioTask_.cancel();
    restorePendingOriginalSource_ = {};
    originalAudio_.close();
    if (accompanimentPanel_)
        accompanimentPanel_->reject();
    cloudTask_.stateChanged = {};
    cloudTask_.cancel();
    localStaffTask_.stateChanged = {};
    localStaffTask_.cancel();
    audioWatcher_.waitForFinished();
    player_.stop();
    watcher_.waitForFinished();
}
void MainWindow::setProject(Project project, bool modified)
{
    if (staffRecoveryPending_)
    {
        staffRecoveryTimer_->stop();
        if (!saveStaffEditRecoverySnapshot())
            throw std::runtime_error(status_->text().toStdString());
    }
    staffRecoverySession_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    staffRecoveryPath_.clear();
    if (project.staffPerformance && project.processing.value("local").toBool() &&
        (project.processing.contains("engine") || project.processing.contains("musicSource")) &&
        !project.staffImagePlayback && !project.staffPages.empty() &&
        std::all_of(project.staffPages.begin(), project.staffPages.end(),
                    [](const auto &page) { return !page.sourceImage.isNull(); }))
    {
        project.staffImagePlayback = true;
        project.generatedNotation = false;
        for (auto &page : project.staffPages)
            page.renderedImage = page.sourceImage;
        project.image = project.staffPages.front().sourceImage;
        for (auto &note : project.score.notes)
        {
            note.hasImageAnchor = false;
            note.source = {};
        }
        for (auto &note : project.staffPerformance->notes)
        {
            note.hasImageAnchor = false;
            note.source = {};
        }
        project.processing.insert("sourceAnchorProvenance", "legacy-generated-coordinates-not-reused");
        project.warnings.append("messages.original_playback.legacy_positions");
    }
    localStaffOriginalImage_ = {};
    staffPageIndex_ = 0;
    if (modified)
    {
        project.score.baseVelocity = settings_.velocity;
        project.score.accentBeats = settings_.accentBeats;
        project.score.versePrograms = {settings_.programA, settings_.programB};
        project.practiceMix.melodyVolume = settings_.melodyVolume;
        project.practiceMix.accompanimentVolume = settings_.accompanimentVolume;
    }
    if (accompanimentPanel_)
    {
        accompanimentPanel_->reject();
        accompanimentPanel_.clear();
    }
    auditionArrangement_.reset();
    preAuditionMix_.reset();
    preAuditionPlaybackSource_.reset();
    playIntent_ = false;
    player_.stop();
    restorePendingOriginalSource_ = {};
    originalAudio_.close();
    if (playbackSource_)
    {
        QSignalBlocker blocker(playbackSource_);
        playbackSource_->setCurrentIndex(0);
        previousOriginalSource_ = 0;
    }
    loading_ = true;
    project_ = std::move(project);
    projectPath_.clear();
    debugText_.clear();
    materialUndo_.clear();
    nonMaterialDirty_ = modified;
    dirty_ = modified;
    selected_ = -1;
    selectedStaffNote_ = -1;
    playingNote_ = -1;
    playingVerse_ = -1;
    loopStart_ = loopEnd_ = 0;
    loopEditRevision_ = 0;
    loop_->setChecked(false);
    title_->setText(qs(project_.score.title));
    title_->setToolTip(qs(project_.score.title));
    key_->setCurrentIndex(project_.score.tonic);
    tempo_->setValue(project_.score.bpm);
    meterTop_->setValue(project_.score.beatsPerBar);
    meterBottom_->setCurrentIndex(meterBottom_->findData(project_.score.beatUnit));
    velocity_->setValue(project_.score.baseVelocity);
    accentBeats_->setChecked(project_.score.accentBeats);
    auto setInstrument = [this](QComboBox *combo, int program)
    {
        int index = combo->findData(program);
        if (index < 0)
        {
            addTranslatedItem(combo, "ui.instrument.gm", program, {QString::number(program + 1)});
            index = combo->count() - 1;
        }
        combo->setCurrentIndex(index);
    };
    setInstrument(programA_, project_.staffPerformance ? project_.staffPerformance->primaryProgram
                                                       : programForVerse(project_.score, 0));
    setInstrument(programB_, project_.staffPerformance ? project_.staffPerformance->otherProgram
                                                       : programForVerse(project_.score, 1));
    verseView_->setCurrentIndex(verseView_->findData(0));
    refreshPracticeControls();
    loading_ = false;
    rebuild(false);
    if (project_.staffImagePlayback)
        staffScoreTabs_->setCurrentIndex(0);
    else if (!project_.staffPages.empty() && !project_.staffPages.front().sourceImage.isNull())
        staffScoreTabs_->setCurrentIndex(1);
    restoreProjectPractice();
    displayedScoreView()->fitWidth();
    if (!project_.score.notes.empty())
        selectNote(0, false);
    QTimer::singleShot(0, view_, [this] { displayedScoreView()->fitWidth(); });
    refreshNotationControls();
    refreshProjectIdentity();
    refreshReviewStatus();
    refreshStaffPageControls();
    updatePlayback();
}
void MainWindow::rebuild(bool preserve)
{
    auto pos = preserve ? practicePositionTick() : 0;
    bool wasPlaying = preserve && player_.isPlaying();
    player_.pause();
    if (project_.generatedNotation && !project_.staffImagePlayback && !project_.score.notes.empty())
    {
        try
        {
            if (project_.staffPerformance)
            {
                if (!project_.staffPages.empty())
                {
                    auto rendered = renderGrandStaffPages(
                        project_.score, *project_.staffPerformance,
                        {project_.staffBassClef, project_.staffKeyFifths, project_.staffMinor});
                    if (rendered.size() != project_.staffPages.size())
                        throw std::runtime_error(trText("messages.pages.invalid_project").toStdString());
                    for (std::size_t index = 0; index < rendered.size(); ++index)
                        project_.staffPages[index].renderedImage = std::move(rendered[index]);
                    project_.image = project_.staffPages.front().renderedImage;
                }
                else
                    project_.image = renderGrandStaffScore(
                        project_.score, *project_.staffPerformance,
                        {project_.staffBassClef, project_.staffKeyFifths, project_.staffMinor});
            }
            else if (project_.notationStyle == NotationStyle::Staff)
                project_.image = renderStaffScore(
                    project_.score, {project_.staffBassClef, project_.staffKeyFifths, project_.staffMinor});
            else
                project_.image = renderNumberedScore(project_.score);
        }
        catch (const std::exception &error)
        {
            setStatusMessage(QString::fromUtf8(error.what()));
        }
    }
    timeline_ = buildTimeline(project_.score);
    loopEnd_ = std::min(loopEnd_, timeline_.durationTicks);
    loopStart_ = std::min(loopStart_, timeline_.durationTicks);
    if (loopStart_ >= loopEnd_)
    {
        loopStart_ = 0;
        loopEnd_ = timeline_.durationTicks;
    }
    refreshIssues();
    refreshReviewStatus();
    refreshNotationControls();
    refreshProjectIdentity();
    view_->setScore(project_.image, project_.score, 0, project_.staffImagePlayback);
    view_->setStaffPerformance(project_.staffPerformance ? &*project_.staffPerformance : nullptr);
    refreshStaffAnchorEditing();
    refreshStaffPageControls();
    if (!project_.staffPages.empty())
        setStaffPage(staffPageIndex_, false);
    position_->setRange(0, int(std::min<int64_t>(timeline_.durationTicks, INT_MAX)));
    playingNote_ = -1;
    playingVerse_ = -1;
    if (accompanimentPanel_)
        accompanimentPanel_->setSourceFingerprint(accompanimentFingerprint(project_.score));
    const auto &arrangement = auditionArrangement_ ? auditionArrangement_ : project_.accompaniment;
    auto settings = arrangement ? arrangement->settings : AccompanimentSettings{};
    if (project_.staffPerformance)
    {
        settings = {};
        settings.originalStaff = true;
        settings.chordProgram = project_.staffPerformance->primaryProgram;
        settings.bassProgram = project_.staffPerformance->otherProgram;
    }
    bool ok = player_.load(project_.score, timeline_, activeAccompanimentPlan(), settings);
    auto mix = auditionArrangement_ ? auditionMix_ : project_.practiceMix;
    if (!project_.staffPerformance &&
        (!arrangement || arrangement->melodyFingerprint != accompanimentFingerprint(project_.score)))
        mix.accompanimentEnabled = false;
    if (ok)
        ok = player_.setPracticeMix(mix);
    refreshPracticeControls();
    const bool tempoPending =
        project_.staffPerformance && project_.processing.value("tempoNeedsConfirmation").toBool();
    play_->setEnabled(ok && !project_.score.notes.empty() && !tempoPending);
    if (!ok)
        setStatusMessage(player_.errorString());
    player_.seek(pos);
    playIntent_ = wasPlaying && ok && !tempoPending;
    if (playIntent_ && !player_.play())
    {
        playIntent_ = false;
        setStatusMessage(player_.errorString());
    }
}
void MainWindow::selectNote(int index, bool seek)
{
    if (index < 0 || index >= int(project_.score.notes.size()))
    {
        selected_ = -1;
        return;
    }
    if (seek && !project_.staffPages.empty() &&
        project_.score.notes[std::size_t(index)].pageIndex != staffPageIndex_)
        setStaffPage(project_.score.notes[std::size_t(index)].pageIndex);
    if (selected_ != index && !loadingNote_ && !resolveNoteDraft())
    {
        view_->setCurrent(selected_, false);
        return;
    }
    selected_ = index;
    const auto &n = project_.score.notes[size_t(index)];
    noteTitle_->setText(QString(trText("ui.inspector.note")).arg(index + 1).arg(project_.score.notes.size()));
    degree_->setCurrentIndex(degree_->findData(n.degree));
    octave_->setValue(n.octave);
    accidental_->setValue(n.accidental);
    duration_->setValue(n.durationTicks / 480.0);
    noteKey_->setCurrentIndex(noteKey_->findData(n.keyOverride));
    const auto verses = lyricVerses(n);
    lyricEdit_->setText(verses.empty() ? QString() : qs(verses[0]));
    lyricBEdit_->setText(verses.size() > 1 ? qs(verses[1]) : QString());
    sharedLyric_->setChecked(verses.size() <= 1);
    lyricBEdit_->setDisabled(sharedLyric_->isChecked());
    tie_->setChecked(n.tieToNext);
    view_->setCurrent(index, false);
    const bool editable = !project_.staffPerformance;
    for (QWidget *field : std::initializer_list<QWidget *>{degree_, octave_, accidental_, duration_, noteKey_,
                                                           lyricEdit_, lyricBEdit_, sharedLyric_, tie_})
        field->setEnabled(editable);
    if (auto *apply = findChild<QPushButton *>("applyNoteChanges"))
        apply->setEnabled(editable);
    if (auto *remove = findChild<QPushButton *>("removeNote"))
        remove->setEnabled(editable);
    if (seek)
    {
        const int verse = verseView_->currentData().toInt();
        auto it = std::find_if(timeline_.events.begin(), timeline_.events.end(), [index, verse](const auto &e)
                               { return e.sourceNoteIndex == size_t(index) && e.verseIndex == verse; });
        if (it == timeline_.events.end())
            it = std::find_if(timeline_.events.begin(), timeline_.events.end(),
                              [index](const auto &e) { return e.sourceNoteIndex == size_t(index); });
        if (it != timeline_.events.end())
        {
            seekPracticeTick(it->startTick);
            if ((!player_.isPlaying() || !player_.practiceMix().melodyEnabled) && !originalAudio_.isPlaying())
            {
                const int pitch = it->midiPitch < 0 ? -1 : it->midiPitch + transpose_->value();
                if (player_.previewNote(pitch, it->program, project_.score.baseVelocity))
                {
                    notePreviewLoading_ = true;
                    centralWidget()->setEnabled(false);
                    setStatus("ui.classroom.loading_audio");
                }
            }
        }
    }
}
void MainWindow::updateNote()
{
    if (project_.staffPerformance)
    {
        setStatus("ui.staff.guide_only");
        return;
    }
    if (selected_ < 0 || (!hasNoteDraft() && project_.score.notes[size_t(selected_)].confidence >= 1))
        return;
    const auto before = materialState();
    auto &n = project_.score.notes[size_t(selected_)];
    n.degree = degree_->currentData().toInt();
    n.octave = octave_->value();
    n.accidental = accidental_->value();
    n.durationTicks = int(std::lround(duration_->value() * 480));
    n.keyOverride = noteKey_->currentData().toInt();
    auto verses = lyricVerses(n);
    if (sharedLyric_->isChecked())
        verses = {lyricEdit_->text().toStdString()};
    else
    {
        verses.resize(std::max<std::size_t>(2, verses.size()));
        verses[0] = lyricEdit_->text().toStdString();
        verses[1] = lyricBEdit_->text().toStdString();
    }
    n.verseLyrics = std::move(verses);
    QStringList legacyLines;
    for (const auto &verse : n.verseLyrics)
        legacyLines.append(qs(verse));
    n.lyric = legacyLines.join('\n').toStdString();
    n.tieToNext = tie_->isChecked();
    n.confidence = 1;
    commitMaterialEdit(before, "ui.product.update_note");
    rebuild(true);
    selectNote(selected_, false);
}
void MainWindow::updatePlayback()
{
    if (originalAudio_.isLoading())
        return;
    if (player_.isPreviewLoading())
        return;
    if (notePreviewLoading_)
    {
        notePreviewLoading_ = false;
        centralWidget()->setEnabled(!busy_ && !audioLoading_);
        setStatusMessage(player_.errorString().isEmpty() ? player_.deviceName() : player_.errorString());
    }
    if (audioLoading_)
        return;
    if (originalAudioMode())
    {
        updateOriginalPlayback();
        return;
    }
    auto tick = player_.positionTicks();
    bool active = player_.isPlaying();
    if (project_.staffPerformance)
    {
        const auto event = timeline_.eventIndexAtTick(tick);
        std::int64_t sourceTick = 0;
        if (event)
        {
            const auto &current = timeline_.events[*event];
            for (std::size_t index = 0; index < current.sourceNoteIndex; ++index)
                sourceTick += project_.score.notes[index].durationTicks;
            sourceTick += tick - current.startTick;
        }
        if (active && staffAutoPage_ && staffAutoPage_->isChecked() && !project_.staffPages.empty())
            for (std::size_t page = 0; page < project_.staffPages.size(); ++page)
                if (sourceTick >= project_.staffPages[page].startTick &&
                    sourceTick < project_.staffPages[page].endTick && int(page) != staffPageIndex_)
                {
                    setStaffPage(int(page));
                    break;
                }
        view_->setStaffCurrent(sourceTick, active, project_.practiceMix.melodyEnabled,
                               project_.practiceMix.accompanimentEnabled, project_.staffImagePlayback && active);
    }
    const auto audioError = player_.errorString();
    if (!audioError.isEmpty() && audioError != lastAudioError_)
    {
        setStatusMessage(audioError);
        playIntent_ = false;
    }
    lastAudioError_ = audioError;
    if (playIntent_ && loop_->isChecked() && loopEnd_ > loopStart_ && tick >= loopEnd_)
    {
        player_.seek(loopStart_);
        if (!active)
            playIntent_ = player_.play();
        tick = player_.positionTicks();
        active = player_.isPlaying();
    }
    if (!active && tick >= timeline_.durationTicks)
        playIntent_ = false;
    if (active != playbackActive_ && project_.staffImagePlayback)
        refreshStaffAnchorEditing();
    playbackActive_ = active;
    play_->setText(trText(active ? "ui.transport.pause" : "ui.transport.play"));
    if (!position_->isSliderDown())
        position_->setValue(int(tick));
    time_->setText(trText("ui.format.range")
                       .arg(clockText(timeline_.secondsAtTick(tick)), clockText(timeline_.durationSeconds())));
    auto event = timeline_.eventIndexAtTick(tick);
    if (event)
    {
        const auto &occurrence = timeline_.events[*event];
        int index = int(occurrence.sourceNoteIndex);
        if (index != playingNote_ || occurrence.verseIndex != playingVerse_)
        {
            playingNote_ = index;
            playingVerse_ = occurrence.verseIndex;
            {
                QSignalBlocker blocker(verseView_);
                while (verseView_->count() <= playingVerse_)
                    addTranslatedItem(verseView_, "ui.transport.pass", verseView_->count(),
                                      {QString::number(verseView_->count() + 1)});
                verseView_->setCurrentIndex(verseView_->findData(playingVerse_));
            }
            playingProgram_ = occurrence.program;
            playingLyric_ = qs(occurrence.lyric);
            refreshPlaybackText();
        }
        // Pausing can clear the cursor without changing the current event.
        view_->setCurrent(index, active);
    }
}
void MainWindow::togglePlayback()
{
    if (originalAudio_.isLoading())
    {
        setStatus("ui.status.loading_audio");
        return;
    }
    if (busy_ || audioLoading_)
        return;
    if (correctionMode_ && project_.staffImagePlayback)
    {
        setCorrectionMode(false);
        if (correctionMode_)
            return;
    }
    if (playbackSource_ && playbackSource_->currentData().toInt() > 0)
    {
        player_.pause();
        if (!originalAudio_.isOpen())
        {
            setStatus("ui.audio_import.no_original");
            return;
        }
        playIntent_ = originalAudio_.isPlaying() ? false : true;
        const bool ok = playIntent_ ? originalAudio_.play() : originalAudio_.pause();
        if (!ok)
        {
            playIntent_ = false;
            setStatusMessage(originalAudio_.errorString());
        }
        return;
    }
    originalAudio_.pause();
    if (player_.isPlaying())
    {
        playIntent_ = false;
        player_.pause();
        return;
    }
    playIntent_ = false;
    audioLoading_ = true;
    centralWidget()->setEnabled(false);
    refreshCloudTaskUi();
    playbackActive_ = false;
    setStatus(player_.audioBackend() == AudioBackend::SampledPiano ? "ui.status.loading_piano"
                                                                   : "ui.status.loading_midi");
    audioWatcher_.setFuture(QtConcurrent::run([this] { return player_.play(); }));
}
void MainWindow::seekVerse(int verse)
{
    if (loading_ || timeline_.events.empty())
        return;
    auto it = std::find_if(
        timeline_.events.begin(), timeline_.events.end(), [this, verse](const auto &event)
        { return event.verseIndex == verse && selected_ >= 0 && event.sourceNoteIndex == size_t(selected_); });
    if (it == timeline_.events.end())
        it = std::find_if(timeline_.events.begin(), timeline_.events.end(),
                          [verse](const auto &event) { return event.verseIndex == verse; });
    if (it == timeline_.events.end())
    {
        QSignalBlocker blocker(verseView_);
        verseView_->setCurrentIndex(std::max(0, playingVerse_));
        setStatus("ui.status.no_pass");
        return;
    }
    player_.seek(it->startTick);
    selectNote(int(it->sourceNoteIndex), false);
    updatePlayback();
    view_->setCurrent(int(it->sourceNoteIndex), true);
}
void MainWindow::updateVersePrograms()
{
    if (loading_)
        return;
    if (project_.staffPerformance)
    {
        AccompanimentSettings settings;
        settings.originalStaff = true;
        settings.chordProgram = programA_->currentData().toInt();
        settings.bassProgram = programB_->currentData().toInt();
        if (!player_.setAccompanimentSettings(settings))
        {
            setStatusMessage(player_.errorString());
            refreshNotationControls();
            return;
        }
        project_.staffPerformance->primaryProgram = settings.chordProgram;
        project_.staffPerformance->otherProgram = settings.bassProgram;
        markModified();
        return;
    }
    if (project_.score.versePrograms.size() < 2)
        project_.score.versePrograms.resize(2, programForVerse(project_.score, 0));
    project_.score.versePrograms[0] = programA_->currentData().toInt();
    project_.score.versePrograms[1] = programB_->currentData().toInt();
    markModified();
    rebuild(true);
}
bool MainWindow::confirmDiscard()
{
    if (!resolveNoteDraft())
        return false;
    if (!dirty_)
        return true;
    QMessageBox message(QMessageBox::Question, {}, {},
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    bindText(&message, "ui.dialog.unsaved_title", "windowTitle");
    bindText(&message, "ui.dialog.unsaved_message");
    bindText(message.button(QMessageBox::Save), "ui.button.save");
    bindText(message.button(QMessageBox::Discard), "ui.button.discard");
    bindText(message.button(QMessageBox::Cancel), "ui.button.cancel");
    message.setDefaultButton(QMessageBox::Save);
    const auto answer = message.exec();
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
    {
        save();
        return !dirty_;
    }
    return true;
}
void MainWindow::openFile(const QString &path)
{
    if (path.endsWith(".musicxml", Qt::CaseInsensitive) || path.endsWith(".xml", Qt::CaseInsensitive))
    {
        importMusicXml(path);
        return;
    }
    if (path.endsWith(".mp3", Qt::CaseInsensitive) || path.endsWith(".wav", Qt::CaseInsensitive))
    {
        openAudioImport(path);
        return;
    }
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning() || !confirmDiscard())
        return;
    try
    {
        if (path.endsWith(".jpp", Qt::CaseInsensitive))
        {
            setProject(loadProject(path));
            projectPath_ = QFileInfo(path).absoluteFilePath();
            rememberProject(projectPath_);
            refreshProjectIdentity();
            return;
        }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        auto size = reader.size();
        if (size.width() > 12000 || size.height() > 20000 || qint64(size.width()) * size.height() > 50000000)
            throw std::runtime_error("ui.error.image_large");
        auto image = reader.read();
        if (image.isNull())
            throw std::runtime_error(reader.errorString().toStdString());
        recognize(image, path);
    }
    catch (const std::exception &e)
    {
        showError(QString::fromUtf8(e.what()));
    }
}
void MainWindow::recognize(QImage image, QString path)
{
    if (busy_ || audioLoading_)
        return;
    auto pages = splitStaffPageInputs(image, QFileInfo(path).fileName(), 0);
    if (pages.size() > 1)
    {
        StaffPageImportDialog order(std::move(pages), this);
        if (order.exec() == QDialog::Accepted)
            startLocalStaffRecognitionPages(order.orderedPages(), QFileInfo(path).fileName());
        return;
    }
    if (hasStaffLines(image))
    {
        startLocalStaffRecognition(std::move(image), std::move(path));
        return;
    }
    playIntent_ = false;
    player_.stop();
    busy_ = true;
    pendingImage_ = image;
    centralWidget()->setEnabled(false);
    import_->setEnabled(false);
    cloud_->setEnabled(false);
    refreshCloudTaskUi();
    setStatus("ui.status.recognizing_local");
    watcher_.setFuture(QtConcurrent::run([image, path] { return LocalRecognizer::recognize(image, path); }));
}
void MainWindow::configureCloud()
{
    openOptions(3);
}
void MainWindow::startCloudRecognition()
{
    if (busy_ || audioLoading_ || cloudTask_.isRunning() ||
        cloudTask_.state() == CloudRecognitionTask::State::Ready || localStaffTask_.isRunning() ||
        localStaffTask_.state() == LocalStaffRecognitionTask::State::Ready)
        return;
    if (project_.staffPages.size() > 1)
    {
        showError("ui.pages.ai_single_page");
        return;
    }
    if (project_.staffPages.size() == 1 && !project_.staffPages.front().sourceImage.isNull())
        localStaffOriginalImage_ = project_.staffPages.front().sourceImage;
    if (project_.processing.value("local").toBool() && localStaffOriginalImage_.isNull())
    {
        showError("ui.local_staff.ai_source_missing");
        return;
    }
    if (vision_.apiKey.isEmpty())
    {
        openOptions(3);
        return;
    }
    const auto sourceLabel =
        project_.score.title.empty() ? qs(project_.score.imagePath) : qs(project_.score.title);
    cloudTask_.start(localStaffOriginalImage_.isNull() ? project_.image : localStaffOriginalImage_,
                     qs(project_.score.imagePath), sourceLabel, vision_,
                     project_.notationStyle == NotationStyle::Staff ? RecognitionNotation::Staff
                                                                    : RecognitionNotation::Numbered);
}
void MainWindow::editRepeats()
{
    if (project_.staffPerformance)
    {
        setStatus("ui.staff.guide_only");
        return;
    }
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning() || !resolveNoteDraft())
        return;
    QDialog d(this);
    bindText(&d, "ui.dialog.repeat_title", "windowTitle");
    d.resize(650, 350);
    QVBoxLayout l(&d);
    QLabel hint;
    bindText(&hint, "ui.dialog.repeat_hint");
    hint.setWordWrap(true);
    l.addWidget(&hint);
    QTableWidget table(0, 4);
    table.setProperty("translationHeaders", QStringList{"ui.dialog.repeat_start", "ui.dialog.repeat_end",
                                                        "ui.dialog.repeat_count", "ui.dialog.repeat_first"});
    table.setHorizontalHeaderLabels({trText("ui.dialog.repeat_start"), trText("ui.dialog.repeat_end"),
                                     trText("ui.dialog.repeat_count"), trText("ui.dialog.repeat_first")});
    table.horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (const auto &r : project_.score.repeats)
    {
        int row = table.rowCount();
        table.insertRow(row);
        int values[] = {int(r.firstNote) + 1, int(r.endNote), r.count,
                        r.firstEndingNote < 0 ? 0 : r.firstEndingNote + 1};
        for (int c = 0; c < 4; ++c)
            table.setItem(row, c, new QTableWidgetItem(QString::number(values[c])));
    }
    l.addWidget(&table);
    QHBoxLayout actions;
    auto *add = button("ui.dialog.repeat_add", &actions);
    auto *remove = button("ui.dialog.repeat_remove", &actions);
    l.addLayout(&actions);
    QObject::connect(add, &QPushButton::clicked, &d,
                     [&]
                     {
                         int row = table.rowCount();
                         table.insertRow(row);
                         int values[] = {std::max(1, selected_ + 1), int(project_.score.notes.size()), 2, 0};
                         for (int c = 0; c < 4; ++c)
                             table.setItem(row, c, new QTableWidgetItem(QString::number(values[c])));
                     });
    QObject::connect(remove, &QPushButton::clicked, &d,
                     [&]
                     {
                         if (table.currentRow() >= 0)
                             table.removeRow(table.currentRow());
                     });
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bindText(buttons.button(QDialogButtonBox::Ok), "ui.button.ok");
    bindText(buttons.button(QDialogButtonBox::Cancel), "ui.button.cancel");
    l.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    auto copy = project_.score;
    copy.repeats.clear();
    for (int row = 0; row < table.rowCount(); ++row)
    {
        int v[4];
        for (int c = 0; c < 4; ++c)
            v[c] = table.item(row, c) ? table.item(row, c)->text().toInt() : 0;
        if (v[0] < 1 || v[1] < v[0] || v[1] > int(copy.notes.size()) || v[2] < 2 || v[2] > 8 || v[3] < 0)
        {
            showError(QStringLiteral("ui.error.repeat_range"));
            return;
        }
        copy.repeats.push_back({size_t(v[0] - 1), size_t(v[1]), v[2], v[3] - 1});
    }
    if (!buildTimeline(copy).valid())
    {
        showError(QStringLiteral("ui.error.repeat_structure"));
        return;
    }
    if (scoreToJson(copy) == scoreToJson(project_.score))
        return;
    const auto before = materialState();
    project_.score = std::move(copy);
    commitMaterialEdit(before, "ui.product.edit_repeats");
    rebuild(true);
}
void MainWindow::showError(const QString &text)
{
    setStatusMessage(text);
    showMessage(text, "ui.app.name", true);
}
void MainWindow::closeEvent(QCloseEvent *e)
{
    if (busy_ || audioLoading_)
    {
        showMessage("ui.dialog.wait", "ui.dialog.processing", false);
        e->ignore();
        return;
    }
    if (audioTask_.isRunning())
    {
        QMessageBox confirmation(QMessageBox::Question, trText("ui.audio_import.exit_title"),
                                 trText("ui.audio_import.exit_running"), QMessageBox::Yes | QMessageBox::No, this);
        confirmation.setObjectName("audioExitConfirmation");
        confirmation.setDefaultButton(QMessageBox::No);
        if (confirmation.exec() != QMessageBox::Yes)
        {
            e->ignore();
            return;
        }
    }
    if (cloudTask_.isRunning())
    {
        QMessageBox confirmation(QMessageBox::Question, trText("ui.cloud.exit_title"),
                                 trText("ui.cloud.exit_running").arg(cloudTask_.sourceLabel()),
                                 QMessageBox::Yes | QMessageBox::No, this);
        confirmation.setObjectName("cloudExitConfirmation");
        confirmation.setTextFormat(Qt::PlainText);
        bindText(confirmation.button(QMessageBox::Yes), "ui.cloud.abort_exit");
        bindText(confirmation.button(QMessageBox::No), "ui.cloud.keep_waiting");
        confirmation.setDefaultButton(QMessageBox::No);
        if (confirmation.exec() != QMessageBox::Yes)
        {
            e->ignore();
            return;
        }
    }
    if (localStaffTask_.isRunning())
    {
        QMessageBox confirmation(QMessageBox::Question, trText("ui.local_staff.exit_title"),
                                 trText("ui.local_staff.exit_running").arg(localStaffTask_.sourceLabel()),
                                 QMessageBox::Yes | QMessageBox::No, this);
        confirmation.setObjectName("localStaffExitConfirmation");
        confirmation.setDefaultButton(QMessageBox::No);
        if (confirmation.exec() != QMessageBox::Yes)
        {
            e->ignore();
            return;
        }
    }
    if (confirmDiscard())
    {
        if (staffRecoveryPending_)
        {
            staffRecoveryTimer_->stop();
            if (!saveStaffEditRecoverySnapshot())
            {
                showError(status_->text());
                e->ignore();
                return;
            }
        }
        if (classroom_ && !classroom_->close())
        {
            e->ignore();
            return;
        }
        cloudTask_.cancel();
        localStaffTask_.cancel();
        audioTask_.cancel();
        restorePendingOriginalSource_ = {};
        originalAudio_.close();
        player_.stop();
        e->accept();
    }
    else
        e->ignore();
}
void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *e)
{
    if (!e->mimeData()->urls().isEmpty())
        openFile(e->mimeData()->urls().first().toLocalFile());
}
} // namespace singlilt

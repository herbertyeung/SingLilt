// Project switching, saving, and file identity.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MainWindow.h"
#include "ScoreView.h"
#include "NewScoreDialog.h"
#include "i18n/LanguageManager.h"
#include "storage/StaffEditRecovery.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#include <QLabel>
#include <QLockFile>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
constexpr auto NumberedDraftMarker = "numberedDraft";

std::unique_ptr<QLockFile> lockNumberedDraft(const QString &path)
{
    auto lock = std::make_unique<QLockFile>(path + ".lock");
    lock->setStaleLockTime(0); // A draft can remain active in a window for hours.
    if (!lock->tryLock(0))
        return {};
    return lock;
}

bool isNumberedDraft(const Project &project)
{
    return project.generatedNotation && project.notationStyle == NotationStyle::Numbered &&
           project.score.editorMinimumBars > 0 && !project.staffPerformance && !project.audioSource;
}

QString numberedDraftDirectory()
{
    const auto overridePath = qEnvironmentVariable("SINGLILT_NUMBERED_DRAFT_DIRECTORY");
    if (QDir::isAbsolutePath(overridePath))
        return QDir::cleanPath(overridePath);
    const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return appData.isEmpty() ? QString{} : QDir(appData).filePath("numbered-drafts");
}
} // namespace
ProjectPracticeSettings MainWindow::currentProjectPractice() const
{
    ProjectPracticeSettings settings;
    settings.transpose = player_.transpose();
    settings.speed = player_.speed();
    settings.metronome = metronome_->isChecked();
    settings.originalSpeed = findChild<QDoubleSpinBox *>("originalSpeed")->value();
    settings.originalVolume = findChild<QSlider *>("originalVolume")->value() / 100.0;
    settings.playbackSource = playbackSource_->currentData().toInt();
    settings.loopEnabled = loop_->isChecked();
    settings.loopStart = loopStart_;
    settings.loopEnd = loopEnd_;
    if (settings.loopStart > settings.loopEnd)
    {
        settings.loopStart = 0;
        settings.loopEnd = timeline_.durationTicks;
    }
    return settings;
}
void MainWindow::restoreProjectPractice()
{
    ProjectPracticeSettings settings;
    settings.metronome = settings_.metronome;
    settings.originalSpeed = settings_.originalSpeed;
    settings.originalVolume = settings_.originalVolume;
    if (project_.practiceSettings)
        settings = *project_.practiceSettings;
    loading_ = true;
    transpose_->setValue(settings.transpose);
    player_.setTranspose(settings.transpose);
    player_.setSpeed(settings.speed);
    practiceSpeed_->setValue(settings.speed);
    metronome_->setChecked(settings.metronome);
    player_.setMetronome(settings.metronome);
    findChild<QDoubleSpinBox *>("originalSpeed")->setValue(settings.originalSpeed);
    findChild<QSlider *>("originalVolume")->setValue(int(std::lround(settings.originalVolume * 100)));
    loopStart_ = settings.loopStart;
    loopEnd_ = settings.loopEnd;
    loop_->setChecked(settings.loopEnabled);
    playbackSource_->setCurrentIndex(playbackSource_->findData(settings.playbackSource));
    loading_ = false;
    refreshSourceControls();
}
void MainWindow::newProject()
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning())
        return;
    NewScoreDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || !confirmDiscard())
        return;
    Project project;
    project.score = dialog.score();
    project.image = QImage(1000, 700, QImage::Format_RGB32);
    project.image.fill(Qt::white);
    project.generatedNotation = true;
    try
    {
        NumberedScoreLayout layout;
        project.image = renderNumberedScore(project.score, &layout);
        setProject(std::move(project), true, std::move(layout));
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
        return;
    }
    setCorrectionMode(true);
    setStatus("ui.editor.new_ready");
}
void MainWindow::save(bool saveAs)
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning() || !resolveNoteDraft())
        return;
    QString path = saveAs ? QString() : projectPath_;
    if (path.isEmpty())
        path = chooseFile(true);
    if (path.isEmpty())
        return;
    if (!path.endsWith(".jpp", Qt::CaseInsensitive))
        path += ".jpp";
    try
    {
        auto snapshot = project_;
        snapshot.practiceSettings = currentProjectPractice();
        saveProject(path, snapshot);
        project_.practiceSettings = snapshot.practiceSettings;
        projectPath_ = QFileInfo(path).absoluteFilePath();
        staffRecoveryTimer_->stop();
        staffRecoveryPending_ = false;
        nonMaterialDirty_ = false;
        materialUndo_.setClean();
        const bool draftCleared = clearNumberedDraft();
        refreshProjectIdentity();
        rememberProject(projectPath_);
        if (draftCleared)
            setStatus("ui.status.saved", {path});
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}
void MainWindow::scheduleNumberedDraft()
{
    if (loading_ || !isNumberedDraft(project_))
        return;
    if (!dirty_)
    {
        clearNumberedDraft();
        return;
    }
    numberedDraftTimer_->start(750);
}

void MainWindow::saveNumberedDraft()
{
    numberedDraftTimer_->stop();
    if (!dirty_ || !isNumberedDraft(project_))
        return;
    try
    {
        auto path = numberedDraftPath_;
        std::unique_ptr<QLockFile> newLock;
        if (path.isEmpty())
        {
            const auto folder = numberedDraftDirectory();
            if (folder.isEmpty() || !QDir().mkpath(folder))
                throw std::runtime_error(trText("ui.editor.draft_directory_error").toStdString());
            path = QDir(folder).filePath("numbered-draft-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                         ".jpp");
            newLock = lockNumberedDraft(path);
            if (!newLock)
                throw std::runtime_error(trText("ui.editor.draft_directory_error").toStdString());
        }
        const QFileInfo draft(path);
        if (draft.isSymLink() || (!projectPath_.isEmpty() && draft == QFileInfo(projectPath_)))
            throw std::runtime_error(trText("ui.editor.draft_directory_error").toStdString());
        auto snapshot = project_;
        snapshot.processing.insert(NumberedDraftMarker, true);
        snapshot.practiceSettings = currentProjectPractice();
        saveProject(path, snapshot);
        numberedDraftPath_ = path;
        if (newLock)
            numberedDraftLock_ = std::move(newLock);
    }
    catch (const std::exception &error)
    {
        setStatus("ui.editor.draft_save_error", {QString::fromUtf8(error.what())});
    }
}

bool MainWindow::clearNumberedDraft()
{
    numberedDraftTimer_->stop();
    if (numberedDraftPath_.isEmpty())
        return true;
    const QFileInfo draft(numberedDraftPath_);
    if (!projectPath_.isEmpty() && draft == QFileInfo(projectPath_))
    {
        numberedDraftLock_.reset();
        numberedDraftPath_.clear();
        return true;
    }
    if (draft.exists() && !QFile::remove(numberedDraftPath_))
    {
        setStatus("ui.editor.draft_cleanup_error", {numberedDraftPath_});
        return false;
    }
    numberedDraftLock_.reset();
    numberedDraftPath_.clear();
    return true;
}

void MainWindow::recoverNumberedDraft()
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning())
        return;
    const auto folder = numberedDraftDirectory();
    if (folder.isEmpty())
    {
        setStatus("ui.editor.no_draft");
        return;
    }
    const auto files =
        QDir(folder).entryInfoList({"numbered-draft-*.jpp"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
    QString lastError;
    int inspected = 0;
    for (const auto &file : files)
    {
        const auto path = file.absoluteFilePath();
        if (path == numberedDraftPath_ || (!projectPath_.isEmpty() && file == QFileInfo(projectPath_)))
            continue;
        if (++inspected > 16)
            break;
        auto lock = lockNumberedDraft(path);
        if (!lock)
            continue;
        Project recovered;
        try
        {
            recovered = loadProject(path);
            if (!isNumberedDraft(recovered) || !recovered.processing.value(NumberedDraftMarker).toBool())
                continue;
            recovered.processing.remove(NumberedDraftMarker);
        }
        catch (const std::exception &error)
        {
            lastError = QString::fromUtf8(error.what());
            continue;
        }
        if (!confirmDiscard())
            return;
        try
        {
            setProject(std::move(recovered));
            numberedDraftPath_ = path;
            numberedDraftLock_ = std::move(lock);
            nonMaterialDirty_ = true;
            refreshProjectIdentity();
            setCorrectionMode(true);
            setStatus("ui.editor.draft_loaded");
        }
        catch (const std::exception &error)
        {
            showError(QString::fromUtf8(error.what()));
        }
        return;
    }
    if (lastError.isEmpty())
        setStatus("ui.editor.no_draft");
    else
        setStatus("ui.editor.draft_load_error", {lastError});
}

void MainWindow::scheduleStaffEditRecovery()
{
    if (staffRecoveryTimer_ && !loading_ && project_.staffImagePlayback && project_.staffPerformance)
    {
        staffRecoveryPending_ = true;
        staffRecoveryTimer_->start(400);
    }
}

bool MainWindow::saveStaffEditRecoverySnapshot()
{
    if (!project_.staffImagePlayback || !project_.staffPerformance)
        return true;
    try
    {
        auto snapshot = project_;
        snapshot.practiceSettings = currentProjectPractice();
        staffRecoveryPath_ = saveStaffEditRecovery(snapshot, projectPath_, staffRecoverySession_,
                                                   qEnvironmentVariable("JIANPU_STAFF_RECOVERY_DIRECTORY"));
        staffRecoveryPending_ = false;
        setStatus("ui.original_playback.recovery_saved", {staffRecoveryPath_});
        return true;
    }
    catch (const std::exception &error)
    {
        setStatus("ui.original_playback.recovery_failed", {QString::fromUtf8(error.what())});
        return false;
    }
}

void MainWindow::recoverStaffEdits()
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || !confirmDiscard())
        return;
    try
    {
        if (staffRecoveryPending_)
        {
            staffRecoveryTimer_->stop();
            if (!saveStaffEditRecoverySnapshot())
                throw std::runtime_error(status_->text().toStdString());
        }
        const auto path = latestStaffEditRecovery(qEnvironmentVariable("JIANPU_STAFF_RECOVERY_DIRECTORY"));
        if (path.isEmpty())
        {
            setStatus("ui.original_playback.no_recovery");
            return;
        }
        auto recovered = loadProject(path);
        setProject(std::move(recovered));
        projectPath_.clear();
        staffRecoveryPath_ = path;
        markModified();
        setCorrectionMode(true);
        setStatus("ui.original_playback.recovery_loaded", {path});
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}

} // namespace singlilt

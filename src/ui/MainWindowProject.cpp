// Project switching, saving, and file identity.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MainWindow.h"
#include "ScoreView.h"
#include "i18n/LanguageManager.h"
#include "storage/StaffEditRecovery.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
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
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning() || !confirmDiscard())
        return;
    Project project;
    project.score.title = trText("messages.storage.untitled").toStdString();
    project.image = QImage(1000, 700, QImage::Format_RGB32);
    project.image.fill(Qt::white);
    project.generatedNotation = true;
    try
    {
        setProject(std::move(project), true);
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
        return;
    }
    setCorrectionMode(true);
    setStatus("ui.status.new_project");
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
        refreshProjectIdentity();
        rememberProject(projectPath_);
        setStatus("ui.status.saved", {path});
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
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

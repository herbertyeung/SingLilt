// Vision configuration, requests, and candidate review.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MainWindow.h"
#include "RecognitionPreviewDialog.h"
#include "i18n/LanguageManager.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <exception>

namespace singlilt
{
void MainWindow::createCloudTaskBanner(QVBoxLayout *layout)
{
    cloudTaskBanner_ = new QWidget;
    cloudTaskBanner_->setObjectName("cloudTaskBanner");
    auto *row = new QHBoxLayout(cloudTaskBanner_);
    row->setContentsMargins(10, 6, 10, 6);
    cloudTaskStatus_ = new QLabel;
    cloudTaskStatus_->setObjectName("cloudTaskStatus");
    cloudTaskStatus_->setTextFormat(Qt::PlainText);
    cloudTaskStatus_->setWordWrap(true);
    row->addWidget(cloudTaskStatus_, 1);
    cloudTaskPreview_ = button("ui.cloud.preview", row);
    cloudTaskPreview_->setObjectName("cloudTaskPreview");
    cloudTaskCancel_ = button("ui.cloud.cancel", row);
    cloudTaskCancel_->setObjectName("cloudTaskCancel");
    cloudTaskDiscard_ = button("ui.cloud.discard", row);
    cloudTaskDiscard_->setObjectName("cloudTaskDiscard");
    QObject::connect(cloudTaskPreview_, &QPushButton::clicked, this, [this] { previewCloudResult(); });
    QObject::connect(cloudTaskCancel_, &QPushButton::clicked, this, [this] { cloudTask_.cancel(); });
    QObject::connect(cloudTaskDiscard_, &QPushButton::clicked, this, [this] { discardCloudResult(); });
    layout->addWidget(cloudTaskBanner_);
    cloudTaskBanner_->hide();
}

void MainWindow::refreshCloudTaskUi()
{
    refreshLocalStaffTaskUi();
    refreshStaffAnchorEditing();
    if (!cloudTaskBanner_)
        return;
    using State = CloudRecognitionTask::State;
    const auto state = cloudTask_.state();
    const bool ready = state == State::Ready;
    const bool canApply = !busy_ && !audioLoading_ && !localStaffTask_.isRunning() &&
                          localStaffTask_.state() != LocalStaffRecognitionTask::State::Ready;
    if (cloud_)
        cloud_->setEnabled(canApply && !cloudTask_.isRunning() && !ready);
    if (cloudPreview_)
        cloudPreview_->setApplyEnabled(canApply && ready);
    cloudTaskBanner_->setVisible(state != State::Idle);
    cloudTaskPreview_->setVisible(ready);
    cloudTaskPreview_->setEnabled(canApply);
    cloudTaskCancel_->setVisible(cloudTask_.isRunning());
    cloudTaskCancel_->setEnabled(state == State::Running);
    cloudTaskDiscard_->setVisible(ready || state == State::Failed || state == State::Cancelled);
    const auto &source = cloudTask_.sourceLabel();
    switch (state)
    {
    case State::Running:
        cloudTaskStatus_->setText(trText("ui.cloud.running").arg(source));
        break;
    case State::Cancelling:
        cloudTaskStatus_->setText(trText("ui.cloud.cancelling").arg(source));
        break;
    case State::Ready:
        cloudTaskStatus_->setText(
            trText("ui.cloud.ready").arg(source).arg(cloudTask_.result()->score.notes.size()));
        break;
    case State::Failed:
        cloudTaskStatus_->setText(
            trText("ui.cloud.failed").arg(source, localizeMessage(cloudTask_.errorString())));
        break;
    case State::Cancelled:
        cloudTaskStatus_->setText(trText("ui.cloud.cancelled").arg(source));
        break;
    case State::Idle:
        cloudTaskStatus_->clear();
        break;
    }
}

void MainWindow::previewCloudResult()
{
    const auto *result = cloudTask_.result();
    if (!result || busy_ || audioLoading_)
        return;
    if (cloudPreview_)
    {
        cloudPreview_->show();
        cloudPreview_->raise();
        cloudPreview_->activateWindow();
        return;
    }
    Project candidate{result->score, cloudTask_.image(), result->warnings};
    candidate.staffPerformance = result->staffPerformance;
    if (candidate.staffPerformance)
        candidate.practiceMix.accompanimentEnabled = candidate.staffPerformance->staffCount > 1;
    if (result->staffNotation)
    {
        candidate.notationStyle = NotationStyle::Staff;
        candidate.staffBassClef = result->staffBass;
        candidate.staffKeyFifths = result->staffKeyFifths;
        candidate.staffMinor = result->staffMinor;
    }
    auto *preview = new RecognitionPreviewDialog(std::move(candidate), cloudTask_.sourceLabel(), this);
    cloudPreview_ = preview;
    preview->setAttribute(Qt::WA_DeleteOnClose);
    preview->applyRequested = [this, preview]
    {
        if (cloudPreview_ == preview)
            applyCloudResult();
    };
    QObject::connect(preview, &QDialog::finished, this,
                     [this, preview]
                     {
                         if (cloudPreview_ == preview)
                             cloudPreview_ = nullptr;
                     });
    preview->show();
}

void MainWindow::applyCloudResult()
{
    const auto *result = cloudTask_.result();
    if (!result || busy_ || audioLoading_ || localStaffTask_.isRunning() ||
        localStaffTask_.state() == LocalStaffRecognitionTask::State::Ready)
        return;
    // Snapshot before modal confirmations; queued UI events may run while they are open.
    Project candidate{result->score, cloudTask_.image(), result->warnings};
    candidate.staffPerformance = result->staffPerformance;
    if (candidate.staffPerformance)
        candidate.practiceMix.accompanimentEnabled = candidate.staffPerformance->staffCount > 1;
    if (result->staffNotation)
    {
        candidate.notationStyle = NotationStyle::Staff;
        candidate.staffBassClef = result->staffBass;
        candidate.staffKeyFifths = result->staffKeyFifths;
        candidate.staffMinor = result->staffMinor;
    }
    const auto debugText = result->debugText;
    QMessageBox confirmation(QMessageBox::Question, trText("ui.cloud.replace_title"),
                             trText("ui.cloud.replace_message")
                                 .arg(QString::fromStdString(project_.score.title), cloudTask_.sourceLabel()),
                             QMessageBox::Yes | QMessageBox::No, cloudPreview_);
    confirmation.setObjectName("cloudReplaceConfirmation");
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::No);
    if (confirmation.exec() != QMessageBox::Yes || !confirmDiscard())
        return;
    if (!cloudTask_.result() || busy_ || audioLoading_)
        return;
    try
    {
        setProject(candidate, true);
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
        return;
    }
    debugText_ = debugText;
    discardCloudResult();
    setStatus("ui.cloud.applied");
}

void MainWindow::discardCloudResult()
{
    if (cloudTask_.isRunning())
        return;
    if (cloudPreview_)
        cloudPreview_->close();
    cloudTask_.discard();
}
} // namespace singlilt

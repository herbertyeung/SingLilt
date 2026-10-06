// Local staff-image recognition and candidate application.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ClassroomDialog.h"
#include "MainWindow.h"
#include "RecognitionPreviewDialog.h"
#include "i18n/LanguageManager.h"
#include <QAction>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <exception>

namespace singlilt
{
void MainWindow::createLocalStaffTaskBanner(QVBoxLayout *layout)
{
    localStaffBanner_ = new QWidget;
    localStaffBanner_->setObjectName("localStaffTaskBanner");
    auto *row = new QHBoxLayout(localStaffBanner_);
    row->setContentsMargins(10, 6, 10, 6);
    localStaffStatus_ = new QLabel;
    localStaffStatus_->setObjectName("localStaffTaskStatus");
    localStaffStatus_->setTextFormat(Qt::PlainText);
    localStaffStatus_->setWordWrap(true);
    row->addWidget(localStaffStatus_, 1);
    localStaffPreviewButton_ = button("ui.local_staff.preview", row);
    localStaffPreviewButton_->setObjectName("localStaffPreview");
    localStaffCancelButton_ = button("ui.cloud.cancel", row);
    localStaffCancelButton_->setObjectName("localStaffCancel");
    localStaffDiscardButton_ = button("ui.cloud.discard", row);
    localStaffDiscardButton_->setObjectName("localStaffDiscard");
    connect(localStaffPreviewButton_, &QPushButton::clicked, this, [this] { previewLocalStaffResult(); });
    connect(localStaffCancelButton_, &QPushButton::clicked, this, [this] { localStaffTask_.cancel(); });
    connect(localStaffDiscardButton_, &QPushButton::clicked, this, [this] { discardLocalStaffResult(); });
    layout->addWidget(localStaffBanner_);
    localStaffBanner_->hide();
}

bool MainWindow::startLocalStaffRecognition(QImage image, QString path, LocalStaffRecognitionOptions options)
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning() || cloudTask_.isRunning() ||
        cloudTask_.state() == CloudRecognitionTask::State::Ready || localStaffTask_.isRunning() ||
        localStaffTask_.state() == LocalStaffRecognitionTask::State::Ready || !resolveNoteDraft())
        return false;
    const QString label = path == "clipboard" || QFileInfo(path).fileName().isEmpty()
                              ? trText("ui.local_staff.clipboard")
                              : QFileInfo(path).fileName();
    if (localStaffPreview_)
        localStaffPreview_->close();
    if (localStaffTask_.state() != LocalStaffRecognitionTask::State::Idle)
        localStaffTask_.discard();
    if (!localStaffTask_.start(std::move(image), std::move(path), label, std::move(options)))
        return false;
    previewLocalStaffResult();
    setStatus("ui.local_staff.started");
    return true;
}

void MainWindow::refreshLocalStaffTaskUi()
{
    if (!localStaffBanner_)
        return;
    using State = LocalStaffRecognitionTask::State;
    const auto state = localStaffTask_.state();
    const bool ready = state == State::Ready;
    const bool canApply = !busy_ && !audioLoading_ && !notePreviewLoading_ && !classroom_ &&
                          !cloudTask_.isRunning() && !audioTask_.isRunning();
    localStaffBanner_->setVisible(state != State::Idle);
    localStaffPreviewButton_->setVisible(ready || localStaffTask_.isRunning());
    localStaffPreviewButton_->setEnabled(!busy_ && !audioLoading_);
    localStaffCancelButton_->setVisible(localStaffTask_.isRunning());
    localStaffCancelButton_->setEnabled(state == State::Running);
    localStaffDiscardButton_->setVisible(ready || state == State::Failed || state == State::Cancelled);
    if (localStaffPreview_)
        localStaffPreview_->setApplyEnabled(canApply && ready);
    if (auto *action = findChild<QAction *>("menuImportStaffImage"))
        action->setEnabled(canApply && !localStaffTask_.isRunning() && !ready);
    const auto &source = localStaffTask_.sourceLabel();
    switch (state)
    {
    case State::Running:
        localStaffStatus_->setText(trText("ui.local_staff.running").arg(source));
        break;
    case State::Cancelling:
        localStaffStatus_->setText(trText("ui.local_staff.cancelling").arg(source));
        break;
    case State::Ready:
        localStaffStatus_->setText(trText("ui.local_staff.ready")
                                       .arg(source)
                                       .arg(localStaffTask_.result()->project->staffPerformance->notes.size()));
        break;
    case State::Failed:
        localStaffStatus_->setText(
            trText("ui.local_staff.failed").arg(source, localizeMessage(localStaffTask_.errorString())));
        break;
    case State::Cancelled:
        localStaffStatus_->setText(trText("ui.local_staff.cancelled").arg(source));
        break;
    case State::Idle:
        localStaffStatus_->clear();
        break;
    }
    if (localStaffPreview_ && localStaffPreview_->property("rawLocalStaffPreview").toBool() && !ready)
        if (auto *warnings = localStaffPreview_->findChild<QPlainTextEdit *>("previewWarnings"))
            warnings->setPlainText(localStaffStatus_->text());
}

void MainWindow::previewLocalStaffResult()
{
    using State = LocalStaffRecognitionTask::State;
    const bool ready = localStaffTask_.state() == State::Ready;
    if ((!ready && !localStaffTask_.isRunning()) || busy_ || audioLoading_)
        return;
    if (localStaffPreview_)
    {
        localStaffPreview_->show();
        localStaffPreview_->raise();
        localStaffPreview_->activateWindow();
        return;
    }
    Project candidate;
    if (ready)
        candidate = *localStaffTask_.result()->project;
    else
    {
        candidate.image = localStaffTask_.image();
        candidate.staffImagePlayback = true;
        candidate.notationStyle = NotationStyle::Staff;
        candidate.score.title = localStaffTask_.sourceLabel().toStdString();
        candidate.warnings.append(trText("ui.local_staff.original_pending"));
        if (localStaffTask_.pageInputs().size() > 1)
            for (const auto &page : localStaffTask_.pageInputs())
                candidate.staffPages.push_back({page.label, page.image, page.image, 0, 0});
    }
    auto *preview = new RecognitionPreviewDialog(std::move(candidate), localStaffTask_.sourceLabel(), this,
                                                 ready ? localStaffTask_.image() : QImage{});
    localStaffPreview_ = preview;
    preview->setObjectName("localStaffRecognitionPreview");
    preview->setProperty("rawLocalStaffPreview", !ready);
    preview->setAttribute(Qt::WA_DeleteOnClose);
    preview->setApplyEnabled(ready && !classroom_ && !cloudTask_.isRunning() && !audioTask_.isRunning());
    preview->applyRequested = [this, preview]
    {
        if (localStaffPreview_ == preview)
            applyLocalStaffResult();
    };
    connect(preview, &QDialog::finished, this,
            [this, preview]
            {
                if (localStaffPreview_ == preview)
                    localStaffPreview_ = nullptr;
            });
    preview->show();
}

void MainWindow::applyLocalStaffResult()
{
    const auto *result = localStaffTask_.result();
    if (!result || !result->valid() || busy_ || audioLoading_ || notePreviewLoading_ || classroom_ ||
        cloudTask_.isRunning() || audioTask_.isRunning())
        return;
    const auto originalProcessing = result->project->processing;
    const auto reviewed = localStaffPreview_ ? localStaffPreview_->projectForApply() : result->project;
    if (!reviewed || (reviewed->processing.value("tempoNeedsConfirmation").toBool()))
    {
        setStatus("ui.fidelity.tempo_required");
        return;
    }
    Project candidate = *reviewed;
    const QImage original = result->originalImage;
    const QString log = result->engineLog;
    QMessageBox confirmation(QMessageBox::Question, trText("ui.local_staff.replace_title"),
                             trText("ui.local_staff.replace_message").arg(localStaffTask_.sourceLabel()),
                             QMessageBox::Yes | QMessageBox::No, localStaffPreview_);
    confirmation.setObjectName("localStaffReplaceConfirmation");
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::No);
    if (confirmation.exec() != QMessageBox::Yes || !confirmDiscard())
        return;
    if (!localStaffTask_.result() || localStaffTask_.result()->project->processing != originalProcessing ||
        busy_ || audioLoading_ || notePreviewLoading_ || classroom_)
        return;
    try
    {
        setProject(std::move(candidate), true);
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
        return;
    }
    localStaffOriginalImage_ = original;
    debugText_ = log;
    discardLocalStaffResult();
    setStatus("ui.local_staff.applied", {QString::number(project_.staffPerformance->notes.size())});
}

void MainWindow::discardLocalStaffResult()
{
    if (localStaffTask_.isRunning())
        return;
    if (localStaffPreview_)
        localStaffPreview_->close();
    localStaffTask_.discard();
}
} // namespace singlilt

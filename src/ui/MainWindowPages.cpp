// Source-page navigation and multi-page import.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MainWindow.h"
#include "RecognitionPreviewDialog.h"
#include "ScoreView.h"
#include "StaffPageImportDialog.h"
#include "i18n/LanguageManager.h"
#include "recognition/StaffPageSplitter.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QImageReader>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <algorithm>
#include <stdexcept>

namespace singlilt
{
void MainWindow::importStaffImages(const QStringList &paths)
{
    if (paths.isEmpty() || busy_ || audioLoading_ || notePreviewLoading_ || localStaffTask_.isRunning() ||
        localStaffTask_.state() == LocalStaffRecognitionTask::State::Ready || !resolveNoteDraft())
        return;
    try
    {
        if (paths.size() > 32)
            throw std::runtime_error(trText("messages.pages.too_many").toStdString());
        std::vector<StaffPageInput> pages;
        qint64 totalPixels = 0;
        for (int index = 0; index < paths.size(); ++index)
        {
            QImageReader reader(paths[index]);
            reader.setAutoTransform(true);
            const auto size = reader.size();
            if (size.width() > 12000 || size.height() > 20000 || qint64(size.width()) * size.height() > 50000000)
                throw std::runtime_error(trText("ui.error.image_large").toStdString());
            if (size.isValid() && qint64(size.width()) * size.height() > 400000000 - totalPixels)
                throw std::runtime_error(trText("messages.local_staff.page_pixel_limit").toStdString());
            const auto image = reader.read();
            if (image.isNull())
                throw std::runtime_error(reader.errorString().toStdString());
            totalPixels += qint64(image.width()) * image.height();
            if (totalPixels > 400000000)
                throw std::runtime_error(trText("messages.local_staff.page_pixel_limit").toStdString());
            auto detected = splitStaffPageInputs(image, QFileInfo(paths[index]).fileName(), index);
            pages.insert(pages.end(), detected.begin(), detected.end());
            if (pages.size() > 32)
                throw std::runtime_error(trText("messages.pages.too_many").toStdString());
        }
        if (pages.size() == 1)
        {
            startLocalStaffRecognition(pages.front().image, paths.front());
            return;
        }
        StaffPageImportDialog order(std::move(pages), this);
        if (order.exec() != QDialog::Accepted)
            return;
        auto selected = order.orderedPages();
        LocalStaffRecognitionOptions options;
        options.timeoutSeconds = std::min(600, std::max(180, int(selected.size()) * 60));
        startLocalStaffRecognitionPages(std::move(selected), trText("ui.pages.song_input"), options);
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}
bool MainWindow::startLocalStaffRecognitionPages(std::vector<StaffPageInput> pages, QString label,
                                                 LocalStaffRecognitionOptions options)
{
    if (pages.empty() || pages.size() > 32 || busy_ || audioLoading_ || notePreviewLoading_ ||
        audioTask_.isRunning() || cloudTask_.isRunning() ||
        cloudTask_.state() == CloudRecognitionTask::State::Ready || localStaffTask_.isRunning() ||
        localStaffTask_.state() == LocalStaffRecognitionTask::State::Ready || !resolveNoteDraft())
        return false;
    if (localStaffPreview_)
        localStaffPreview_->close();
    if (localStaffTask_.state() != LocalStaffRecognitionTask::State::Idle)
        localStaffTask_.discard();
    if (!localStaffTask_.startPages(std::move(pages), std::move(label), std::move(options)))
        return false;
    previewLocalStaffResult();
    setStatus("ui.pages.started");
    return true;
}
void MainWindow::refreshStaffPageControls()
{
    if (!staffPageSelector_)
        return;
    const QSignalBlocker blocker(staffPageSelector_);
    staffPageSelector_->clear();
    for (std::size_t index = 0; index < project_.staffPages.size(); ++index)
        staffPageSelector_->addItem(trText("ui.pages.page_number").arg(index + 1).arg(project_.staffPages.size()),
                                    int(index));
    staffPageIndex_ =
        project_.staffPages.empty() ? 0 : std::clamp(staffPageIndex_, 0, int(project_.staffPages.size()) - 1);
    refreshStaffSourceView();
    staffPageSelector_->setCurrentIndex(staffPageIndex_);
    const bool multiple = project_.staffPages.size() > 1;
    staffPageToolbar_->setVisible(multiple);
    for (QWidget *control :
         std::initializer_list<QWidget *>{staffPageSelector_, staffPreviousPage_, staffNextPage_, staffAutoPage_})
        control->setVisible(multiple);
    staffPreviousPage_->setEnabled(staffPageIndex_ > 0);
    staffNextPage_->setEnabled(staffPageIndex_ + 1 < int(project_.staffPages.size()));
}
void MainWindow::setStaffPage(int pageIndex, bool fit)
{
    if (pageIndex < 0 || std::size_t(pageIndex) >= project_.staffPages.size() || !view_)
        return;
    if (project_.staffImagePlayback && pageIndex != staffPageIndex_ && !loadingNote_ && !resolveNoteDraft())
    {
        if (staffPageSelector_)
        {
            const QSignalBlocker blocker(staffPageSelector_);
            staffPageSelector_->setCurrentIndex(staffPageIndex_);
        }
        return;
    }
    staffPageIndex_ = pageIndex;
    const auto &page = project_.staffPages[std::size_t(pageIndex)];
    view_->setScore(project_.staffImagePlayback ? page.sourceImage : page.renderedImage, project_.score, pageIndex,
                    project_.staffImagePlayback);
    view_->setStaffPerformance(project_.staffPerformance ? &*project_.staffPerformance : nullptr);
    refreshStaffAnchorEditing();
    if (staffPageSelector_)
    {
        const QSignalBlocker blocker(staffPageSelector_);
        staffPageSelector_->setCurrentIndex(pageIndex);
    }
    if (staffPreviousPage_)
        staffPreviousPage_->setEnabled(pageIndex > 0);
    if (staffNextPage_)
        staffNextPage_->setEnabled(pageIndex + 1 < int(project_.staffPages.size()));
    refreshStaffSourceView();
    if (fit)
        displayedScoreView()->fitWidth();
}
ScoreView *MainWindow::displayedScoreView() const
{
    return !project_.staffImagePlayback && staffScoreTabs_ && staffScoreTabs_->currentIndex() == 1
               ? staffSourceView_
               : view_;
}
void MainWindow::refreshStaffSourceView()
{
    if (!staffScoreTabs_ || !staffSourceView_)
        return;
    if (project_.staffImagePlayback)
    {
        staffScoreTabs_->setCurrentIndex(0);
        staffScoreTabs_->setTabText(0, trText("ui.original_playback.view"));
        staffScoreTabs_->setTabVisible(1, false);
        staffScoreTabs_->tabBar()->hide();
        refreshStaffAnchorEditing();
        return;
    }
    const QImage source =
        project_.staffPages.empty() ? QImage{} : project_.staffPages[std::size_t(staffPageIndex_)].sourceImage;
    const bool available = !source.isNull();
    staffScoreTabs_->setTabText(0, trText("ui.fidelity.recognized"));
    staffScoreTabs_->setTabText(1, trText("ui.fidelity.original"));
    if (!available)
        staffScoreTabs_->setCurrentIndex(0);
    staffScoreTabs_->setTabVisible(1, available);
    staffScoreTabs_->tabBar()->setVisible(available);
    staffSourceView_->setToolTip(trText("ui.fidelity.original_help"));
    staffSourceView_->setScore(source, Score{}, staffPageIndex_);
    if (available)
        staffSourceView_->fitWidth();
}
} // namespace singlilt

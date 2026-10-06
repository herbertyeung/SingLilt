// Recognition-candidate comparison and acceptance.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/ProjectStore.h"
#include <QDialog>
#include <functional>
#include <optional>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QTableWidget;
class QGraphicsRectItem;
class QTabWidget;
class QComboBox;
class QDoubleSpinBox;
class QCheckBox;

namespace singlilt
{
class ScoreView;

class RecognitionPreviewDialog : public QDialog
{
  public:
    explicit RecognitionPreviewDialog(Project project, QString sourceLabel, QWidget *parent = nullptr,
                                      QImage originalImage = {});

    std::function<void()> applyRequested;
    void setApplyEnabled(bool enabled);
    std::optional<Project> projectForApply() const;

  protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

  private:
    void createUi();
    void populateNotes();
    void populateStaffNotes();
    void selectNote(int index, bool followScore);
    void selectStaffNote(int index, bool followScore);
    void retranslateStaffNotes();
    void retranslateUi();
    void setPage(int index);

    Project project_;
    QImage originalImage_;
    ScoreView *originalView_ = nullptr;
    QTabWidget *imageTabs_ = nullptr;
    QComboBox *pageSelector_ = nullptr;
    int pageIndex_ = 0;
    QString sourceLabel_;
    bool initialFitDone_ = false;
    bool applyAllowed_ = false;
    QLabel *tempoReviewLabel_ = nullptr;
    QDoubleSpinBox *tempoReview_ = nullptr;
    QCheckBox *tempoConfirmed_ = nullptr;
    ScoreView *scoreView_ = nullptr;
    QGraphicsRectItem *staffSelection_ = nullptr;
    QTableWidget *notes_ = nullptr;
    QLabel *source_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *summary_ = nullptr;
    QLabel *warningsTitle_ = nullptr;
    QLabel *footer_ = nullptr;
    QPlainTextEdit *warnings_ = nullptr;
    QPushButton *fitWidth_ = nullptr;
    QPushButton *apply_ = nullptr;
    QPushButton *close_ = nullptr;
};
} // namespace singlilt

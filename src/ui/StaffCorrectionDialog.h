// Full-voice staff corrections before applying an edited score.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/ProjectStore.h"
#include <QDialog>
#include <optional>
class QComboBox;
class QLabel;
class QTableWidget;

namespace singlilt
{
class ScoreView;
class StaffCorrectionDialog final : public QDialog
{
  public:
    explicit StaffCorrectionDialog(Project project, QWidget *parent = nullptr);
    std::optional<Project> correctedProject() const;
    void accept() override;

  private:
    void addNoteRow(const StaffPerformanceNote *note, int originalIndex);
    void showSourcePage(int page);
    Project original_;
    std::optional<Project> corrected_;
    QTableWidget *notes_ = nullptr;
    QTableWidget *measures_ = nullptr;
    QLabel *error_ = nullptr;
    QComboBox *sourcePage_ = nullptr;
    ScoreView *sourceView_ = nullptr;
    int bindingRow_ = -1;
};
} // namespace singlilt

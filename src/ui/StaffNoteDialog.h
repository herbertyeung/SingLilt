// Staff-note pitch, timing, and source-position entry.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/ProjectStore.h"
#include <QDialog>
#include <functional>
class QLabel;

namespace singlilt
{
class StaffNoteEditor;
class StaffNoteDialog final : public QDialog
{
  public:
    StaffNoteDialog(Project project, StaffPerformanceNote draft, QWidget *parent = nullptr, int editingIndex = -1);
    const std::optional<Project> &correctedProject() const;
    const StaffPerformanceNote &note() const;
    void setTimingSuggestion(const QString &message, bool available);
    std::function<void(const StaffPerformanceNote &)> draftPreviewChanged;
    std::function<void(const StaffPerformanceNote &)> auditionRequested;

  protected:
    void accept() override;

  private:
    Project original_;
    StaffPerformanceNote draft_;
    std::optional<Project> corrected_;
    StaffNoteEditor *editor_ = nullptr;
    QLabel *timingHint_ = nullptr;
    int editingIndex_ = -1;
};
} // namespace singlilt

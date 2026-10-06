// Staff-note inspector fields, previews, and draft management.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/ProjectStore.h"
#include <QWidget>
#include <array>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace singlilt
{
class StaffNoteEditor final : public QWidget
{
  public:
    explicit StaffNoteEditor(QWidget *parent = nullptr);
    void setNote(Project original, StaffPerformanceNote draft);
    void clearNote();
    StaffPerformanceNote note() const;
    bool hasDraft() const;
    void setEditingEnabled(bool enabled);
    void discardDraft();
    void setError(const QString &message);
    void setModalControls(bool modal);
    void requireOnsetInput();

    std::function<void(const StaffPerformanceNote &)> draftPreviewChanged;
    std::function<void(const StaffPerformanceNote &)> auditionRequested;
    std::function<void()> draftChanged;
    std::function<void()> applyRequested;
    std::function<void()> cancelRequested;
    std::function<void()> deleteRequested;

  private:
    std::optional<Project> original_;
    StaffPerformanceNote draft_;
    std::array<QString, 6> baselineFields_{};
    QLabel *title_ = nullptr;
    QLabel *help_ = nullptr;
    QSpinBox *pitch_ = nullptr;
    QSpinBox *staff_ = nullptr;
    QComboBox *measure_ = nullptr;
    QLineEdit *onset_ = nullptr;
    QLineEdit *duration_ = nullptr;
    QLineEdit *voice_ = nullptr;
    QLabel *error_ = nullptr;
    QLabel *pitchName_ = nullptr;
    QPushButton *apply_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *delete_ = nullptr;
    QPushButton *audition_ = nullptr;
    bool binding_ = false;
    bool editingEnabled_ = true;
    bool modalControls_ = false;

    std::array<QString, 6> fields() const;
    void updateDraftPreview(bool notify);
    void refreshControls();
};
} // namespace singlilt

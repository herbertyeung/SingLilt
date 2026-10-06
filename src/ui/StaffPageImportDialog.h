// Page order and split choices for staff-image import.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "recognition/StaffPageInput.h"
#include <QDialog>
#include <vector>
class QListWidget;
namespace singlilt
{
class StaffPageImportDialog final : public QDialog
{
  public:
    explicit StaffPageImportDialog(std::vector<StaffPageInput> pages, QWidget *parent = nullptr);
    std::vector<StaffPageInput> orderedPages() const;

  private:
    void moveSelected(int offset);
    void splitSelected();
    void refreshLabels();
    std::vector<StaffPageInput> pages_;
    QListWidget *list_ = nullptr;
};
} // namespace singlilt

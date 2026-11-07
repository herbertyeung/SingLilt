// New numbered-score setup without mutating the current document.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "domain/Score.h"
#include <QDialog>
namespace singlilt
{
class ScorePropertiesPanel;
class NewScoreDialog final : public QDialog
{
  public:
    explicit NewScoreDialog(QWidget *parent = nullptr);
    Score score() const;

  private:
    ScorePropertiesPanel *properties_;
};
} // namespace singlilt

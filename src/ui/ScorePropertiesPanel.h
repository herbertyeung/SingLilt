// Reusable score metadata and meter fields.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "domain/Score.h"
#include <QWidget>
#include <functional>
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;

namespace singlilt
{
class ScorePropertiesPanel final : public QWidget
{
  public:
    explicit ScorePropertiesPanel(QWidget *parent = nullptr);
    void setScore(const Score &score);
    Score editedScore(const Score &base) const;
    std::function<void()> edited;

  private:
    bool loading_ = false;
    QLineEdit *title_, *subtitle_, *lyricist_, *composer_;
    QComboBox *key_, *beatUnit_;
    QSpinBox *beats_;
    QDoubleSpinBox *tempo_;
};
} // namespace singlilt

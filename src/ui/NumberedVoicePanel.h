// Compact whole-score voice management inside the notation ribbon.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#pragma once
#include "domain/NumberedComposition.h"
#include <QWidget>
#include <functional>
class QComboBox;
namespace singlilt
{
enum class VoiceAction
{
    Add,
    Rename,
    Remove,
    Up,
    Down
};
class NumberedVoicePanel final : public QWidget
{
  public:
    explicit NumberedVoicePanel(QWidget *parent = nullptr);
    void setComposition(const NumberedComposition &composition, int active);
    void retranslate();
    std::function<void(int)> selected;
    std::function<void(VoiceAction)> requested;

  private:
    QComboBox *voices_;
};
} // namespace singlilt

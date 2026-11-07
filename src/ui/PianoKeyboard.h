// Accessible three-octave pitch-entry keyboard.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include <QWidget>
#include <functional>
#include <vector>
class QPushButton;
namespace singlilt
{
class PianoKeyboard final : public QWidget
{
  public:
    explicit PianoKeyboard(QWidget *parent = nullptr);
    void retranslate();
    std::function<void(int, int, int)> noteRequested;

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    struct Key
    {
        QPushButton *button;
        int whiteIndex;
        bool black;
    };
    std::vector<Key> keys_;
};
} // namespace singlilt

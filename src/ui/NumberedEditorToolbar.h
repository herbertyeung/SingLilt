// Grouped notation tools and authoring command strip.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include <QWidget>
#include <functional>
class QLabel;
class QTabWidget;
namespace singlilt
{
class NumberedVoicePanel;
enum class NumberedEditAction
{
    Accent,
    Staccato,
    Rest,
    Dotted,
    Sharp,
    Flat,
    Natural,
    OctaveUp,
    OctaveDown,
    Tie,
    DeleteNote,
    InsertBarBefore,
    InsertBarAfter,
    DeleteBar,
    AddBars,
    Lyrics,
    Append
};
class NumberedEditorToolbar final : public QWidget
{
  public:
    explicit NumberedEditorToolbar(QWidget *parent = nullptr);
    void setPosition(int bar, double usedBeats, double totalBeats, bool appending = false);
    std::function<void(NumberedEditAction)> actionRequested;
    std::function<void(int)> durationRequested;
    std::function<void(bool)> keyboardToggled;
    std::function<void(bool)> propertiesToggled;
    void retranslate();
    NumberedVoicePanel *voicePanel() const
    {
        return voices_;
    }
    std::function<void(int)> dynamicRequested;

  private:
    QTabWidget *tabs_;
    NumberedVoicePanel *voices_;
    QLabel *position_;
    int bar_ = 0;
    double usedBeats_ = 0, totalBeats_ = 4;
    bool appending_ = false;
};
} // namespace singlilt

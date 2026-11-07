// Grouped notation tools and authoring command strip.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "NumberedEditorToolbar.h"
#include "NumberedVoicePanel.h"
#include <QComboBox>
#include "i18n/LanguageManager.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace singlilt
{
NumberedEditorToolbar::NumberedEditorToolbar(QWidget *parent) : QWidget(parent)
{
    setObjectName("numberedEditorToolbar");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto action = [this](QHBoxLayout *row, const char *key, const char *name, NumberedEditAction command)
    {
        auto *button = new QPushButton(trText(key));
        button->setObjectName(name);
        button->setProperty("editorTextKey", key);
        button->setFocusPolicy(Qt::NoFocus);
        row->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, command]
                {
                    if (actionRequested)
                        actionRequested(command);
                });
    };
    tabs_ = new QTabWidget;
    tabs_->setObjectName("editorToolTabs");
    layout->addWidget(tabs_);
    auto *panels = new QWidget;
    panels->setObjectName("editorPanelToggles");
    auto *panelRow = new QHBoxLayout(panels);
    panelRow->setContentsMargins(0, 0, 4, 4);
    panelRow->setSpacing(6);
    for (bool keyboard : {true, false})
    {
        const char *key = keyboard ? "ui.editor.keyboard" : "ui.editor.properties";
        auto *toggle = new QPushButton(trText(key));
        toggle->setProperty("editorTextKey", key);
        toggle->setObjectName(keyboard ? "editorKeyboardToggle" : "editorPropertiesToggle");
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setFocusPolicy(Qt::NoFocus);
        panelRow->addWidget(toggle);
        connect(toggle, &QPushButton::toggled, this,
                [this, keyboard](bool checked)
                {
                    if (keyboard && keyboardToggled)
                        keyboardToggled(checked);
                    if (!keyboard && propertiesToggled)
                        propertiesToggled(checked);
                });
    }
    tabs_->setCornerWidget(panels, Qt::TopRightCorner);
    auto page = [this](const char *key)
    {
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content = new QWidget;
        auto *row = new QHBoxLayout(content);
        row->setContentsMargins(8, 6, 8, 6);
        scroll->setWidget(content);
        tabs_->addTab(scroll, trText(key));
        return row;
    };
    auto *notes = page("ui.editor.notes");
    auto *durations = new QButtonGroup(this);
    const int ticks[] = {960, 480, 240, 120, 60};
    const char *keys[] = {"half", "quarter", "eighth", "sixteenth", "thirty_second"};
    for (int i = 0; i < 5; ++i)
    {
        const QString key = "ui.editor." + QString(keys[i]);
        auto *button = new QPushButton(trText(qPrintable(key)));
        button->setProperty("editorTextKey", key);
        button->setObjectName(QString("editorDuration%1").arg(ticks[i]));
        button->setCheckable(true);
        button->setChecked(i == 1);
        button->setFocusPolicy(Qt::NoFocus);
        durations->addButton(button);
        notes->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, duration = ticks[i]]
                {
                    if (durationRequested)
                        durationRequested(duration);
                });
    }
    action(notes, "ui.editor.dotted", "editorDotted", NumberedEditAction::Dotted);
    action(notes, "ui.editor.rest", "editorRest", NumberedEditAction::Rest);
    action(notes, "ui.editor.sharp", "editorSharp", NumberedEditAction::Sharp);
    action(notes, "ui.editor.flat", "editorFlat", NumberedEditAction::Flat);
    action(notes, "ui.editor.natural", "editorNatural", NumberedEditAction::Natural);
    action(notes, "ui.editor.octave_up", "editorOctaveUp", NumberedEditAction::OctaveUp);
    action(notes, "ui.editor.octave_down", "editorOctaveDown", NumberedEditAction::OctaveDown);
    action(notes, "ui.editor.tie", "editorTie", NumberedEditAction::Tie);
    notes->addStretch();
    auto *bars = page("ui.editor.bars");
    action(bars, "ui.editor.bar_before", "editorBarBefore", NumberedEditAction::InsertBarBefore);
    action(bars, "ui.editor.bar_after", "editorBarAfter", NumberedEditAction::InsertBarAfter);
    action(bars, "ui.editor.bar_delete", "editorBarDelete", NumberedEditAction::DeleteBar);
    action(bars, "ui.editor.add_bars", "editorAddBars", NumberedEditAction::AddBars);
    bars->addStretch();
    voices_ = new NumberedVoicePanel;
    tabs_->addTab(voices_, trText("ui.editor.voices"));
    auto *text = page("ui.editor.text");
    action(text, "ui.editor.lyrics", "editorLyrics", NumberedEditAction::Lyrics);
    text->addStretch();
    auto *marks = page("ui.editor.marks");
    auto *dynamics = new QComboBox;
    dynamics->setObjectName("numberedDynamics");
    dynamics->addItems({trText("ui.editor.dynamic_inherit"), "pp", "p", "mp", "mf", "f", "ff"});
    marks->addWidget(dynamics);
    connect(dynamics, &QComboBox::currentIndexChanged, this,
            [this](int level)
            {
                if (dynamicRequested)
                    dynamicRequested(level);
            });
    action(marks, "ui.editor.accent", "editorAccent", NumberedEditAction::Accent);
    action(marks, "ui.editor.staccato", "editorStaccato", NumberedEditAction::Staccato);
    marks->addStretch();
    tabs_->setFixedHeight(90);
    auto *status = new QHBoxLayout;
    position_ = new QLabel;
    position_->setObjectName("editorPosition");
    status->addWidget(position_);
    status->addStretch();
    action(status, "ui.editor.append", "editorAppend", NumberedEditAction::Append);
    action(status, "ui.editor.delete_note", "editorDeleteNote", NumberedEditAction::DeleteNote);
    layout->addLayout(status);
}
void NumberedEditorToolbar::setPosition(int bar, double usedBeats, double totalBeats, bool appending)
{
    bar_ = bar;
    usedBeats_ = usedBeats;
    totalBeats_ = totalBeats;
    appending_ = appending;
    position_->setText(
        trText("ui.editor.position").arg(bar + 1).arg(usedBeats, 0, 'g', 4).arg(totalBeats, 0, 'g', 4) +
        (appending ? " · " + trText("ui.editor.append_cursor") : QString()));
}
void NumberedEditorToolbar::retranslate()
{
    setPosition(bar_, usedBeats_, totalBeats_, appending_);
    for (auto *button : findChildren<QPushButton *>())
        if (button->property("editorTextKey").isValid())
            button->setText(trText(qPrintable(button->property("editorTextKey").toString())));
    tabs_->setTabText(0, trText("ui.editor.notes"));
    tabs_->setTabText(1, trText("ui.editor.bars"));
    tabs_->setTabText(2, trText("ui.editor.voices"));
    tabs_->setTabText(3, trText("ui.editor.text"));
    tabs_->setTabText(4, trText("ui.editor.marks"));
    voices_->retranslate();
    findChild<QComboBox *>("numberedDynamics")->setItemText(0, trText("ui.editor.dynamic_inherit"));
}
} // namespace singlilt

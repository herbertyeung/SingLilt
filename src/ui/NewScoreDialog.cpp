// New numbered-score setup without mutating the current document.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "NewScoreDialog.h"
#include "ScorePropertiesPanel.h"
#include "i18n/LanguageManager.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace singlilt
{
NewScoreDialog::NewScoreDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("newScoreDialog");
    setWindowTitle(trText("ui.editor.new"));
    setMinimumWidth(400);
    auto *layout = new QVBoxLayout(this);
    properties_ = new ScorePropertiesPanel;
    Score initial;
    initial.title = trText("messages.storage.untitled").toStdString();
    initial.bpm = 120;
    properties_->setScore(initial);
    layout->addWidget(properties_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(trText("ui.editor.create"));
    buttons->button(QDialogButtonBox::Cancel)->setText(trText("ui.button.cancel"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
Score NewScoreDialog::score() const
{
    auto score = properties_->editedScore(Score{});
    score.editorMinimumBars = 4;
    return score;
}
} // namespace singlilt

// Compact whole-score voice management inside the notation ribbon.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#include "NumberedVoicePanel.h"
#include "i18n/LanguageManager.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
namespace singlilt
{
NumberedVoicePanel::NumberedVoicePanel(QWidget *parent) : QWidget(parent)
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(8, 6, 8, 6);
    voices_ = new QComboBox;
    voices_->setObjectName("numberedVoiceSelector");
    voices_->setMinimumWidth(150);
    voices_->setMaximumWidth(220);
    row->addWidget(voices_);
    const char *keys[] = {"ui.editor.voice_add", "ui.editor.voice_rename", "ui.editor.voice_remove",
                          "ui.editor.voice_up", "ui.editor.voice_down"};
    const char *names[] = {"voiceAdd", "voiceRename", "voiceRemove", "voiceUp", "voiceDown"};
    for (int i = 0; i < 5; ++i)
    {
        auto *button = new QPushButton;
        button->setObjectName(names[i]);
        button->setProperty("voiceTextKey", QByteArray(keys[i]));
        button->setFocusPolicy(Qt::NoFocus);
        row->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, i]
                {
                    if (requested)
                        requested(VoiceAction(i));
                });
    }
    row->addStretch();
    connect(voices_, &QComboBox::currentIndexChanged, this,
            [this](int)
            {
                if (selected)
                    selected(voices_->currentData().toInt());
            });
    retranslate();
}
void NumberedVoicePanel::setComposition(const NumberedComposition &composition, int active)
{
    QSignalBlocker block(voices_);
    voices_->clear();
    for (const auto &voice : composition.voices)
        voices_->addItem(QString::fromStdString(voice.name), voice.id);
    voices_->setCurrentIndex(voices_->findData(active));
    findChild<QPushButton *>("voiceAdd")->setEnabled(composition.voices.size() < MaximumNumberedVoices);
    findChild<QPushButton *>("voiceRemove")->setEnabled(composition.voices.size() > 1);
    findChild<QPushButton *>("voiceUp")->setEnabled(voices_->currentIndex() > 0);
    findChild<QPushButton *>("voiceDown")->setEnabled(voices_->currentIndex() + 1 < voices_->count());
}
void NumberedVoicePanel::retranslate()
{
    voices_->setAccessibleName(trText("ui.editor.voices"));
    for (auto *button : findChildren<QPushButton *>())
        button->setText(trText(qPrintable(button->property("voiceTextKey").toString())));
}
} // namespace singlilt

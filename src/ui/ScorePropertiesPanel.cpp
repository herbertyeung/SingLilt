// Reusable score metadata and meter fields.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ScorePropertiesPanel.h"
#include "i18n/LanguageManager.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace singlilt
{
ScorePropertiesPanel::ScorePropertiesPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("scorePropertiesPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);
    auto *heading = new QLabel(trText("ui.editor.properties"));
    heading->setObjectName("editorHeading");
    heading->setProperty("_ui_text", QByteArray("ui.editor.properties"));
    layout->addWidget(heading);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setSpacing(10);
    auto field = [this, form](const char *key, const char *name)
    {
        auto *edit = new QLineEdit;
        edit->setObjectName(name);
        edit->setMaxLength(512);
        edit->setAccessibleName(trText(key));
        form->addRow(trText(key), edit);
        form->labelForField(edit)->setProperty("_ui_text", QByteArray(key));
        connect(edit, &QLineEdit::editingFinished, this,
                [this]
                {
                    if (!loading_ && edited)
                        edited();
                });
        return edit;
    };
    title_ = field("ui.editor.title", "editorTitle");
    subtitle_ = field("ui.editor.subtitle", "editorSubtitle");
    key_ = new QComboBox;
    key_->setObjectName("editorKey");
    for (int i = 0; i < 12; ++i)
    {
        const auto key = QString("ui.key.%1").arg(i).toUtf8();
        key_->addItem(trText(key.constData()), i);
        key_->setItemData(i, key, Qt::UserRole + 1);
    }
    form->addRow(trText("ui.option.key"), key_);
    form->labelForField(key_)->setProperty("_ui_text", QByteArray("ui.option.key"));
    auto *meter = new QWidget;
    auto *row = new QHBoxLayout(meter);
    row->setContentsMargins(0, 0, 0, 0);
    beats_ = new QSpinBox;
    beats_->setObjectName("editorBeats");
    beats_->setRange(1, MaximumBeatsPerBar);
    beatUnit_ = new QComboBox;
    beatUnit_->setObjectName("editorBeatUnit");
    for (int unit = 1; unit <= MaximumBeatUnit; unit *= 2)
        beatUnit_->addItem(QString::number(unit), unit);
    row->addWidget(beats_);
    row->addWidget(new QLabel("/"));
    row->addWidget(beatUnit_);
    form->addRow(trText("ui.option.meter"), meter);
    form->labelForField(meter)->setProperty("_ui_text", QByteArray("ui.option.meter"));
    tempo_ = new QDoubleSpinBox;
    tempo_->setObjectName("editorTempo");
    tempo_->setRange(MinimumScoreBpm, MaximumScoreBpm);
    tempo_->setDecimals(1);
    tempo_->setSuffix(" BPM");
    form->addRow(trText("ui.option.tempo"), tempo_);
    form->labelForField(tempo_)->setProperty("_ui_text", QByteArray("ui.option.tempo"));
    lyricist_ = field("ui.editor.lyricist", "editorLyricist");
    composer_ = field("ui.editor.composer", "editorComposer");
    layout->addLayout(form);
    auto *hint = new QLabel(trText("ui.editor.properties_hint"));
    hint->setWordWrap(true);
    hint->setProperty("_ui_text", QByteArray("ui.editor.properties_hint"));
    layout->addWidget(hint);
    layout->addStretch();
    auto changed = [this]
    {
        if (!loading_ && edited)
            edited();
    };
    connect(key_, &QComboBox::currentIndexChanged, this, changed);
    connect(beatUnit_, &QComboBox::currentIndexChanged, this, changed);
    connect(beats_, &QSpinBox::editingFinished, this, changed);
    connect(tempo_, &QDoubleSpinBox::editingFinished, this, changed);
    setScore(Score{});
}

void ScorePropertiesPanel::setScore(const Score &score)
{
    loading_ = true;
    title_->setText(QString::fromStdString(score.title));
    subtitle_->setText(QString::fromStdString(score.subtitle));
    lyricist_->setText(QString::fromStdString(score.lyricist));
    composer_->setText(QString::fromStdString(score.composer));
    key_->setCurrentIndex(key_->findData(score.tonic));
    beats_->setValue(score.beatsPerBar);
    beatUnit_->setCurrentIndex(beatUnit_->findData(score.beatUnit));
    tempo_->setValue(score.bpm);
    loading_ = false;
}

Score ScorePropertiesPanel::editedScore(const Score &base) const
{
    auto score = base;
    score.title = title_->text().trimmed().toStdString();
    if (score.title.empty())
        score.title = trText("messages.storage.untitled").toStdString();
    score.subtitle = subtitle_->text().toStdString();
    score.lyricist = lyricist_->text().toStdString();
    score.composer = composer_->text().toStdString();
    score.tonic = key_->currentData().toInt();
    score.beatsPerBar = beats_->value();
    score.beatUnit = beatUnit_->currentData().toInt();
    score.bpm = tempo_->value();
    return score;
}
} // namespace singlilt

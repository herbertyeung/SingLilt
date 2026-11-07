// Accessible three-octave pitch-entry keyboard.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "PianoKeyboard.h"
#include "i18n/LanguageManager.h"
#include <QPushButton>
#include <QResizeEvent>

namespace singlilt
{
PianoKeyboard::PianoKeyboard(QWidget *parent) : QWidget(parent)
{
    setObjectName("pianoKeyboard");
    setMinimumHeight(100);
    setMaximumHeight(124);
    for (int octave = -1; octave <= 1; ++octave)
        for (int degree = 1; degree <= 7; ++degree)
        {
            const int index = (octave + 1) * 7 + degree - 1;
            auto makeKey = [this, octave, degree, index](bool black)
            {
                const QString registerText = trText(octave < 0   ? "ui.editor.low"
                                                    : octave > 0 ? "ui.editor.high"
                                                                 : "ui.editor.middle");
                const QString text = (black ? QStringLiteral("♯") : QString()) + QString::number(degree);
                auto *key = new QPushButton(text + (black ? QString() : "\n" + registerText), this);
                key->setObjectName(QString("piano_%1_%2_%3").arg(octave).arg(degree).arg(black ? 1 : 0));
                key->setProperty("pianoBlack", black);
                key->setAccessibleName(registerText + " " + text);
                key->setToolTip(registerText + " " + text);
                connect(key, &QPushButton::clicked, this,
                        [this, octave, degree, black]
                        {
                            if (noteRequested)
                                noteRequested(degree, octave, black ? 1 : 0);
                        });
                keys_.push_back({key, index, black});
            };
            makeKey(false);
            if (degree != 3 && degree != 7)
                makeKey(true);
        }
    for (const auto &key : keys_)
        if (key.black)
            key.button->raise();
}
void PianoKeyboard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const double width = this->width() / 21.0;
    for (const auto &key : keys_)
    {
        if (key.black)
            key.button->setGeometry(int((key.whiteIndex + .67) * width), 0, int(width * .64), height() * 3 / 5);
        else
            key.button->setGeometry(int(key.whiteIndex * width), 0, int(width) + 1, height());
    }
}
void PianoKeyboard::retranslate()
{
    for (const auto &key : keys_)
    {
        const QString registerText = trText(key.whiteIndex < 7    ? "ui.editor.low"
                                            : key.whiteIndex < 14 ? "ui.editor.middle"
                                                                  : "ui.editor.high");
        const QString text =
            (key.black ? QStringLiteral("♯") : QString()) + QString::number(key.whiteIndex % 7 + 1);
        key.button->setText(text + (key.black ? QString() : "\n" + registerText));
        key.button->setAccessibleName(registerText + " " + text);
        key.button->setToolTip(registerText + " " + text);
    }
}
} // namespace singlilt

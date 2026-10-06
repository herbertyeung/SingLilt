// Windows OCR text and bounding-box extraction.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace singlilt
{
struct OcrWord
{
    QString text;
    QRectF box;
    int line = 0;
};
struct OcrText
{
    QVector<OcrWord> words;
    QString text;
    QString language;
    QString error;
};
// Creates a private MTA worker, so callers need not manage a WinRT apartment.
OcrText recognizeWindowsText(const QImage &image, const QString &language = QStringLiteral("zh-Hans-CN"));
} // namespace singlilt

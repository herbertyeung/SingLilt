// Tempo-marking extraction from staff-image OCR.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QImage>
#include <QRect>
#include <QString>
#include <optional>

namespace singlilt
{
struct StaffTempoSuggestion
{
    std::optional<double> quarterBpm;
    bool needsConfirmation = true;
    QString status = QStringLiteral("unknown");
    QString ocrText;
    QString error;
    QRect sourceRect;
};

QImage makeStaffInputCanvas(const QImage &image);
StaffTempoSuggestion parseQuarterTempoText(const QString &text);
StaffTempoSuggestion recognizeStaffTempo(const QImage &image);
} // namespace singlilt

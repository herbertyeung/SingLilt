// Tempo-marking extraction from staff-image OCR.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffTempoRecognizer.h"
#include "WindowsOcr.h"
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <vector>

namespace singlilt
{
namespace
{
int firstStaffTop(const QImage &image)
{
    const auto gray = image.convertToFormat(QImage::Format_Grayscale8);
    const int left = gray.width() / 20;
    const int right = gray.width() - left;
    std::vector<int> rows;
    int start = -1;
    for (int y = 0; y <= gray.height(); ++y)
    {
        int ink = 0;
        if (y < gray.height())
            for (int x = left; x < right; ++x)
                ink += gray.constScanLine(y)[x] < 190;
        const bool line = right > left && ink >= (right - left) * 0.60;
        if (line && start < 0)
            start = y;
        if (!line && start >= 0)
        {
            if (y - start <= 6)
                rows.push_back((start + y - 1) / 2);
            start = -1;
        }
    }
    for (std::size_t first = 0; first < rows.size(); ++first)
        for (std::size_t second = first + 1; second < rows.size(); ++second)
        {
            const int spacing = rows[second] - rows[first];
            if (spacing > 80)
                break;
            if (spacing < 2)
                continue;
            const int tolerance = std::max(1, spacing / 5);
            bool complete = true;
            for (int line = 2; line < 5; ++line)
            {
                const int target = rows[first] + line * spacing;
                const auto found = std::lower_bound(rows.begin(), rows.end(), target - tolerance);
                if (found == rows.end() || *found > target + tolerance)
                {
                    complete = false;
                    break;
                }
            }
            if (complete)
                return rows[first];
        }
    return -1;
}
} // namespace

QImage makeStaffInputCanvas(const QImage &image)
{
    if (image.isNull())
        return {};
    if (!image.hasAlphaChannel())
        return image.convertToFormat(QImage::Format_RGB32);
    QImage canvas(image.size(), QImage::Format_RGB32);
    if (canvas.isNull())
        return {};
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    painter.drawImage(0, 0, image);
    return canvas;
}

StaffTempoSuggestion parseQuarterTempoText(const QString &text)
{
    StaffTempoSuggestion result;
    result.ocrText = text.left(4096);
    const QRegularExpression unsupportedUnit(
        QStringLiteral("dotted\\s+quarter|[♩𝅘𝅥]\\s*[.·•]|quarter(?:\\s+note)?\\s*[.·•]"),
        QRegularExpression::CaseInsensitiveOption);
    if (unsupportedUnit.match(text).hasMatch())
    {
        result.status = QStringLiteral("unsupported-beat-unit");
        return result;
    }
    // A number or an OCR guess such as "J=90" does not establish its beat unit.
    const QRegularExpression explicitQuarter(
        QStringLiteral("(?:[♩𝅘𝅥]|quarter(?:\\s+note)?|四分音符)\\s*[=＝]\\s*(\\d{2,3})(?![\\d.])"),
        QRegularExpression::CaseInsensitiveOption);
    auto matches = explicitQuarter.globalMatch(text);
    std::optional<double> candidate;
    while (matches.hasNext())
    {
        const double bpm = matches.next().captured(1).toDouble();
        if (bpm < 10 || bpm > 400)
        {
            result.status = QStringLiteral("out-of-range");
            return result;
        }
        if (candidate && *candidate != bpm)
        {
            result.status = QStringLiteral("conflicting");
            return result;
        }
        candidate = bpm;
    }
    if (candidate)
    {
        result.quarterBpm = candidate;
        result.status = QStringLiteral("quarter-suggestion");
    }
    return result;
}

StaffTempoSuggestion recognizeStaffTempo(const QImage &image)
{
    StaffTempoSuggestion result;
    const auto canvas = makeStaffInputCanvas(image);
    if (canvas.isNull())
    {
        result.status = QStringLiteral("invalid-image");
        return result;
    }
    const auto sample = canvas.width() > 1600 ? canvas.scaledToWidth(1600, Qt::SmoothTransformation) : canvas;
    const int staffTop = firstStaffTop(sample);
    if (staffTop <= 0)
    {
        result.status = QStringLiteral("header-not-located");
        return result;
    }
    const double scale = double(canvas.width()) / sample.width();
    const int sourceBottom = std::min(canvas.height(), int(std::floor(staffTop * scale)));
    result.sourceRect = QRect(0, 0, canvas.width(), sourceBottom);
    auto header = canvas.copy(result.sourceRect);
    // The bounded header stays above the first staff; credit/page numbers are never bare tempo candidates.
    if (header.width() < 2000)
        header = header.scaledToWidth(2000, Qt::SmoothTransformation);
    const auto ocr = recognizeWindowsText(header, QStringLiteral("en-US"));
    const auto rectangle = result.sourceRect;
    result = parseQuarterTempoText(ocr.text);
    result.sourceRect = rectangle;
    result.error = ocr.error;
    if (!ocr.error.isEmpty())
    {
        result.quarterBpm.reset();
        result.status = QStringLiteral("ocr-error");
    }
    return result;
}
} // namespace singlilt

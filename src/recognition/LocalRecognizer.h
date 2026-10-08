// Local numbered-notation recognition from score images.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/Score.h"
#include "domain/StaffPerformance.h"
#include <QImage>
#include <QStringList>

namespace singlilt
{
struct RecognitionResult
{
    Score score;
    QStringList warnings;
    QString debugText;
    bool staffNotation = false;
    bool staffBass = false;
    int staffKeyFifths = 0;
    bool staffMinor = false;
    std::optional<StaffPerformance> staffPerformance;
};
class LocalRecognizer
{
  public:
    // Recognizes printed numbered notation, including brace-linked hands and image anchors.
    // Confidence below 0.8 marks a note for manual review.
    static RecognitionResult recognize(const QImage &image, const QString &imagePath);
};
} // namespace singlilt

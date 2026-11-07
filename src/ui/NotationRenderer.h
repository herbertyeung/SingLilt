// Numbered-notation images and note layout.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/Score.h"
#include "domain/NumberedComposition.h"

#include <QImage>
#include <QRectF>

namespace singlilt
{
// Monotonic diagnostics counters; no rendering decisions depend on them.
std::uint64_t numberedRasterizationCount();
void recordNumberedRasterization();
struct NumberedInputSlot
{
    QRectF bounds;
    std::int64_t tick = 0;
    int voiceId = 0;
};
struct NumberedNoteHit
{
    QRectF bounds;
    int voiceId = 0;
    int noteIndex = -1;
};
struct NumberedSpacingPoint
{
    std::int64_t tick = 0;
    double left = 0, right = 0, position = 0;
};
struct NumberedMeasureGrid
{
    double width = 0;
    std::vector<NumberedSpacingPoint> points;
};
struct NumberedVoiceLabel
{
    QRectF bounds;
    int voiceId = 0;
};
struct NumberedScoreLayout
{
    std::vector<NumberedInputSlot> slots;
    std::vector<QRectF> measures;
    QRectF addBars;
    std::vector<QRectF> systems;
    std::vector<NumberedNoteHit> notes;
    std::vector<NumberedMeasureGrid> grid;
    std::vector<NumberedVoiceLabel> voiceLabels;
};

// Draws notation and replaces only source anchors, line and cumulative measure.
// Throws std::runtime_error on invalid notation or an oversized single-image layout.
QImage renderNumberedScore(Score &score, NumberedScoreLayout *layout = nullptr,
                           const std::vector<NumberedMeasureGrid> *sharedGrid = nullptr);
NumberedScoreLayout layoutNumberedScore(Score &score);
QImage renderNumberedComposition(Score &score, NumberedComposition &composition, int activeVoice,
                                 NumberedScoreLayout &layout);

} // namespace singlilt

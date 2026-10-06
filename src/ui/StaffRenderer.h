// Staff notation layout, glyphs, and note positions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/Score.h"

#include <QFont>
#include <QImage>
#include <vector>

namespace singlilt
{

struct StaffPerformance;
QFont staffMusicFont(int pixelSize);

struct StaffRenderOptions
{
    bool bassClef = false;
    int keyFifths = 0;
    bool minor = false;
};

// Keeps the musical material intact and replaces only display anchors, line and measure.
// Uses the bundled SMuFL font; invalid scores and oversized layouts throw std::runtime_error.
QImage renderStaffScore(Score &score, const StaffRenderOptions &options = {});

// Original-staff notes retain explicit beams/stems on a shared written-time axis.
// The guide receives matching click anchors.
QImage renderGrandStaffScore(Score &score, StaffPerformance &performance, const StaffRenderOptions &options = {});

std::vector<QImage> renderGrandStaffPages(Score &score, StaffPerformance &performance,
                                          const StaffRenderOptions &options = {});

} // namespace singlilt

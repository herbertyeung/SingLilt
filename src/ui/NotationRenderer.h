// Numbered-notation images and note layout.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/Score.h"

#include <QImage>

namespace singlilt
{

// Draws notation and replaces only source anchors, line and cumulative measure.
// Throws std::runtime_error on invalid notation or an oversized single-image layout.
QImage renderNumberedScore(Score &score);

} // namespace singlilt

// Source-page images and identity for staff recognition.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QImage>
#include <QRect>
#include <QString>

namespace singlilt
{
struct StaffPageInput
{
    QImage image;
    QString label;
    int sourceIndex = 0;
    QRect sourceRect;
};
} // namespace singlilt

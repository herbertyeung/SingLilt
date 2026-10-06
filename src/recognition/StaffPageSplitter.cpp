// Scan splitting and source-page ordering.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffPageSplitter.h"
#include "LocalStaffRecognizer.h"
#include <algorithm>
#include <cmath>

namespace singlilt
{
namespace
{
int clearGutter(const QImage &image)
{
    const auto sample = (image.width() > 1600 ? image.scaledToWidth(1600, Qt::SmoothTransformation) : image)
                            .convertToFormat(QImage::Format_Grayscale8);
    const int left = sample.width() * 37 / 100;
    const int right = sample.width() * 63 / 100;
    const int top = sample.height() / 20;
    const int bottom = sample.height() - top;
    int start = -1;
    int bestStart = -1;
    int bestEnd = -1;
    double bestDistance = sample.width();
    for (int x = left; x <= right; ++x)
    {
        int ink = 0;
        if (x < right)
            for (int y = top; y < bottom; ++y)
                ink += sample.constScanLine(y)[x] < 210;
        const bool white = x < right && ink <= std::max(1, (bottom - top) / 250);
        if (white && start == -1)
            start = x;
        if (!white && start != -1)
        {
            const double center = (start + x - 1) / 2.0;
            const double distance = std::abs(center - sample.width() / 2.0);
            if (x - start >= std::max(8, sample.width() / 80) && distance < bestDistance)
            {
                bestStart = start;
                bestEnd = x;
                bestDistance = distance;
            }
            start = -1;
        }
    }
    if (bestStart < 0)
        return -1;
    return int(std::lround((bestStart + bestEnd) / 2.0 * image.width() / sample.width()));
}
} // namespace

std::vector<StaffPageInput> splitStaffPageInputs(const QImage &image, const QString &label, int sourceIndex,
                                                 bool autoDetect)
{
    return splitStaffPageInputs(image, label, sourceIndex,
                                autoDetect ? StaffPageSplitMode::Auto : StaffPageSplitMode::Single);
}

std::vector<StaffPageInput> splitStaffPageInputs(const QImage &image, const QString &label, int sourceIndex,
                                                 StaffPageSplitMode mode)
{
    if (image.isNull())
        return {};
    const auto whole = [&] { return std::vector<StaffPageInput>{{image, label, sourceIndex, image.rect()}}; };
    if (image.width() < 2)
        return whole();
    if (mode == StaffPageSplitMode::Single)
        return whole();
    if (mode == StaffPageSplitMode::Auto && (image.width() < 400 || image.width() < image.height() * 1.15))
        return whole();
    int split = clearGutter(image);
    if (split <= 0 || split >= image.width())
    {
        if (mode == StaffPageSplitMode::Auto)
            return whole();
        split = image.width() / 2;
    }
    const QRect left(0, 0, split, image.height());
    const QRect right(split, 0, image.width() - split, image.height());
    const auto leftImage = image.copy(left);
    const auto rightImage = image.copy(right);
    if (mode == StaffPageSplitMode::Auto && (!hasStaffLines(leftImage) || !hasStaffLines(rightImage)))
        return whole();
    return {{leftImage, label + " [L]", sourceIndex, left}, {rightImage, label + " [R]", sourceIndex, right}};
}
} // namespace singlilt

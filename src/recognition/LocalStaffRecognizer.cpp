// Local staff recognition and candidate construction.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "LocalStaffRecognizer.h"
#include "CrispStaffRecognizer.h"
#include "StaffTempoRecognizer.h"
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace singlilt
{
bool LocalStaffRecognitionResult::valid() const
{
    return !cancelled && error.isEmpty() && project && project->staffPerformance &&
           !project->score.notes.empty() && !project->image.isNull() &&
           (project->staffPages.empty() || project->staffPages.size() == originalImages.size());
}

bool hasStaffLines(const QImage &image)
{
    if (image.isNull() || image.width() < 120 || image.height() < 40)
        return false;
    const auto canvas = makeStaffInputCanvas(image);
    const QImage sample = (canvas.width() > 1400 ? canvas.scaledToWidth(1400, Qt::SmoothTransformation) : canvas)
                              .convertToFormat(QImage::Format_Grayscale8);
    const int left = sample.width() / 20;
    const int right = sample.width() - left;
    struct Rule
    {
        double center;
        int thickness;
    };
    std::vector<Rule> rules;
    int first = -1;
    for (int y = 0; y <= sample.height(); ++y)
    {
        int ink = 0;
        if (y < sample.height())
            for (int x = left; x < right; ++x)
                ink += sample.constScanLine(y)[x] < 190;
        const bool horizontalRule = ink >= (right - left) * 0.60;
        if (horizontalRule && first == -1)
            first = y;
        if (!horizontalRule && first != -1)
        {
            rules.push_back({(first + y - 1) / 2.0, y - first});
            first = -1;
        }
    }
    for (std::size_t firstRule = 0; firstRule < rules.size(); ++firstRule)
        for (std::size_t second = firstRule + 1; second < rules.size(); ++second)
        {
            const double spacing = rules[second].center - rules[firstRule].center;
            if (spacing > 80)
                break;
            if (spacing < 2)
                continue;
            const double tolerance = std::max(1.5, spacing * 0.20);
            bool complete = true;
            for (int line = 0; line < 5; ++line)
            {
                const double target = rules[firstRule].center + line * spacing;
                const auto matching =
                    std::lower_bound(rules.begin(), rules.end(), target - tolerance,
                                     [](const Rule &rule, double position) { return rule.center < position; });
                if (matching == rules.end() || matching->center > target + tolerance ||
                    matching->thickness > std::max(3.0, spacing * 0.35))
                {
                    complete = false;
                    break;
                }
            }
            if (complete)
                return true;
        }
    return false;
}

LocalStaffRecognitionResult recognizeLocalStaff(const QImage &image, const QString &imagePath,
                                                const LocalStaffRecognitionOptions &options,
                                                const std::atomic_bool *cancellation)
{
    return recognizeCrispStaffPages({StaffPageInput{image, QFileInfo(imagePath).fileName(), 0, image.rect()}},
                                    options, cancellation);
}

LocalStaffRecognitionResult recognizeLocalStaffPages(const std::vector<StaffPageInput> &pages,
                                                     const LocalStaffRecognitionOptions &options,
                                                     const std::atomic_bool *cancellation)
{
    return recognizeCrispStaffPages(pages, options, cancellation);
}
} // namespace singlilt

// Source-image anchors for native staff-recognition results.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/MusicXmlImporter.h"
#include <QImage>
#include <QRect>
#include <QRectF>
#include <array>
#include <atomic>

namespace singlilt
{
struct CrispStaffAnchorReport
{
    int anchoredNotes = 0;
    int unanchoredNotes = 0;
    int anchoredGuideNotes = 0;
    int unanchoredGuideNotes = 0;
    QStringList warnings;
    QString error;
    int detectedSystems = 0;
    // At least one unique anchor on each staff; this is not a complete-page accuracy claim.
    int coveredSystems = 0;
    int detectedNoteheads = 0;
    int unmatchedDetectedNoteheads = 0;
};

// Return real paired-staff regions in original pixels, never a uniform page split.
std::vector<QRect> crispStaffSystemRegions(const QImage &image);

struct StaffImageLines
{
    int systemIndex = 0;
    int staff = 1;
    std::array<double, 5> linePositions{};
    QRectF bounds;
    double spacing = 0;
};

std::vector<StaffImageLines> crispStaffImageLines(const QImage &image, int staffCount = 2);

// Image coordinates come from detected ink; ambiguous sequence matches stay unanchored.
CrispStaffAnchorReport applyCrispStaffSourceAnchors(const std::vector<QImage> &pages,
                                                    const MusicXmlImportResult &imported, Score &score,
                                                    StaffPerformance &performance,
                                                    const std::atomic_bool *cancellation = nullptr);
} // namespace singlilt

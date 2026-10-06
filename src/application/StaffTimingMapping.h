// In-measure timing estimates from reliable source anchors.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "recognition/CrispStaffSourceAnchors.h"
#include "storage/ProjectStore.h"
#include <QPointF>
#include <optional>

namespace singlilt
{
struct StaffTimingHint
{
    std::int64_t startTick = 0;
    std::int64_t durationTicks = TicksPerQuarter;
    int measureIndex = -1;
    int referenceIndex = -1;
    bool sameBeat = false;
    double alignedX = 0;
    QString explanation;
};

std::optional<StaffTimingHint> suggestStaffTimingAt(const Project &project, int page,
                                                    const StaffImageLines &selected,
                                                    const std::vector<StaffImageLines> &staves, QPointF position,
                                                    int nearestIndex, std::optional<int> sameBeatReference = {});

std::optional<StaffTimingHint> suggestStaffTimingMove(const Project &project, int performanceIndex,
                                                      QPointF position);
} // namespace singlilt

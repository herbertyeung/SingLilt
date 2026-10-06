// Scan splitting and source-page ordering.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "StaffPageInput.h"
#include <vector>

namespace singlilt
{
enum class StaffPageSplitMode
{
    Auto,
    Single,
    LeftRight
};

std::vector<StaffPageInput> splitStaffPageInputs(const QImage &image, const QString &label, int sourceIndex,
                                                 bool autoDetect = true);
std::vector<StaffPageInput> splitStaffPageInputs(const QImage &image, const QString &label, int sourceIndex,
                                                 StaffPageSplitMode mode);
} // namespace singlilt

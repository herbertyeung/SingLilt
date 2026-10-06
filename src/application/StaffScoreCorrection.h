// Full-voice score corrections and shared timeline updates.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/ProjectStore.h"
#include <cstdint>
#include <vector>

namespace singlilt
{
// UI values are quarter-note beats, regardless of the local time-signature denominator.
std::int64_t staffCorrectionTicks(double quarterBeats);
// Notes use absolute ticks on the edited measure clock; the input project is never modified.
// Measure order/page identity are retained; starts are recalculated from the supplied durations.
Project correctedStaffProject(const Project &original, std::vector<StaffPerformanceNote> notes,
                              std::vector<WrittenMeasure> measures);
} // namespace singlilt

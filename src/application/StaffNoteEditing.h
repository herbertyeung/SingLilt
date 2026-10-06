// Add, remove, and update complete staff-note events.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/ProjectStore.h"

namespace singlilt
{
// Both operations preserve original pixels and rebuild the guide; performance indexes may change.
Project addedStaffNote(const Project &original, StaffPerformanceNote note);
Project deletedStaffNote(const Project &original, int performanceIndex);
Project updatedStaffNote(const Project &original, int performanceIndex, StaffPerformanceNote note);
} // namespace singlilt

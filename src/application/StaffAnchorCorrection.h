// Source-image anchor rebinding for corrected staff notes.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/ProjectStore.h"

namespace singlilt
{
// The box uses original-image pixels on the selected performance note's existing page.
// Only positioning and unambiguous existing guide aliases change; music and pixels do not.
Project correctedStaffAnchor(const Project &original, int performanceIndex, const SourceRect &source);
} // namespace singlilt

// Native CrispEmbed invocation and notation result extraction.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "LocalStaffRecognizer.h"

namespace singlilt
{
LocalStaffRecognitionResult recognizeCrispStaffPages(const std::vector<StaffPageInput> &pages,
                                                     const LocalStaffRecognitionOptions &options,
                                                     const std::atomic_bool *cancellation = nullptr);
} // namespace singlilt

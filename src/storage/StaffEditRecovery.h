// Independent recovery snapshots for staff-image edits.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace singlilt
{
struct Project;

// Saves an independent recovery package; the caller owns edit timing and UI error feedback.
QString saveStaffEditRecovery(const Project &project, const QString &sourcePath, const QString &sessionId,
                              const QString &directory = {});
QString latestStaffEditRecovery(const QString &directory = {});
} // namespace singlilt

// One-time import of preferences and records from the previous application identity.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

class QSettings;

namespace singlilt
{
void migrateLegacyState(QSettings &legacySettings, QSettings &settings, const QString &legacyDataDirectory,
                        const QString &dataDirectory);
void migrateLegacyUserState();
} // namespace singlilt

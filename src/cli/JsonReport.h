// Atomic JSON report output with checked writes.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

class QString;
class QJsonObject;

namespace singlilt
{
void writeJsonReport(const QString &path, const QJsonObject &report);
}

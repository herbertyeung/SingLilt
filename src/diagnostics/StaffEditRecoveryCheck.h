// Staff edit snapshot and recovery-package checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
class QApplication;
class QCommandLineParser;

namespace singlilt
{
QJsonObject checkStaffEditRecovery();
void runStaffEditRecoveryCheck(const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

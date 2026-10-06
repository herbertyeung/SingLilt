// Settings changes and optional-component validation checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

class QApplication;
class QCommandLineParser;
namespace singlilt
{
void runSettingsProductCheck(const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

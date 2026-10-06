// Staff-note source-anchor correction regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

class QApplication;
class QCommandLineParser;

namespace singlilt
{
class MainWindow;
void runStaffAnchorCorrectionCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

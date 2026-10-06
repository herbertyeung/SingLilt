// WAV export range, timing, and failure-handling checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

class QApplication;
class QCommandLineParser;

namespace singlilt
{
class MainWindow;
void runWaveExportCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

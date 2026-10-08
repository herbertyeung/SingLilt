// Asynchronous original-audio cancellation regressions.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
class QApplication;
class QCommandLineParser;
namespace singlilt
{
class MainWindow;
void runAudioCancellationCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

// GUI transport, screenshot, and project round-trip smoke checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
class QApplication;
class QCommandLineParser;
namespace singlilt
{
class MainWindow;
// Exercises the same Qt controls as the user, waiting for asynchronous loading.
void runGuiSmoke(MainWindow &window, const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

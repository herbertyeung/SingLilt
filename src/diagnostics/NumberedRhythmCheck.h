// Complete rhythm-entry workflows, not just toolbar presence.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT
#pragma once
class QApplication;
class QCommandLineParser;
namespace singlilt
{
class MainWindow;
void runNumberedRhythmCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

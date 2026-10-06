// Workspace identity, editing modes, and draft-state checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
class QApplication;
class QCommandLineParser;
namespace singlilt
{
class MainWindow;
class LanguageManager;
void runProductWorkspaceCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                              QApplication &app);
} // namespace singlilt

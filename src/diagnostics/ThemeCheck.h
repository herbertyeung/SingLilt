// Appearance selection, persistence, system changes, and display-only regressions.
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
class ThemeManager;
void runThemeCheck(MainWindow &window, LanguageManager &languages, ThemeManager &themes,
                   const QCommandLineParser &args, QApplication &application);
} // namespace singlilt

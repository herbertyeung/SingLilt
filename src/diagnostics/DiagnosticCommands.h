// Diagnostic dispatch and isolated test preferences.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QStringList>
#include <optional>
class QApplication;
class QCommandLineParser;

namespace singlilt
{
class LanguageManager;
class ThemeManager;
class MainWindow;
bool configureDiagnosticSettings(const QStringList &arguments);
std::optional<int> runCoreDiagnostics(const QCommandLineParser &args, LanguageManager &languages);
void runWindowDiagnostics(MainWindow &window, LanguageManager &languages, ThemeManager &themes,
                          const QCommandLineParser &args, QApplication &application);
} // namespace singlilt

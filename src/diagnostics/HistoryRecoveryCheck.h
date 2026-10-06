// Practice-history write failure and recovery checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

class QApplication;
class QCommandLineParser;

namespace singlilt
{
class LanguageManager;
class MainWindow;
void runHistoryRecoveryCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                             QApplication &app);
} // namespace singlilt

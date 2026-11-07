// Regression cases from the numbered-authoring code review.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT
#pragma once
class QApplication;
class QCommandLineParser;
namespace singlilt
{
class MainWindow;
void runNumberedReviewCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

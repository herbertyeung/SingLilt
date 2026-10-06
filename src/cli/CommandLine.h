// Public command-line options and early command detection.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QStringList>
class QCommandLineParser;

namespace singlilt
{
bool hasCommandLineOption(const QStringList &arguments, const QString &name);
bool isHeadlessCommand(const QStringList &arguments);
void addCommandLineOptions(QCommandLineParser &args);
} // namespace singlilt

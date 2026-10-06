// kern/bekern decoding and timing-validation regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
class QApplication;
class QCommandLineParser;

namespace singlilt
{
QJsonObject checkBekernDecoder();
void runBekernDecoderCheck(const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

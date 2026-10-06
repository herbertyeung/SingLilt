// Staff-layout, glyph, and source-position regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

class QApplication;
class QCommandLineParser;

namespace singlilt
{
void runStaffRendererCheck(const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

// Full-voice staff playback and instrument-routing checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

class QApplication;
class QCommandLineParser;

namespace singlilt
{
void runStaffPlaybackCheck(const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

// Recognition, transcription, inspection, and export commands.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
class QCommandLineParser;

namespace singlilt
{
// nullopt means no project command was requested.
std::optional<int> runProjectCommand(const QCommandLineParser &args);
} // namespace singlilt

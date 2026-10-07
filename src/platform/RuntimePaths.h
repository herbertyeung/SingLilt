// Bundled tool layout; persisted settings can still specify explicit paths.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QtGlobal>

namespace singlilt
{
#ifdef Q_OS_WIN
inline constexpr char NativeExecutableSuffix[] = ".exe";
inline constexpr char SeparatorPythonPath[] = "/tools/separation/python/python.exe";
#else
inline constexpr char NativeExecutableSuffix[] = "";
inline constexpr char SeparatorPythonPath[] = "/tools/separation/python/bin/python3";
#endif
} // namespace singlilt

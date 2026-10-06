// Bundled lesson loading and course validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/SingingLesson.h"
#include <QString>

namespace singlilt
{
// Returns a complete validated catalog or throws with the failing file's path.
std::vector<SingingLesson> loadSingingLessons(const QString &directory, const QString &locale);
} // namespace singlilt

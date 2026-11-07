// Validated, copy-based composition structure edits.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#pragma once
#include "domain/NumberedComposition.h"
namespace singlilt
{
int addNumberedVoice(NumberedComposition &composition, std::string name);
void removeNumberedVoice(NumberedComposition &composition, int id);
void moveNumberedVoice(NumberedComposition &composition, int id, int direction);
void editCompositionMeasure(Score &metadata, NumberedComposition &composition, int measure, bool remove);
} // namespace singlilt

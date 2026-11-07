// Versioned authored-voice JSON payload.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#pragma once
#include "domain/NumberedComposition.h"
#include <QJsonObject>
namespace singlilt
{
QJsonObject numberedCompositionToJson(const Score &metadata, const NumberedComposition &composition);
NumberedComposition numberedCompositionFromJson(const Score &metadata, const QJsonObject &object);
} // namespace singlilt

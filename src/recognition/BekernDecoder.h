// kern/bekern decoding into complete staff-note events.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/MusicXmlImporter.h"

namespace singlilt
{
struct BekernDecodeOptions
{
    bool pageFragment = false;
    bool preserveGraceAsReviewAnnotation = false;
    bool reviewRhythmConflicts = false;
    // Later systems inherit only an already validated preceding system's attributes.
    std::optional<MusicXmlAttributes> initialAttributes;
};

// Accepts Humdrum records or SMT's space-separated <b>/<t>/<s> token stream.
MusicXmlImportResult decodeBekern(const QString &notation, const QString &sourceName = {},
                                  const BekernDecodeOptions &options = {});
} // namespace singlilt

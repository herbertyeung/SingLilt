// Pitch suggestions from staff lines, clefs, and accidentals.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "storage/ProjectStore.h"
#include <QPointF>
#include <array>
#include <optional>

namespace singlilt
{
struct StaffPositionHint
{
    StaffPerformanceNote note;
    int nearestNoteIndex = -1;
    int measureIndex = -1;
    int system = -1;
    double lineSpacing = 0;
    std::array<double, 5> staffLines{};
    QString explanation;
    bool timingSuggested = false;
    bool sameBeat = false;
    int timingReferenceIndex = -1;
};

// New positions use local written-clock calibration; aligned columns can suggest a same-beat chord.
std::optional<StaffPositionHint> suggestStaffNoteAt(const Project &project, int page, QPointF position,
                                                    std::optional<int> sameBeatReference = {});
// A drag keeps the existing event's staff, system, voice and written clock.
std::optional<StaffPositionHint> suggestStaffNotePosition(const Project &project,
                                                          const StaffPerformanceNote &existing, QPointF position);
std::optional<StaffSpelling> staffSpellingForPitch(const Project &project, const StaffPerformanceNote &existing,
                                                   int midi);
// Numeric pitch edits preserve x/width/height and time; only the original-image y position is recalculated.
std::optional<SourceRect> staffAnchorForPitch(const Project &project, const StaffPerformanceNote &existing,
                                              int newMidi, std::optional<StaffSpelling> spelling = {});
} // namespace singlilt

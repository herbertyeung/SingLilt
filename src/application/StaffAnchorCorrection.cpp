// Source-image anchor rebinding for corrected staff notes.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffAnchorCorrection.h"

#include "i18n/LanguageManager.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace singlilt
{
namespace
{
void requireAnchor(bool valid, const char *key)
{
    if (!valid)
        throw std::runtime_error(trText(key).toStdString());
}

bool sameBox(const SourceRect &left, const SourceRect &right)
{
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}
} // namespace

Project correctedStaffAnchor(const Project &original, int performanceIndex, const SourceRect &source)
{
    requireAnchor(original.staffImagePlayback && original.staffPerformance && performanceIndex >= 0 &&
                      std::size_t(performanceIndex) < original.staffPerformance->notes.size(),
                  "messages.staff.invalid_performance");
    projectToJson(original);
    const auto &performance = *original.staffPerformance;
    const auto &selected = performance.notes[std::size_t(performanceIndex)];
    requireAnchor(selected.pageIndex >= 0 && std::size_t(selected.pageIndex) < original.staffPages.size(),
                  "messages.pages.invalid_project");
    const auto &image = original.staffPages[std::size_t(selected.pageIndex)].sourceImage;
    requireAnchor(std::isfinite(source.x) && std::isfinite(source.y) && std::isfinite(source.width) &&
                      std::isfinite(source.height) && source.x >= 0 && source.y >= 0 && source.width > 0 &&
                      source.height > 0 && source.x + source.width <= image.width() &&
                      source.y + source.height <= image.height(),
                  "messages.storage.invalid_rectangle");

    // A shared unison box is not sufficient evidence of which printed note owns a guide slice.
    std::vector<std::pair<std::int64_t, std::int64_t>> otherRanges;
    for (std::size_t index = 0; index < performance.notes.size(); ++index)
    {
        const auto &other = performance.notes[index];
        if (index != std::size_t(performanceIndex) && other.hasImageAnchor &&
            other.pageIndex == selected.pageIndex && other.midiPitch == selected.midiPitch &&
            sameBox(other.source, selected.source))
            otherRanges.emplace_back(other.startTick, other.startTick + other.durationTicks);
    }
    std::sort(otherRanges.begin(), otherRanges.end());
    std::int64_t latestEnd = 0;
    for (auto &range : otherRanges)
    {
        latestEnd = std::max(latestEnd, range.second);
        range.second = latestEnd;
    }

    Project corrected = original;
    std::int64_t startTick = 0;
    int tonic = original.score.tonic;
    const int soundingPitch = selected.midiPitch + original.score.tonic - performance.sourceTonic;
    for (std::size_t index = 0; index < corrected.score.notes.size(); ++index)
    {
        auto &guide = corrected.score.notes[index];
        const auto endTick = startTick + guide.durationTicks;
        if (guide.keyOverride >= 0)
            tonic = guide.keyOverride;
        if (selected.hasImageAnchor && selected.sourceNoteIndex >= 0 &&
            index >= std::size_t(selected.sourceNoteIndex) && guide.hasImageAnchor &&
            guide.pageIndex == selected.pageIndex && sameBox(guide.source, selected.source) &&
            startTick >= selected.startTick && endTick <= selected.startTick + selected.durationTicks &&
            guide.degree != 0 && midiPitch(guide, tonic) == soundingPitch)
        {
            const auto after =
                std::upper_bound(otherRanges.begin(), otherRanges.end(), startTick,
                                 [](std::int64_t tick, const auto &range) { return tick < range.first; });
            const bool ambiguous = after != otherRanges.begin() && std::prev(after)->second >= endTick;
            if (!ambiguous)
                guide.source = source;
        }
        startTick = endTick;
    }
    auto &note = corrected.staffPerformance->notes[std::size_t(performanceIndex)];
    note.source = source;
    note.hasImageAnchor = true;

    requireAnchor(!original.processing.contains("manualImageAnchors") ||
                      original.processing.value("manualImageAnchors").isArray(),
                  "messages.package.invalid_metadata");
    QJsonArray edits;
    for (const auto &entry : original.processing.value("manualImageAnchors").toArray())
    {
        requireAnchor(entry.isObject(), "messages.package.invalid_metadata");
        if (entry.toObject().value("performanceIndex").toInt(-1) != performanceIndex)
            edits.append(entry);
    }
    edits.append(QJsonObject{{"performanceIndex", performanceIndex},
                             {"pageIndex", selected.pageIndex},
                             {"source", QJsonArray{source.x, source.y, source.width, source.height}}});
    corrected.processing.insert("manualImageAnchors", edits);
    projectToJson(corrected);
    return corrected;
}
} // namespace singlilt

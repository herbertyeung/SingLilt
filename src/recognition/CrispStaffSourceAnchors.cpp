// Source-image anchors for native staff-recognition results.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "CrispStaffSourceAnchors.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace singlilt
{
namespace
{
constexpr int MaximumAlignmentCells = 2000000;
constexpr int MaximumDetectedHeads = 4000;

void checkCancellation(const std::atomic_bool *cancellation)
{
    if (cancellation && cancellation->load(std::memory_order_relaxed))
        throw std::runtime_error("Native source image matching cancelled.");
}

struct BinaryPage
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> ink;

    bool black(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < width && y < height && ink[static_cast<std::size_t>(y) * width + x] != 0;
    }
};

BinaryPage binarize(const QImage &source, const std::atomic_bool *cancellation)
{
    if (source.isNull() || source.width() > 16000 || source.height() > 16000 ||
        static_cast<qint64>(source.width()) * source.height() > 40000000)
        throw std::runtime_error("Native source image dimensions exceed the supported work limit.");
    const auto image = source.convertToFormat(QImage::Format_Grayscale8);
    BinaryPage page{image.width(), image.height(), {}};
    page.ink.resize(static_cast<std::size_t>(page.width) * page.height);
    for (int y = 0; y < page.height; ++y)
    {
        checkCancellation(cancellation);
        const auto *row = image.constScanLine(y);
        for (int x = 0; x < page.width; ++x)
            page.ink[static_cast<std::size_t>(y) * page.width + x] = row[x] < 170;
    }
    return page;
}

struct ImageStaff
{
    std::array<double, 5> lines{};
    double spacing = 0;
    double thickness = 1;
    int left = 0;
    int right = 0;
    double topLimit = 0;
    double bottomLimit = 0;
};

std::vector<ImageStaff> findStaves(const BinaryPage &page, const std::atomic_bool *cancellation)
{
    std::vector<std::pair<double, int>> lines;
    int first = -1;
    for (int y = 0; y <= page.height; ++y)
    {
        checkCancellation(cancellation);
        const int count =
            y == page.height ? 0
                             : std::accumulate(page.ink.begin() + static_cast<std::size_t>(y) * page.width,
                                               page.ink.begin() + static_cast<std::size_t>(y + 1) * page.width, 0);
        if (count >= page.width * 0.35)
        {
            if (first < 0)
                first = y;
        }
        else if (first >= 0)
        {
            lines.emplace_back((first + y - 1) / 2.0, y - first);
            first = -1;
        }
    }
    std::vector<ImageStaff> staves;
    for (std::size_t index = 0; index + 4 < lines.size();)
    {
        const double spacing = (lines[index + 4].first - lines[index].first) / 4;
        bool regular = spacing >= 5 && spacing <= 100;
        for (std::size_t offset = 1; offset < 5; ++offset)
            regular = regular && std::abs(lines[index + offset].first - lines[index + offset - 1].first -
                                          spacing) <= std::max(1.5, spacing * 0.10);
        if (!regular)
        {
            ++index;
            continue;
        }
        ImageStaff staff;
        staff.spacing = spacing;
        staff.left = page.width;
        for (std::size_t offset = 0; offset < 5; ++offset)
        {
            staff.lines[offset] = lines[index + offset].first;
            staff.thickness = std::max(staff.thickness, double(lines[index + offset].second));
        }
        for (int x = 0; x < page.width; ++x)
        {
            int hits = 0;
            for (const double line : staff.lines)
                hits += page.black(x, static_cast<int>(std::round(line)));
            if (hits >= 4)
            {
                staff.left = std::min(staff.left, x);
                staff.right = x;
            }
        }
        if (staff.right - staff.left >= page.width * 0.3 && staff.thickness < spacing * 0.25)
            staves.push_back(staff);
        index += 5;
    }
    for (std::size_t index = 0; index < staves.size(); ++index)
    {
        staves[index].topLimit =
            index == 0 ? 0 : (staves[index - 1].lines.back() + staves[index].lines.front()) / 2;
        staves[index].bottomLimit = index + 1 == staves.size()
                                        ? page.height
                                        : (staves[index].lines.back() + staves[index + 1].lines.front()) / 2;
    }
    return staves;
}

struct DetectedHead
{
    SourceRect bounds;
    double x = 0;
    double y = 0;
    int step = 0;
    bool hollow = false;
    double confidence = 0;
    int system = 0;
};

bool linePixel(const ImageStaff &staff, int y)
{
    // Ledger lines share the staff's spacing. Do not let them turn a hollow head into a filled one.
    const double distance = (staff.lines.back() - y) / staff.spacing;
    return std::abs(distance - std::round(distance)) * staff.spacing <= staff.thickness / 2 + 0.5;
}

DetectedHead measureHead(const BinaryPage &page, const ImageStaff &staff, double x, double y, int step,
                         double width, double height, bool horizontal)
{
    const double cosine = horizontal ? 1 : 0.9396926208;
    const double sine = horizontal ? 0 : -0.3420201433;
    const double a = width * staff.spacing;
    const double b = height * staff.spacing;
    int core = 0, coreBlack = 0, interior = 0, interiorBlack = 0, rim = 0, rimBlack = 0;
    int outside = 0, outsideBlack = 0;
    std::array<int, 4> rimSections{}, blackRimSections{};
    int left = page.width, right = -1, top = page.height, bottom = -1;
    for (int py = static_cast<int>(std::floor(y - staff.spacing)); py <= y + staff.spacing; ++py)
        for (int px = static_cast<int>(std::floor(x - staff.spacing)); px <= x + staff.spacing; ++px)
        {
            if (linePixel(staff, py))
                continue;
            const double dx = px - x, dy = py - y;
            const double u = (dx * cosine + dy * sine) / a;
            const double v = (-dx * sine + dy * cosine) / b;
            const double radius = u * u + v * v;
            const bool black = page.black(px, py);
            if (radius <= 0.20)
            {
                ++core;
                coreBlack += black;
            }
            if (radius <= 0.65)
            {
                ++interior;
                interiorBlack += black;
            }
            if (radius >= 0.60 && radius <= 1.25)
            {
                ++rim;
                rimBlack += black;
                const std::size_t section = (u >= 0 ? 1 : 0) + (v >= 0 ? 2 : 0);
                ++rimSections[section];
                blackRimSections[section] += black;
            }
            const bool stemStrip = std::abs(dx) >= a * 0.70 && std::abs(dx) <= a * 1.12 && std::abs(dy) > b * 0.5;
            if (radius >= 1.25 && radius <= 2.05 && !stemStrip)
            {
                ++outside;
                outsideBlack += black;
            }
            if (black && radius <= 1.12)
            {
                left = std::min(left, px);
                right = std::max(right, px);
                top = std::min(top, py);
                bottom = std::max(bottom, py);
            }
        }
    DetectedHead head;
    if (core < 3 || interior < 8 || rim < 8 || outside < 8 || right < left || bottom < top)
        return head;
    const double filled = double(interiorBlack) / interior;
    const double ring = double(rimBlack) / rim;
    const double hole = 1 - double(coreBlack) / core;
    const double clear = 1 - double(outsideBlack) / outside;
    if (clear < 0.75)
        return head;
    if (filled >= 0.83)
        head.confidence = filled * clear;
    else if (ring >= 0.42 && hole >= 0.78 &&
             std::equal(rimSections.begin(), rimSections.end(), blackRimSections.begin(),
                        [](int pixels, int black) { return pixels >= 2 && black >= pixels * 0.35; }))
    {
        head.hollow = true;
        head.confidence = ring * hole * clear;
    }
    else
        return head;
    head.x = x;
    head.y = y;
    head.step = step;
    head.bounds = {double(left), double(top), double(right - left + 1), double(bottom - top + 1)};
    return head;
}

std::vector<DetectedHead> findHeads(const BinaryPage &page, const ImageStaff &staff,
                                    const std::atomic_bool *cancellation)
{
    std::vector<DetectedHead> detected;
    for (int step = -8; step <= 20; ++step)
    {
        checkCancellation(cancellation);
        const double y = staff.lines.back() - step * staff.spacing / 2;
        if (y - staff.spacing * 0.5 < staff.topLimit || y + staff.spacing * 0.5 > staff.bottomLimit)
            continue;
        for (const double offset : {-0.20, 0.20, -0.32, 0.32})
        {
            const int scanY = static_cast<int>(std::round(y + offset * staff.spacing));
            std::vector<std::pair<int, int>> spans;
            for (int x = staff.left; x < staff.right;)
            {
                if (!page.black(x, scanY))
                {
                    ++x;
                    continue;
                }
                const int start = x;
                while (x < staff.right && page.black(x, scanY))
                    ++x;
                spans.emplace_back(start, x);
            }
            std::vector<double> centers;
            for (std::size_t index = 0; index < spans.size(); ++index)
            {
                const auto [start, end] = spans[index];
                const double length = end - start;
                if (length >= staff.spacing * 0.40 && length <= staff.spacing * 1.75)
                    centers.push_back((start + end - 1) / 2.0);
                if (index + 1 >= spans.size())
                    continue;
                const auto [nextStart, nextEnd] = spans[index + 1];
                const double nextLength = nextEnd - nextStart;
                const double hole = nextStart - end;
                const double width = nextEnd - start;
                // Thin hollow rims can leave two short ink runs instead of one head-width run.
                if (length >= 2 && nextLength >= 2 && length <= staff.spacing * 0.65 &&
                    nextLength <= staff.spacing * 0.65 && hole >= staff.spacing * 0.15 && hole <= staff.spacing &&
                    width >= staff.spacing * 0.8 && width <= staff.spacing * 1.75)
                    centers.push_back((start + nextEnd - 1) / 2.0);
            }
            for (const double center : centers)
            {
                DetectedHead best;
                for (const double shift : {-0.15, 0.0, 0.15})
                    for (const double dy : {-0.08, 0.0, 0.08})
                        for (const auto &size : {std::tuple{0.62, 0.36, false}, std::tuple{0.68, 0.40, false},
                                                 std::tuple{0.62, 0.44, false}, std::tuple{0.73, 0.42, true}})
                        {
                            const auto candidate =
                                measureHead(page, staff, center + shift * staff.spacing, y + dy * staff.spacing,
                                            step, std::get<0>(size), std::get<1>(size), std::get<2>(size));
                            if (candidate.confidence > best.confidence)
                                best = candidate;
                        }
                if (best.confidence > 0)
                    detected.push_back(best);
                if (detected.size() > MaximumDetectedHeads)
                    throw std::runtime_error("Native source image contains too many notehead candidates.");
            }
        }
    }
    std::sort(detected.begin(), detected.end(),
              [](const auto &a, const auto &b) { return a.confidence > b.confidence; });
    std::vector<DetectedHead> unique;
    for (const auto &head : detected)
        if (std::none_of(unique.begin(), unique.end(),
                         [&](const auto &other)
                         {
                             return std::abs(head.x - other.x) < staff.spacing * 0.75 &&
                                    std::abs(head.y - other.y) < staff.spacing * 0.35;
                         }))
            unique.push_back(head);
    std::sort(unique.begin(), unique.end(), [](const auto &a, const auto &b) { return a.x < b.x; });
    return unique;
}

struct HeadGroup
{
    std::vector<DetectedHead> heads;
};

std::vector<HeadGroup> groupHeads(const std::vector<DetectedHead> &heads, double spacing)
{
    std::vector<HeadGroup> groups;
    for (const auto &head : heads)
    {
        if (groups.empty() || head.x - groups.back().heads.front().x > spacing * 0.8)
            groups.push_back({});
        groups.back().heads.push_back(head);
    }
    for (auto &group : groups)
        std::sort(group.heads.begin(), group.heads.end(),
                  [](const auto &a, const auto &b) { return a.step < b.step; });
    return groups;
}

using EventKey = std::tuple<std::int64_t, std::int64_t, int, std::string, int>;
EventKey eventKey(const StaffPerformanceNote &note)
{
    return {note.startTick, note.durationTicks, note.staff, note.voice, note.midiPitch};
}

struct ExpectedHead
{
    EventKey key;
    int step = 0;
    bool hollow = false;
};

struct ExpectedGroup
{
    std::vector<ExpectedHead> heads;
};

int diatonicStep(const QString &step)
{
    const int value = QStringLiteral("CDEFGAB").indexOf(step);
    if (step.size() != 1 || value < 0)
        throw std::runtime_error("Native source matching requires written pitch spelling.");
    return value;
}

std::optional<int> bottomLine(const MusicXmlPart &part, int staff, std::int64_t tick)
{
    MusicXmlClef clef;
    clef.staff = staff;
    if (staff == 2)
    {
        clef.sign = "F";
        clef.line = 4;
    }
    for (const auto &attributes : part.attributes)
        if (attributes.startTick <= tick)
            for (const auto &entry : attributes.clefs)
                if (entry.staff == staff)
                    clef = entry;
    const int reference = clef.sign == "G"   ? 4 * 7 + 4
                          : clef.sign == "F" ? 3 * 7 + 3
                          : clef.sign == "C" ? 4 * 7
                                             : -1;
    if (reference < 0 || clef.line < 1 || clef.line > 5)
        return std::nullopt;
    return reference - (clef.line - 1) * 2 + clef.octaveChange * 7;
}

std::map<std::pair<int, int>, std::vector<ExpectedGroup>> expectedGroups(const MusicXmlImportResult &imported)
{
    std::map<std::pair<std::size_t, int>, int> staffNumbers;
    for (std::size_t part = 0; part < imported.parts.size(); ++part)
        for (int staff = 1; staff <= imported.parts[part].staffCount; ++staff)
            staffNumbers.emplace(std::pair{part, staff}, 0);
    for (const auto &track : imported.tracks)
        staffNumbers.emplace(std::pair{track.partIndex, track.staff}, 0);
    int nextStaff = 0;
    for (auto &entry : staffNumbers)
        entry.second = ++nextStaff;
    std::map<std::pair<int, int>, std::map<std::int64_t, ExpectedGroup>> timed;
    for (const auto &track : imported.tracks)
    {
        if (track.partIndex >= imported.parts.size())
            throw std::runtime_error("Native source matching encountered an invalid part index.");
        const int staff = staffNumbers.at({track.partIndex, track.staff});
        for (const auto &event : track.events)
        {
            if (event.rest)
                continue;
            const auto bottom = bottomLine(imported.parts[track.partIndex], track.staff, event.startTick);
            if (!bottom)
                continue;
            const EventKey key{event.startTick, event.endTick - event.startTick, staff,
                               (track.partId + ':' + track.voice).toStdString(), event.midiPitch};
            const bool hollow = event.type == "half" || event.type == "whole" || event.type == "breve" ||
                                (event.type.isEmpty() && event.endTick - event.startTick >= 2 * TicksPerQuarter);
            timed[{event.pageIndex, staff}][event.startTick].heads.push_back(
                {key, event.octave * 7 + diatonicStep(event.step) - *bottom, hollow});
        }
    }
    std::map<std::pair<int, int>, std::vector<ExpectedGroup>> groups;
    for (auto &[pageStaff, sequence] : timed)
        for (auto &[tick, group] : sequence)
        {
            std::sort(group.heads.begin(), group.heads.end(),
                      [](const auto &a, const auto &b) { return a.step < b.step; });
            groups[pageStaff].push_back(std::move(group));
        }
    return groups;
}

bool matches(const ExpectedGroup &expected, const HeadGroup &actual)
{
    if (expected.heads.size() != actual.heads.size())
        return false;
    for (std::size_t index = 0; index < expected.heads.size(); ++index)
    {
        // Pixel pitch cannot disambiguate two simultaneous voices on the same written line.
        if (index > 0 && expected.heads[index].step == expected.heads[index - 1].step)
            return false;
        if (expected.heads[index].step != actual.heads[index].step ||
            expected.heads[index].hollow != actual.heads[index].hollow)
            return false;
    }
    return true;
}

void alignUnique(const std::vector<ExpectedGroup> &expected, const std::vector<HeadGroup> &actual,
                 const std::map<EventKey, std::vector<std::size_t>> &indices, StaffPerformance &performance,
                 int page, std::vector<bool> &coveredSystems, int &matchedHeads,
                 const std::atomic_bool *cancellation)
{
    const std::size_t rows = expected.size() + 1, columns = actual.size() + 1;
    if (rows > MaximumAlignmentCells / columns)
        throw std::runtime_error("Native source image alignment exceeds its work limit.");
    std::vector<int> forward(rows * columns), backward(rows * columns);
    const auto at = [&](std::size_t i, std::size_t j) { return i * columns + j; };
    for (std::size_t i = 0; i < rows; ++i)
        forward[at(i, 0)] = static_cast<int>(i);
    for (std::size_t j = 0; j < columns; ++j)
        forward[at(0, j)] = static_cast<int>(j);
    for (std::size_t i = 1; i < rows; ++i)
    {
        checkCancellation(cancellation);
        for (std::size_t j = 1; j < columns; ++j)
        {
            int distance = std::min(forward[at(i - 1, j)], forward[at(i, j - 1)]) + 1;
            if (matches(expected[i - 1], actual[j - 1]))
                distance = std::min(distance, forward[at(i - 1, j - 1)]);
            forward[at(i, j)] = distance;
        }
    }
    for (std::size_t i = 0; i < rows; ++i)
        backward[at(i, columns - 1)] = static_cast<int>(rows - i - 1);
    for (std::size_t j = 0; j < columns; ++j)
        backward[at(rows - 1, j)] = static_cast<int>(columns - j - 1);
    for (std::size_t i = rows - 1; i-- > 0;)
    {
        checkCancellation(cancellation);
        for (std::size_t j = columns - 1; j-- > 0;)
        {
            int distance = std::min(backward[at(i + 1, j)], backward[at(i, j + 1)]) + 1;
            if (matches(expected[i], actual[j]))
                distance = std::min(distance, backward[at(i + 1, j + 1)]);
            backward[at(i, j)] = distance;
        }
    }
    const int optimum = forward.back();
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        checkCancellation(cancellation);
        int matched = -1;
        bool ambiguous = false;
        for (std::size_t j = 0; j < columns; ++j)
        {
            if (forward[at(i, j)] + 1 + backward[at(i + 1, j)] == optimum)
                ambiguous = true;
            if (j < actual.size() && matches(expected[i], actual[j]) &&
                forward[at(i, j)] + backward[at(i + 1, j + 1)] == optimum)
            {
                if (matched >= 0)
                    ambiguous = true;
                matched = static_cast<int>(j);
            }
        }
        if (ambiguous || matched < 0)
            continue;
        for (std::size_t head = 0; head < expected[i].heads.size(); ++head)
        {
            const auto found = indices.find(expected[i].heads[head].key);
            if (found == indices.end() || found->second.size() != 1)
                continue;
            auto &note = performance.notes[found->second.front()];
            if (note.pageIndex != page)
                continue;
            note.source = actual[matched].heads[head].bounds;
            note.hasImageAnchor = true;
            coveredSystems[static_cast<std::size_t>(actual[matched].heads[head].system * performance.staffCount +
                                                    note.staff - 1)] = true;
            ++matchedHeads;
        }
    }
}

void clearAnchors(Score &score, StaffPerformance &performance)
{
    for (auto &note : score.notes)
    {
        note.source = {};
        note.hasImageAnchor = false;
    }
    for (auto &note : performance.notes)
    {
        note.source = {};
        note.hasImageAnchor = false;
    }
}

void mapGuide(Score &score, const StaffPerformance &performance, const std::atomic_bool *cancellation)
{
    std::vector<std::int64_t> starts(score.notes.size() + 1, 0);
    std::vector<int> pitches(score.notes.size(), -1);
    int tonic = score.tonic;
    for (std::size_t index = 0; index < score.notes.size(); ++index)
    {
        const auto &note = score.notes[index];
        if (note.keyOverride >= 0)
            tonic = note.keyOverride;
        starts[index + 1] = starts[index] + note.durationTicks;
        if (note.degree != 0)
            pitches[index] = midiPitch(note, tonic);
    }
    std::vector<int> counts(score.notes.size(), 0);
    int comparisons = 0;
    for (const auto &full : performance.notes)
    {
        checkCancellation(cancellation);
        if (!full.hasImageAnchor || full.sourceNoteIndex < 0)
            continue;
        for (std::size_t index = static_cast<std::size_t>(full.sourceNoteIndex);
             index < score.notes.size() && starts[index] < full.startTick + full.durationTicks; ++index)
        {
            if (++comparisons > 20000000)
                throw std::runtime_error("Native source guide matching exceeds its work limit.");
            if (starts[index] < full.startTick || starts[index + 1] > full.startTick + full.durationTicks ||
                pitches[index] != full.midiPitch || score.notes[index].pageIndex != full.pageIndex)
                continue;
            ++counts[index];
            score.notes[index].source = full.source;
            score.notes[index].hasImageAnchor = true;
        }
    }
    for (std::size_t index = 0; index < counts.size(); ++index)
        if (counts[index] > 1)
        {
            score.notes[index].source = {};
            score.notes[index].hasImageAnchor = false;
        }
}
} // namespace

std::vector<StaffImageLines> crispStaffImageLines(const QImage &image, int staffCount)
{
    std::vector<StaffImageLines> lines;
    if (staffCount < 1 || staffCount > 2)
        return {};
    try
    {
        const auto page = binarize(image, nullptr);
        const auto staves = findStaves(page, nullptr);
        if (staves.empty() || staves.size() % staffCount != 0)
            return {};
        if (staffCount == 2)
            for (std::size_t index = 0; index < staves.size(); index += 2)
            {
                const auto &upper = staves[index];
                const auto &lower = staves[index + 1];
                const double spacing = std::max(upper.spacing, lower.spacing);
                const double gap = lower.lines.front() - upper.lines.back();
                const int overlap = std::min(upper.right, lower.right) - std::max(upper.left, lower.left);
                if (std::abs(upper.spacing - lower.spacing) > spacing * 0.15 || gap < spacing * 1.5 ||
                    gap > spacing * 12 ||
                    overlap < std::min(upper.right - upper.left, lower.right - lower.left) * 0.7)
                    return {};
            }
        for (std::size_t index = 0; index < staves.size(); ++index)
        {
            const auto &staff = staves[index];
            const QRectF bounds(staff.left, staff.topLimit, staff.right - staff.left + 1,
                                staff.bottomLimit - staff.topLimit);
            lines.push_back({static_cast<int>(index / staffCount), static_cast<int>(index % staffCount + 1),
                             staff.lines, bounds, staff.spacing});
        }
    }
    catch (const std::exception &)
    {
        return {};
    }
    return lines;
}

std::vector<QRect> crispStaffSystemRegions(const QImage &image)
{
    std::vector<QRect> regions;
    try
    {
        const auto page = binarize(image, nullptr);
        const auto staves = findStaves(page, nullptr);
        if (staves.empty() || staves.size() % 2 != 0)
            return {};
        for (std::size_t index = 0; index < staves.size(); index += 2)
        {
            const auto &upper = staves[index];
            const auto &lower = staves[index + 1];
            const double spacing = std::max(upper.spacing, lower.spacing);
            const double gap = lower.lines.front() - upper.lines.back();
            const int overlap = std::min(upper.right, lower.right) - std::max(upper.left, lower.left);
            if (std::abs(upper.spacing - lower.spacing) > spacing * 0.15 || gap < spacing * 1.5 ||
                gap > spacing * 12 || overlap < std::min(upper.right - upper.left, lower.right - lower.left) * 0.7)
                return {};
            const int left = static_cast<int>(std::floor(std::min(upper.left, lower.left) - spacing * 3));
            const int right = static_cast<int>(std::ceil(std::max(upper.right, lower.right) + spacing * 1.5));
            const int top =
                static_cast<int>(std::floor(std::max(upper.topLimit, upper.lines.front() - spacing * 4)));
            const int bottom =
                static_cast<int>(std::ceil(std::min(lower.bottomLimit, lower.lines.back() + spacing * 4)));
            const auto region = QRect(QPoint(left, top), QPoint(right, bottom)).intersected(image.rect());
            if (region.isEmpty())
                return {};
            regions.push_back(region);
        }
    }
    catch (const std::exception &)
    {
        return {};
    }
    return regions;
}

CrispStaffAnchorReport applyCrispStaffSourceAnchors(const std::vector<QImage> &pages,
                                                    const MusicXmlImportResult &imported, Score &score,
                                                    StaffPerformance &performance,
                                                    const std::atomic_bool *cancellation)
{
    CrispStaffAnchorReport report;
    int matchedHeads = 0;
    clearAnchors(score, performance);
    try
    {
        checkCancellation(cancellation);
        if (pages.empty() || pages.size() > 32 || performance.staffCount < 1 || performance.staffCount > 2)
            throw std::runtime_error("Native source matching requires one or two staves and 1-32 source pages.");
        const auto expected = expectedGroups(imported);
        std::map<EventKey, std::vector<std::size_t>> indices;
        for (std::size_t index = 0; index < performance.notes.size(); ++index)
            indices[eventKey(performance.notes[index])].push_back(index);
        for (std::size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
        {
            const auto page = binarize(pages[pageIndex], cancellation);
            const auto staves = findStaves(page, cancellation);
            if (staves.empty() || staves.size() % performance.staffCount != 0)
            {
                report.warnings.append(
                    QString("Page %1 has no unambiguous complete staff layout.").arg(pageIndex + 1));
                continue;
            }
            std::vector<std::vector<HeadGroup>> sequences(static_cast<std::size_t>(performance.staffCount));
            std::vector<bool> coveredSystems(staves.size(), false);
            report.detectedSystems += static_cast<int>(staves.size() / performance.staffCount);
            for (std::size_t staff = 0; staff < staves.size(); ++staff)
            {
                auto heads = findHeads(page, staves[staff], cancellation);
                report.detectedNoteheads += static_cast<int>(heads.size());
                for (auto &head : heads)
                    head.system = static_cast<int>(staff / performance.staffCount);
                auto groups = groupHeads(heads, staves[staff].spacing);
                auto &sequence = sequences[staff % performance.staffCount];
                sequence.insert(sequence.end(), std::make_move_iterator(groups.begin()),
                                std::make_move_iterator(groups.end()));
            }
            for (int staff = 1; staff <= performance.staffCount; ++staff)
            {
                const auto found = expected.find({static_cast<int>(pageIndex), staff});
                if (found != expected.end())
                    alignUnique(found->second, sequences[static_cast<std::size_t>(staff - 1)], indices,
                                performance, static_cast<int>(pageIndex), coveredSystems, matchedHeads,
                                cancellation);
            }
            for (std::size_t system = 0; system < staves.size() / performance.staffCount; ++system)
            {
                const auto first = coveredSystems.begin() + system * performance.staffCount;
                if (std::all_of(first, first + performance.staffCount, [](bool covered) { return covered; }))
                    ++report.coveredSystems;
            }
        }
        mapGuide(score, performance, cancellation);
    }
    catch (const std::exception &error)
    {
        clearAnchors(score, performance);
        matchedHeads = 0;
        report.coveredSystems = 0;
        report.error = QString::fromUtf8(error.what());
        report.warnings.append(report.error);
    }
    for (const auto &note : performance.notes)
        note.hasImageAnchor ? ++report.anchoredNotes : ++report.unanchoredNotes;
    for (const auto &note : score.notes)
        note.hasImageAnchor ? ++report.anchoredGuideNotes : ++report.unanchoredGuideNotes;
    report.unmatchedDetectedNoteheads = report.detectedNoteheads - matchedHeads;
    if (report.unanchoredNotes > 0)
        report.warnings.append(QString("%1 of %2 recognized notes have no unique detected original-image head. "
                                       "They remain audible without a fabricated highlight.")
                                   .arg(report.unanchoredNotes)
                                   .arg(performance.notes.size()));
    return report;
}
} // namespace singlilt

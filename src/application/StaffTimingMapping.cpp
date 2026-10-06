// In-measure timing estimates from reliable source anchors.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffTimingMapping.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace singlilt
{
namespace
{
constexpr std::int64_t TimingGrid = 60;

struct Anchor
{
    const StaffPerformanceNote *note = nullptr;
    int index = -1;
    int measure = -1;
    double x = 0;
    bool calibration = true;
    bool chord = true;
};

struct Column
{
    double x = 0;
    std::int64_t tick = 0;
    const Anchor *reference = nullptr;
};

QJsonArray boxJson(const SourceRect &box)
{
    return {box.x, box.y, box.width, box.height};
}

std::optional<int> measureAt(const Project &project, const StaffPerformanceNote &note)
{
    const auto &measures = project.score.writtenMeasures;
    const auto after = std::upper_bound(measures.begin(), measures.end(), note.startTick,
                                        [](std::int64_t tick, const WrittenMeasure &measure)
                                        { return tick < measure.startTick; });
    if (after == measures.begin())
        return {};
    const auto index = std::size_t(std::distance(measures.begin(), after) - 1);
    const auto &measure = measures[index];
    if (measure.pageIndex != note.pageIndex || note.durationTicks <= 0 || measure.durationTicks < TimingGrid ||
        note.startTick < measure.startTick || note.startTick - measure.startTick >= measure.durationTicks ||
        note.durationTicks > measure.startTick + measure.durationTicks - note.startTick)
        return {};
    return static_cast<int>(index);
}

bool validBox(const QImage &image, const StaffPerformanceNote &note)
{
    const auto &box = note.source;
    return note.hasImageAnchor && std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) &&
           std::isfinite(box.height) && box.x >= 0 && box.y >= 0 && box.width > 0 && box.height > 0 &&
           box.x + box.width <= image.width() && box.y + box.height <= image.height();
}

void editEligibility(const Project &project, Anchor &anchor)
{
    const auto &note = *anchor.note;
    for (const auto &value : project.processing.value("staffVisualEdits").toArray())
    {
        const auto edit = value.toObject();
        if (edit.value("pageIndex").toInt(-1) != note.pageIndex || edit.value("staff").toInt() != note.staff ||
            edit.value("voice").toString().toStdString() != note.voice ||
            edit.value("startTick").toDouble(-1) != note.startTick ||
            edit.value("midiPitch").toInt(-1) != note.midiPitch ||
            edit.value("source").toArray() != boxJson(note.source))
            continue;
        const auto origin = edit.value("timingSource").toString();
        if (origin == "distance" || origin == "chord")
            anchor.calibration = false;
        else if (edit.value("originalSource").toArray().isEmpty() && origin != "manual")
            anchor.calibration = anchor.chord = false;
    }
}

std::vector<Anchor> anchorsInSystem(const Project &project, int page, const QImage &image,
                                    const StaffImageLines &selected, const std::vector<StaffImageLines> &staves,
                                    int excludedIndex = -1)
{
    std::vector<Anchor> anchors;
    for (std::size_t index = 0; index < project.staffPerformance->notes.size(); ++index)
    {
        const auto &note = project.staffPerformance->notes[index];
        if (static_cast<int>(index) == excludedIndex || note.pageIndex != page || !validBox(image, note))
            continue;
        const QPointF center(note.source.x + note.source.width / 2, note.source.y + note.source.height / 2);
        const bool inSystem = std::any_of(staves.begin(), staves.end(),
                                          [&](const StaffImageLines &staff)
                                          {
                                              return staff.systemIndex == selected.systemIndex &&
                                                     staff.staff == note.staff && staff.bounds.contains(center);
                                          });
        const auto measure = measureAt(project, note);
        if (!inSystem || !measure)
            continue;
        Anchor anchor{&note, static_cast<int>(index), *measure, center.x()};
        editEligibility(project, anchor);
        anchors.push_back(anchor);
    }
    return anchors;
}

double columnTolerance(const Anchor &anchor, double spacing)
{
    return std::max(2.0, std::min(anchor.note->source.width, spacing) * 0.25);
}

StaffTimingHint chordHint(const Project &project, const Anchor &reference, const QString &explanation)
{
    const auto &measure = project.score.writtenMeasures[std::size_t(reference.measure)];
    StaffTimingHint hint;
    hint.startTick = reference.note->startTick;
    hint.durationTicks =
        std::min(reference.note->durationTicks, measure.startTick + measure.durationTicks - hint.startTick);
    hint.measureIndex = reference.measure;
    hint.referenceIndex = reference.index;
    hint.sameBeat = true;
    hint.alignedX = reference.x;
    hint.explanation = explanation;
    return hint;
}

// Only the current grand-staff ROI is scanned; both staff spans must carry each candidate bar stroke.
std::vector<double> barColumns(const QImage &image, const StaffImageLines &selected,
                               const std::vector<StaffImageLines> &staves)
{
    std::vector<const StaffImageLines *> pair;
    for (const auto &staff : staves)
        if (staff.systemIndex == selected.systemIndex)
            pair.push_back(&staff);
    if (pair.size() != 2)
        return {};
    std::sort(pair.begin(), pair.end(), [](const auto *left, const auto *right)
              { return left->linePositions.front() < right->linePositions.front(); });
    const int left =
        std::max(0, static_cast<int>(std::ceil(std::max(pair[0]->bounds.left(), pair[1]->bounds.left()))));
    const int right =
        std::min(image.width() - 1,
                 static_cast<int>(std::floor(std::min(pair[0]->bounds.right(), pair[1]->bounds.right()))));
    const int top = std::max(0, static_cast<int>(std::floor(pair[0]->linePositions.front())));
    const int bottom = std::min(image.height() - 1, static_cast<int>(std::ceil(pair[1]->linePositions.back())));
    if (right <= left || bottom <= top)
        return {};
    const auto raster = image.copy(QRect(left, top, right - left + 1, bottom - top + 1))
                            .convertToFormat(QImage::Format_Grayscale8);
    const auto coverage = [&](int x, int first, int last)
    {
        int ink = 0;
        int hole = 0;
        int longestHole = 0;
        for (int y = first; y <= last; ++y)
        {
            const auto *row = raster.constScanLine(y - top);
            bool black = false;
            for (int dx = -1; dx <= 1; ++dx)
                if (x + dx >= left && x + dx <= right && row[x + dx - left] < 170)
                    black = true;
            if (black)
            {
                ++ink;
                hole = 0;
            }
            else
                longestHole = std::max(longestHole, ++hole);
        }
        return std::pair{double(ink) / (last - first + 1), longestHole};
    };
    struct BarStroke
    {
        double center;
        int width;
        bool connected;
    };
    std::vector<BarStroke> strokes;
    int begin = -1;
    bool connected = false;
    for (int x = left; x <= right + 1; ++x)
    {
        bool bar = x <= right;
        for (const auto *staff : pair)
        {
            if (!bar)
                break;
            const auto stroke = coverage(x, static_cast<int>(std::floor(staff->linePositions.front())),
                                         static_cast<int>(std::ceil(staff->linePositions.back())));
            bar = stroke.first >= 0.94 && stroke.second <= std::max(2, int(std::ceil(staff->spacing * 0.12)));
        }
        if (bar && begin < 0)
        {
            begin = x;
            connected = false;
        }
        if (bar)
        {
            const int first = static_cast<int>(std::ceil(pair[0]->linePositions.back())) + 1;
            const int last = static_cast<int>(std::floor(pair[1]->linePositions.front())) - 1;
            connected |= last < first || coverage(x, first, last).first >= 0.75;
        }
        if (!bar && begin >= 0)
        {
            const double center = (begin + x - 1) / 2.0;
            if (x - begin <= selected.spacing * 0.5 && center > left + selected.spacing * 0.5 &&
                center < right - selected.spacing * 0.5)
                strokes.push_back({center, x - begin, connected});
            begin = -1;
        }
    }
    std::vector<double> bars;
    for (std::size_t first = 0; first < strokes.size();)
    {
        std::size_t last = first + 1;
        double center = strokes[first].center;
        bool reliable = strokes[first].connected || strokes[first].width - 2 >= selected.spacing * 0.2;
        while (last < strokes.size() && strokes[last].center - strokes[last - 1].center < selected.spacing * 0.8)
        {
            center += strokes[last].center;
            reliable |= strokes[last].connected || strokes[last].width - 2 >= selected.spacing * 0.2;
            ++last;
        }
        // Some grand-staff double/heavy bars stop in the inter-staff gap; a single thin stem is not a bar.
        if (reliable || last - first >= 2)
            bars.push_back(center / (last - first));
        first = last;
    }
    return bars;
}

std::optional<std::vector<Column>> columnsFor(const std::vector<const Anchor *> &anchors, double spacing)
{
    std::map<std::int64_t, std::vector<const Anchor *>> simultaneous;
    for (const auto *anchor : anchors)
        simultaneous[anchor->note->startTick].push_back(anchor);
    std::vector<Column> columns;
    for (auto &[tick, notes] : simultaneous)
    {
        std::sort(notes.begin(), notes.end(),
                  [](const auto *left, const auto *right) { return left->x < right->x; });
        if (notes.back()->x - notes.front()->x > spacing * 1.5)
            return {};
        const auto *middle = notes[notes.size() / 2];
        columns.push_back({middle->x, tick, middle});
    }
    std::sort(columns.begin(), columns.end(),
              [](const Column &left, const Column &right) { return left.x < right.x; });
    for (std::size_t index = 1; index < columns.size(); ++index)
        if (columns[index].x - columns[index - 1].x <= spacing * 0.25 ||
            columns[index].tick <= columns[index - 1].tick)
            return {};
    return columns;
}

std::int64_t suggestedDuration(std::int64_t previous, std::int64_t available)
{
    constexpr std::int64_t values[]{1920, 1440, 960, 720, 480, 360, 240, 180, 120, 60};
    if (previous >= 120 && previous <= available &&
        std::find(std::begin(values), std::end(values), previous) != std::end(values))
        return previous;
    const auto maximum = std::min<std::int64_t>(TicksPerQuarter, available);
    for (const auto duration : values)
        if (duration <= maximum)
            return duration;
    return 0;
}

struct ClockPosition
{
    double tick;
    const Anchor *reference;
};

std::optional<ClockPosition> localClockAt(const std::vector<Column> &columns, double x)
{
    if (columns.size() < 2)
        return {};
    const auto next = std::upper_bound(columns.begin(), columns.end(), x,
                                       [](double position, const Column &column) { return position < column.x; });
    const Column *previous = nullptr;
    const Column *following = nullptr;
    const Anchor *reference = nullptr;
    if (next == columns.begin())
    {
        previous = &columns[0];
        following = &columns[1];
        if (previous->x - x > following->x - previous->x)
            return {};
        reference = previous->reference;
    }
    else if (next == columns.end())
    {
        previous = &columns[columns.size() - 2];
        following = &columns.back();
        if (x - following->x > following->x - previous->x)
            return {};
        reference = following->reference;
    }
    else
    {
        previous = &*std::prev(next);
        following = &*next;
        reference = previous->reference;
    }
    const double ratio = double(following->tick - previous->tick) / (following->x - previous->x);
    return ClockPosition{previous->tick + (x - previous->x) * ratio, reference};
}
} // namespace

std::optional<StaffTimingHint> suggestStaffTimingAt(const Project &project, int page,
                                                    const StaffImageLines &selected,
                                                    const std::vector<StaffImageLines> &staves, QPointF position,
                                                    int nearestIndex, std::optional<int> sameBeatReference)
{
    if (!project.staffImagePlayback || !project.staffPerformance || page < 0 ||
        std::size_t(page) >= project.staffPages.size() || selected.spacing <= 0 || !std::isfinite(position.x()) ||
        !std::isfinite(position.y()) || !selected.bounds.contains(position) || nearestIndex < 0 ||
        std::size_t(nearestIndex) >= project.staffPerformance->notes.size())
        return {};
    const auto &image = project.staffPages[std::size_t(page)].sourceImage;
    if (image.isNull())
        return {};
    const auto anchors = anchorsInSystem(project, page, image, selected, staves);
    const auto &seed = project.staffPerformance->notes[std::size_t(nearestIndex)];
    if (sameBeatReference)
    {
        const auto found = std::find_if(anchors.begin(), anchors.end(),
                                        [&](const Anchor &anchor) { return anchor.index == *sameBeatReference; });
        if (found == anchors.end())
            return {};
        return chordHint(project, *found, "Same-beat onset and column explicitly selected by the user.");
    }

    std::vector<const Anchor *> nearby;
    bool trustedColumn = false;
    for (const auto &anchor : anchors)
        if (anchor.chord && std::abs(anchor.x - position.x()) <= columnTolerance(anchor, selected.spacing))
        {
            nearby.push_back(&anchor);
            trustedColumn |= anchor.calibration;
        }
    if (!nearby.empty())
    {
        const Anchor *reference = nullptr;
        int bestRank = std::numeric_limits<int>::max();
        for (const auto *anchor : nearby)
        {
            if (trustedColumn && !anchor->calibration)
                continue;
            if (reference && reference->note->startTick != anchor->note->startTick)
                return {};
            const int rank =
                (anchor->note->staff == selected.staff ? 0 : 2) + (anchor->note->voice == seed.voice ? 0 : 1);
            if (!reference || rank < bestRank)
            {
                reference = anchor;
                bestRank = rank;
            }
        }
        if (reference)
            return chordHint(
                project, *reference,
                "Aligned with an existing local note column; verify the inherited beat and duration.");
    }

    const auto bars = barColumns(image, selected, staves);
    double left = selected.bounds.left();
    double right = selected.bounds.right();
    for (const auto bar : bars)
    {
        if (std::abs(position.x() - bar) <= std::max(2.0, selected.spacing * 0.15))
            return {};
        if (bar < position.x())
            left = bar;
        else
        {
            right = bar;
            break;
        }
    }
    std::optional<int> physicalMeasure;
    if (!bars.empty())
        for (const auto &anchor : anchors)
            if (anchor.calibration && anchor.x > left && anchor.x < right)
            {
                if (physicalMeasure && *physicalMeasure != anchor.measure)
                    return {};
                physicalMeasure = anchor.measure;
            }
    if (!bars.empty() && !physicalMeasure)
        return {};

    using GroupKey = std::tuple<int, int, std::string>;
    std::map<GroupKey, std::vector<const Anchor *>> groups;
    for (const auto &anchor : anchors)
        if (anchor.calibration && (!physicalMeasure || anchor.measure == *physicalMeasure) &&
            (bars.empty() || (anchor.x > left && anchor.x < right)))
            groups[{anchor.measure, anchor.note->staff, anchor.note->voice}].push_back(&anchor);
    std::optional<StaffTimingHint> result;
    int resultRank = std::numeric_limits<int>::max();
    for (const auto &[identity, group] : groups)
    {
        const auto columns = columnsFor(group, selected.spacing);
        if (!columns)
            continue;
        if (columns->size() < 2)
            continue;
        const auto next = std::upper_bound(columns->begin(), columns->end(), position.x(),
                                           [](double x, const Column &column) { return x < column.x; });
        if (next == columns->begin())
            continue;
        const Column *previous = &*std::prev(next);
        const Column *origin = previous;
        const Column *following = nullptr;
        if (next == columns->end())
        {
            if (bars.empty() || position.x() - previous->x > previous->x - columns->at(columns->size() - 2).x)
                continue;
            following = previous;
            previous = &columns->at(columns->size() - 2);
        }
        else
            following = &*next;
        const int measureIndex = std::get<0>(identity);
        const auto &measure = project.score.writtenMeasures[std::size_t(measureIndex)];
        const double ratio = double(following->tick - previous->tick) / (following->x - previous->x);
        const double estimated = origin->tick + (position.x() - origin->x) * ratio;
        const auto snapped = measure.startTick +
                             std::int64_t(std::llround((estimated - measure.startTick) / TimingGrid)) * TimingGrid;
        const auto start =
            std::clamp(snapped, measure.startTick, measure.startTick + measure.durationTicks - TimingGrid);
        if (start <= origin->tick)
            continue;
        std::int64_t available = measure.startTick + measure.durationTicks - start;
        for (const auto &anchor : anchors)
            if (anchor.chord && anchor.measure == measureIndex && anchor.note->staff == selected.staff &&
                anchor.note->voice == seed.voice && anchor.note->startTick > start)
                available = std::min(available, anchor.note->startTick - start);
        const auto duration = suggestedDuration(origin->reference->note->durationTicks, available);
        if (duration < TimingGrid)
            continue;
        const int rank =
            (std::get<1>(identity) == selected.staff ? 0 : 2) + (std::get<2>(identity) == seed.voice ? 0 : 1);
        StaffTimingHint hint{
            start,
            duration,
            measureIndex,
            origin->reference->index,
            false,
            position.x(),
            "Estimated from the previous local anchor's horizontal distance; verify the beat and duration."};
        if (!result || rank < resultRank)
        {
            result = hint;
            resultRank = rank;
        }
        else if (rank == resultRank &&
                 (result->startTick != hint.startTick || result->measureIndex != hint.measureIndex))
            return {};
    }
    return result;
}

std::optional<StaffTimingHint> suggestStaffTimingMove(const Project &project, int performanceIndex,
                                                      QPointF position)
{
    if (!project.staffImagePlayback || !project.staffPerformance || performanceIndex < 0 ||
        std::size_t(performanceIndex) >= project.staffPerformance->notes.size() || !std::isfinite(position.x()) ||
        !std::isfinite(position.y()))
        return {};
    const auto &note = project.staffPerformance->notes[std::size_t(performanceIndex)];
    const auto measureIndex = measureAt(project, note);
    if (!measureIndex || note.pageIndex < 0 || std::size_t(note.pageIndex) >= project.staffPages.size())
        return {};
    const auto &image = project.staffPages[std::size_t(note.pageIndex)].sourceImage;
    if (image.isNull() || !validBox(image, note))
        return {};
    const QPointF original(note.source.x + note.source.width / 2, note.source.y + note.source.height / 2);
    const auto staves = crispStaffImageLines(image, project.staffPerformance->staffCount);
    const auto selected = std::find_if(staves.begin(), staves.end(), [&](const StaffImageLines &staff)
                                       { return staff.staff == note.staff && staff.bounds.contains(original); });
    if (selected == staves.end() || !selected->bounds.contains(position) || position.x() < note.source.width / 2 ||
        position.x() + note.source.width / 2 > image.width() || position.y() < note.source.height / 2 ||
        position.y() + note.source.height / 2 > image.height())
        return {};
    if (std::abs(position.x() - original.x()) < 0.001)
        return StaffTimingHint{note.startTick,
                               note.durationTicks,
                               *measureIndex,
                               performanceIndex,
                               false,
                               original.x(),
                               "No horizontal movement; the existing beat and duration are retained."};

    const auto bars = barColumns(image, *selected, staves);
    double left = selected->bounds.left();
    double right = selected->bounds.right();
    for (const double bar : bars)
    {
        const double tolerance = std::max(2.0, selected->spacing * 0.15);
        if (std::abs(original.x() - bar) <= tolerance || std::abs(position.x() - bar) <= tolerance ||
            (bar > std::min(original.x(), position.x()) && bar < std::max(original.x(), position.x())))
            return {};
        if (bar < original.x())
            left = bar;
        else
        {
            right = bar;
            break;
        }
    }

    const auto anchors = anchorsInSystem(project, note.pageIndex, image, *selected, staves, performanceIndex);
    using GroupKey = std::pair<int, std::string>;
    std::map<GroupKey, std::vector<const Anchor *>> groups;
    for (const auto &anchor : anchors)
    {
        if (!anchor.calibration || anchor.x <= left || anchor.x >= right)
            continue;
        if (!bars.empty() && anchor.measure != *measureIndex)
            return {};
        if (anchor.measure == *measureIndex)
            groups[{anchor.note->staff, anchor.note->voice}].push_back(&anchor);
    }
    const auto &measure = project.score.writtenMeasures[std::size_t(*measureIndex)];
    std::optional<StaffTimingHint> result;
    int resultRank = std::numeric_limits<int>::max();
    for (const auto &[identity, group] : groups)
    {
        const auto columns = columnsFor(group, selected->spacing);
        if (!columns)
            continue;
        const auto originalClock = localClockAt(*columns, original.x());
        const auto movedClock = localClockAt(*columns, position.x());
        if (!originalClock || !movedClock)
            continue;
        // Preserve the user's existing clock offset; geometry supplies only the movement delta.
        const double delta = movedClock->tick - originalClock->tick;
        const double estimated = note.startTick + std::round(delta / TimingGrid) * TimingGrid;
        const auto start = static_cast<std::int64_t>(
            std::clamp(estimated, double(measure.startTick),
                       double(measure.startTick + measure.durationTicks - note.durationTicks)));
        const int rank = (identity.first == note.staff ? 0 : 2) + (identity.second == note.voice ? 0 : 1);
        StaffTimingHint hint{start,
                             note.durationTicks,
                             *measureIndex,
                             movedClock->reference->index,
                             false,
                             position.x(),
                             "Beat movement estimated from local horizontal distance; the original duration "
                             "and measure are retained."};
        if (!result || rank < resultRank)
        {
            result = hint;
            resultRank = rank;
        }
        else if (rank == resultRank && result->startTick != hint.startTick)
            return {};
    }
    return result;
}
} // namespace singlilt

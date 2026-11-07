// Numbered-notation images and note layout.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "NotationRenderer.h"

#include "domain/Timeline.h"
#include "i18n/LanguageManager.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace singlilt
{
namespace
{

std::atomic<std::uint64_t> rasterizations{0};
constexpr int ImageWidth = 1440;
constexpr int MaximumHeight = 16000;
constexpr double Margin = 66;
constexpr double ContentWidth = ImageWidth - 2 * Margin;
constexpr double HeaderHeight = 155;
constexpr double BarPadding = 20;
const QColor Ink("#182C4C");

struct RhythmGlyph
{
    int underlines = 0;
    int dashes = 0;
    bool dotted = false;
    bool exact = true;
};

RhythmGlyph rhythmGlyph(std::int64_t duration)
{
    RhythmGlyph glyph;
    if (duration == 720 || duration == 360 || duration == 180 || duration == 90 || duration == 45)
    {
        glyph.dotted = true;
        duration = duration * 2 / 3;
    }
    if (duration >= TicksPerQuarter && duration % TicksPerQuarter == 0)
    {
        glyph.dashes = static_cast<int>(duration / TicksPerQuarter - 1);
        return glyph;
    }
    for (int subdivision = TicksPerQuarter / 2; subdivision >= 15; subdivision /= 2)
    {
        ++glyph.underlines;
        if (duration == subdivision)
            return glyph;
    }
    glyph.underlines = 0;
    glyph.exact = false;
    return glyph;
}

QString accidentalText(int accidental)
{
    return accidental > 0 ? QString(accidental, QChar(0x266f)) : QString(-accidental, QChar(0x266d));
}

struct Segment
{
    std::size_t noteIndex;
    std::int64_t duration;
    bool continuation;
    int measure;
    RhythmGlyph rhythm;
    double cellWidth = 0;
    int row = 0;
    double centerX = 0;
    double numberTop = 0;
    std::int64_t startInBar = 0;
    double leftExtent = 0, rightExtent = 0;
    int degree = 0, octave = 0, accidental = 0;
};

struct Bar
{
    std::vector<std::size_t> segments;
    double width = 2 * BarPadding;
    int row = 0;
    double left = 0;
};

struct Anchor
{
    SourceRect source;
    int line = 0;
    int measure = 0;
};

[[noreturn]] void fail(const char *key)
{
    throw std::runtime_error(trText(key).toStdString());
}

void drawArc(QPainter &painter, double firstX, double endX, double y)
{
    if (endX <= firstX)
        return;
    QPainterPath arc;
    arc.moveTo(firstX, y);
    arc.cubicTo(firstX + (endX - firstX) / 3, y - 15, firstX + (endX - firstX) * 2 / 3, y - 15, endX, y);
    painter.drawPath(arc);
}

} // namespace

std::uint64_t numberedRasterizationCount()
{
    return rasterizations.load(std::memory_order_relaxed);
}
void recordNumberedRasterization()
{
    rasterizations.fetch_add(1, std::memory_order_relaxed);
}

static QImage renderNumberedScoreImpl(Score &score, NumberedScoreLayout *layout,
                                      const std::vector<NumberedMeasureGrid> *sharedGrid, bool rasterize,
                                      const std::vector<NumberedMeasureSpan> *measures,
                                      const std::vector<QString> *labels)
{
    auto validation = score;
    if (validation.notes.empty())
        validation.notes.push_back(Note{});
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || score.editorMinimumBars < 0 ||
        score.editorMinimumBars > 128)
        fail("messages.audio_import.notation_invalid_score");
    const auto written =
        measures ? buildNumberedWrittenTimeline(std::move(validation), *measures) : buildTimeline(validation);
    if (!written.valid())
        fail("messages.audio_import.notation_invalid_score");
    if (layout)
        *layout = {};
    const int barTicks = ticksPerBar(score);
    std::int64_t totalTicks = 0;
    std::size_t lyricRows = 1;
    int aboveDots = 0;
    int belowDots = 0;
    for (const auto &note : score.notes)
    {
        totalTicks += note.durationTicks;
        lyricRows = std::max(lyricRows, lyricVerses(note).size());
        if (note.degree != 0)
        {
            aboveDots = std::max(aboveDots, note.octave);
            belowDots = std::max(belowDots, -note.octave);
        }
    }
    const auto barCount =
        measures ? std::int64_t(measures->size())
                 : std::max<std::int64_t>({1, score.editorMinimumBars, (totalTicks + barTicks - 1) / barTicks});
    const auto startOf = [&](std::size_t index)
    { return measures ? (*measures)[index].start : std::int64_t(index) * barTicks; };
    const auto endOf = [&](std::size_t index)
    { return measures ? (*measures)[index].end : (std::int64_t(index) + 1) * barTicks; };
    const auto beatOf = [&](std::size_t index)
    { return TicksPerQuarter * 4 / (measures ? (*measures)[index].unit : score.beatUnit); };
    const auto beatsOf = [&](std::size_t index)
    { return measures ? (*measures)[index].beats : score.beatsPerBar; };
    const double headerHeight =
        HeaderHeight + ((!score.subtitle.empty() || !score.lyricist.empty() || !score.composer.empty()) ? 64 : 0);
    const bool marks = std::any_of(score.notes.begin(), score.notes.end(), [](const Note &note)
                                   { return note.dynamicLevel || note.accent || note.staccato; });
    double rowHeight = 126 + 7 * (aboveDots + belowDots) + 26 * lyricRows + (marks ? 52 : 0) + (measures ? 28 : 0);
    if (HeaderHeight + std::ceil(barCount / 4.0) * rowHeight + Margin > MaximumHeight)
        fail("messages.audio_import.notation_too_large");

    QFont numberFont("Times New Roman");
    numberFont.setPixelSize(42);
    numberFont.setWeight(QFont::Bold);
    QFont lyricFont("Microsoft YaHei UI");
    lyricFont.setPixelSize(19);
    QFont smallFont("Microsoft YaHei UI");
    smallFont.setPixelSize(17);
    const QFontMetricsF numbers(numberFont);
    const QFontMetricsF lyrics(lyricFont);
    const QFontMetricsF small(smallFont);

    std::vector<Bar> bars(static_cast<std::size_t>(barCount));
    std::vector<Segment> segments;
    std::vector<Anchor> anchors(score.notes.size());
    std::vector<int> sourcePitches;
    sourcePitches.reserve(score.notes.size());
    std::int64_t tick = 0;
    int key = score.tonic;
    const int initialDots = aboveDots + belowDots;
    int maximumUnderlines = 0;
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        const auto &note = score.notes[i];
        if (note.keyOverride >= 0)
            key = note.keyOverride;
        if (measures)
            key = (*measures)[numberedMeasureAt(*measures, tick)].tonic;
        sourcePitches.push_back(midiPitch(note, key));
        std::int64_t remaining = note.durationTicks;
        bool continuation = false;
        while (remaining > 0)
        {
            const int measure = measures ? int(numberedMeasureAt(*measures, tick)) : int(tick / barTicks);
            const auto duration = std::min(remaining, endOf(measure) - tick);
            Segment segment{i, duration, continuation, measure, rhythmGlyph(duration)};
            segment.degree = note.degree;
            segment.octave = note.octave;
            segment.accidental = note.accidental;
            if (measures && continuation && note.degree && key != (*measures)[measure].tonic)
            {
                // A continuation keeps its sounding pitch; spell it in the new movable-do key.
                const int tonic = (*measures)[measure].tonic;
                bool spelled = false;
                for (int alter : {0, 1, -1})
                {
                    for (int degree = 1; degree <= 7 && !spelled; ++degree)
                        for (int octave = -6; octave <= 6; ++octave)
                        {
                            Note candidate;
                            candidate.degree = degree;
                            candidate.octave = octave;
                            candidate.accidental = alter;
                            if (midiPitch(candidate, tonic) == sourcePitches.back() && sourcePitches.back() >= 0)
                            {
                                segment.degree = degree;
                                segment.octave = octave;
                                segment.accidental = alter;
                                spelled = true;
                                break;
                            }
                        }
                    if (spelled)
                        break;
                }
                if (!spelled)
                    fail("messages.domain.pitch_range");
                aboveDots = std::max(aboveDots, segment.octave);
                belowDots = std::max(belowDots, -segment.octave);
            }
            segment.startInBar = tick - startOf(measure);
            maximumUnderlines = std::max(maximumUnderlines, segment.rhythm.underlines);
            double lyricWidth = 0;
            if (!continuation)
            {
                for (const auto &verse : lyricVerses(note))
                    lyricWidth = std::max(lyricWidth, lyrics.horizontalAdvance(QString::fromStdString(verse)));
            }
            const double rhythmWidth = segment.rhythm.dashes * 30 + (segment.rhythm.dotted ? 13 : 0);
            const double pitchWidth =
                numbers.horizontalAdvance(QString::number(segment.degree)) +
                small.horizontalAdvance(accidentalText(segment.degree == 0 ? 0 : segment.accidental)) +
                rhythmWidth;
            segment.cellWidth = std::max({48.0, pitchWidth + 22, lyricWidth + 14});
            if (!segment.rhythm.exact)
                segment.cellWidth = std::max(segment.cellWidth, 88.0);
            const double digitWidth = numbers.horizontalAdvance(QString::number(segment.degree));
            const double accidentalWidth =
                small.horizontalAdvance(accidentalText(segment.degree == 0 ? 0 : segment.accidental));
            segment.leftExtent = std::max({24.0, digitWidth / 2 + accidentalWidth + 12, lyricWidth / 2 + 8});
            segment.rightExtent = std::max({24.0, digitWidth / 2 + rhythmWidth + 10, lyricWidth / 2 + 8});
            if (!segment.rhythm.exact)
                segment.rightExtent =
                    std::max(segment.rightExtent,
                             digitWidth / 2 + small.horizontalAdvance(QString("(%1t)").arg(duration)) + 12);
            auto &bar = bars[static_cast<std::size_t>(measure)];
            bar.width += segment.cellWidth;
            bar.segments.push_back(segments.size());
            segments.push_back(segment);
            tick += duration;
            remaining -= duration;
            continuation = true;
        }
    }

    rowHeight += maximumUnderlines * 5 + 7 * (aboveDots + belowDots - initialDots);

    int row = 0;
    int barsInRow = 0;
    double rowWidth = 0;
    for (auto &bar : bars)
    {
        const auto barIndex = &bar - bars.data();
        const auto barStart = startOf(barIndex);
        const auto firstEmpty = std::max(totalTicks, barStart);
        const int beatTicks = beatOf(barIndex);
        const int emptySlots =
            firstEmpty < endOf(barIndex) ? int((endOf(barIndex) - firstEmpty + beatTicks - 1) / beatTicks) : 0;
        if (layout && emptySlots)
        {
            if (bar.width + 12 * emptySlots > ContentWidth)
                fail("messages.audio_import.notation_too_large");
            bar.width = std::min(ContentWidth, bar.width + 48 * emptySlots);
        }
        bar.width = std::max(285.0, bar.width);
        if (sharedGrid)
        {
            if (sharedGrid->size() != bars.size())
                fail("ui.editor.invalid_composition");
            bar.width = (*sharedGrid)[std::size_t(barIndex)].width;
        }
        if (bar.width > ContentWidth)
            fail("messages.audio_import.notation_too_large");
        if (barsInRow == 4 || (barsInRow > 0 && rowWidth + bar.width > ContentWidth))
        {
            ++row;
            barsInRow = 0;
            rowWidth = 0;
        }
        bar.row = row;
        bar.left = Margin + rowWidth;
        rowWidth += bar.width;
        ++barsInRow;
    }
    const int height = std::max(score.editorMinimumBars > 0 ? 1900 : 0,
                                static_cast<int>(std::ceil(headerHeight + (row + 1) * rowHeight + Margin + 72)));
    if (height > MaximumHeight)
        fail("messages.audio_import.notation_too_large");

    const auto gridPoint = [&](std::size_t measure, std::int64_t tick) -> const NumberedSpacingPoint &
    {
        const auto &points = (*sharedGrid)[measure].points;
        const auto found = std::lower_bound(points.begin(), points.end(), tick,
                                            [](const auto &point, auto target) { return point.tick < target; });
        if (found == points.end() || found->tick != tick)
            fail("ui.editor.invalid_composition");
        return *found;
    };
    const double numberTopOffset = 34 + aboveDots * 7 + (marks ? 30 : 0) + (measures ? 28 : 0);
    const double lyricTopOffset =
        numberTopOffset + numbers.height() + maximumUnderlines * 5 + belowDots * 7 + 27 + (marks ? 22 : 0);
    for (auto &bar : bars)
    {
        double usedWidth = 2 * BarPadding;
        for (const auto index : bar.segments)
            usedWidth += segments[index].cellWidth;
        const auto barIndex = &bar - bars.data();
        const auto emptyStart = std::max(totalTicks, startOf(barIndex));
        const auto emptyTicks = std::max<std::int64_t>(0, endOf(barIndex) - emptyStart);
        const int beatTicks = beatOf(barIndex);
        const int emptySlots = int((emptyTicks + beatTicks - 1) / beatTicks);
        const double reserve = layout ? std::min(48.0 * emptySlots, bar.width - usedWidth) : 0;
        const double extra = bar.segments.empty() ? 0 : (bar.width - usedWidth - reserve) / bar.segments.size();
        double left = bar.left + BarPadding;
        for (const auto index : bar.segments)
        {
            auto &segment = segments[index];
            const double cellWidth = segment.cellWidth + extra;
            const double rhythmWidth = segment.rhythm.dashes * 30 + (segment.rhythm.dotted ? 13 : 0);
            const double accidentalWidth =
                small.horizontalAdvance(accidentalText(segment.degree == 0 ? 0 : segment.accidental));
            segment.row = bar.row;
            segment.centerX = left + (cellWidth - rhythmWidth + accidentalWidth) / 2;
            if (sharedGrid)
                segment.centerX = bar.left + gridPoint(std::size_t(barIndex), segment.startInBar).position;
            segment.numberTop = headerHeight + bar.row * rowHeight + numberTopOffset;
            const double digitWidth = numbers.horizontalAdvance(QString::number(segment.degree));
            if (!segment.continuation)
                anchors[segment.noteIndex] = {
                    {segment.centerX - digitWidth / 2, segment.numberTop, digitWidth, numbers.height()},
                    segment.row,
                    segment.measure};
            left += cellWidth;
        }
    }

    if (layout)
    {
        layout->grid.resize(bars.size());
        for (int system = 0; system <= row; ++system)
            layout->systems.emplace_back(0, headerHeight + system * rowHeight, ImageWidth, rowHeight);
        for (std::size_t i = 0; i < bars.size(); ++i)
        {
            const auto &bar = bars[i];
            const double top = headerHeight + bar.row * rowHeight + numberTopOffset - 10;
            layout->measures.emplace_back(bar.left, top, bar.width, numbers.height() + 24);
            auto &grid = layout->grid[i];
            grid.width = bar.width;
            double used = 2 * BarPadding;
            for (auto index : bar.segments)
            {
                const auto &segment = segments[index];
                used += segment.cellWidth;
                grid.points.push_back({segment.startInBar, segment.leftExtent, segment.rightExtent, 0});
            }
            const auto start = startOf(i);
            const auto firstEmpty = std::max(totalTicks, start);
            if (firstEmpty >= endOf(i))
                continue;
            const int beatTicks = beatOf(i);
            const int slots = int((endOf(i) - firstEmpty + beatTicks - 1) / beatTicks);
            const double reserve =
                bar.segments.empty() ? bar.width - 2 * BarPadding : std::min(48.0 * slots, bar.width - used);
            const double slotWidth = reserve / slots;
            const double left = bar.left + bar.width - BarPadding - reserve;
            for (int slot = 0; slot < slots; ++slot)
            {
                const auto slotTick = firstEmpty + slot * beatTicks;
                double cellLeft = left + slot * slotWidth;
                double cellRight = cellLeft + slotWidth;
                if (sharedGrid)
                {
                    const auto &point = gridPoint(i, slotTick - start);
                    cellLeft = bar.left + point.position - point.left;
                    if (slot + 1 < slots)
                    {
                        const auto &next = gridPoint(i, slotTick - start + beatTicks);
                        cellRight = bar.left + next.position - next.left;
                    }
                    else
                        cellRight = bar.left + bar.width - BarPadding;
                }
                const double width =
                    std::max(1.0, cellRight - cellLeft - std::min(4.0, (cellRight - cellLeft) / 5));
                layout->slots.push_back({QRectF(cellLeft, top, width, numbers.height() + 20), slotTick});
                const double half = std::max(6.0, std::min(16.0, ContentWidth / (4 * beatsOf(i))));
                grid.points.push_back({slotTick - start, half, half, 0});
            }
        }
        layout->addBars = QRectF(Margin, headerHeight + (row + 1) * rowHeight + 12, ContentWidth, 48);
    }
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        score.notes[i].source = anchors[i].source;
        score.notes[i].hasImageAnchor = true;
        score.notes[i].line = anchors[i].line;
        score.notes[i].measure = anchors[i].measure;
    }
    if (!rasterize)
        return {};
    recordNumberedRasterization();
    QImage image(ImageWidth, height, QImage::Format_RGB32);
    if (image.isNull())
        fail("messages.audio_import.notation_layout_failed");
    image.fill(Qt::white);
    QPainter painter(&image);
    if (!painter.isActive())
        fail("messages.audio_import.notation_layout_failed");
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Ink);
    QFont titleFont("Microsoft YaHei UI");
    titleFont.setPixelSize(32);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    const auto title = QFontMetricsF(titleFont).elidedText(QString::fromStdString(score.title), Qt::ElideRight,
                                                           static_cast<int>(ContentWidth));
    painter.drawText(QRectF(Margin, 28, ContentWidth, 58), Qt::AlignCenter, title);
    painter.setFont(lyricFont);
    painter.drawText(QRectF(Margin, 86, ContentWidth, 28), Qt::AlignCenter,
                     lyrics.elidedText(QString::fromStdString(score.subtitle), Qt::ElideRight, ContentWidth));
    const QString credits = QString::fromStdString(score.lyricist) +
                            (score.lyricist.empty() ? QString() : "  " + trText("ui.editor.lyricist")) +
                            (score.composer.empty() ? QString()
                                                    : "    " + QString::fromStdString(score.composer) + "  " +
                                                          trText("ui.editor.composer"));
    painter.drawText(QRectF(Margin, headerHeight - 55, ContentWidth, 28), Qt::AlignRight,
                     lyrics.elidedText(credits, Qt::ElideRight, ContentWidth * .65));
    static constexpr std::array<const char *, 12> keyNames{"C",  "C#", "D",  "Eb", "E",  "F",
                                                           "F#", "G",  "Ab", "A",  "Bb", "B"};
    painter.setFont(lyricFont);
    const auto *opening = measures && !measures->empty() ? &measures->front() : nullptr;
    painter.drawText(QPointF(Margin, headerHeight - 25),
                     QString("1=%1     %2/%3     %4=%5")
                         .arg(keyNames[static_cast<std::size_t>(opening ? opening->tonic : score.tonic)])
                         .arg(opening ? opening->beats : score.beatsPerBar)
                         .arg(opening ? opening->unit : score.beatUnit)
                         .arg(QChar(0x2669))
                         .arg(opening ? opening->bpm : score.bpm, 0, 'g', 5));

    if (lyricRows > 1)
    {
        painter.setFont(smallFont);
        painter.setPen(QColor("#61728A"));
        for (int line = 0; line <= row; ++line)
        {
            for (std::size_t verse = 0; verse < lyricRows; ++verse)
            {
                painter.drawText(QPointF(Margin - 28, headerHeight + line * rowHeight + lyricTopOffset +
                                                          verse * 26 + lyrics.ascent()),
                                 QString(QChar(static_cast<char>('A' + verse))));
            }
        }
    }

    for (const auto &segment : segments)
    {
        const auto &note = score.notes[segment.noteIndex];
        const double digitWidth = numbers.horizontalAdvance(QString::number(segment.degree));
        const double baseline = segment.numberTop + numbers.ascent();
        painter.setPen(Ink);
        painter.setFont(numberFont);
        painter.drawText(QPointF(segment.centerX - digitWidth / 2, baseline), QString::number(segment.degree));
        painter.setFont(smallFont);
        const auto accidental = accidentalText(segment.degree == 0 ? 0 : segment.accidental);
        painter.drawText(
            QPointF(segment.centerX - digitWidth / 2 - small.horizontalAdvance(accidental) - 4, baseline - 9),
            accidental);
        painter.setBrush(Ink);
        const int octave = segment.degree == 0 ? 0 : segment.octave;
        for (int dot = 0; dot < std::abs(octave); ++dot)
        {
            const double y =
                octave > 0 ? segment.numberTop - 5 - dot * 7
                           : segment.numberTop + numbers.height() + segment.rhythm.underlines * 5 + 8 + dot * 7;
            painter.drawEllipse(QPointF(segment.centerX, y), 2.2, 2.2);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Ink, 1.6));
        for (int line = 0; line < segment.rhythm.underlines; ++line)
        {
            const double y = segment.numberTop + numbers.height() + 2 + line * 5;
            painter.drawLine(QPointF(segment.centerX - 12, y), QPointF(segment.centerX + 12, y));
        }
        for (int dash = 0; dash < segment.rhythm.dashes; ++dash)
        {
            const double x = segment.centerX + digitWidth / 2 + 17 + dash * 30;
            painter.drawLine(QPointF(x, baseline - 13), QPointF(x + 18, baseline - 13));
        }
        if (segment.rhythm.dotted)
        {
            painter.setBrush(Ink);
            painter.drawEllipse(QPointF(segment.centerX + digitWidth / 2 + 10, baseline - 13), 2.2, 2.2);
            painter.setBrush(Qt::NoBrush);
        }
        if (!segment.rhythm.exact)
        {
            painter.setFont(smallFont);
            painter.drawText(QPointF(segment.centerX + digitWidth / 2 + 5, baseline - 4),
                             QString("(%1t)").arg(segment.duration));
        }
        if (!segment.continuation)
        {
            painter.setFont(smallFont);
            if (note.accent)
                painter.drawText(QPointF(segment.centerX - 7, segment.numberTop - aboveDots * 7 - 25), ">");
            if (note.staccato)
            {
                painter.setBrush(Ink);
                painter.drawEllipse(QPointF(segment.centerX, segment.numberTop - aboveDots * 7 - 14), 2.5, 2.5);
                painter.setBrush(Qt::NoBrush);
            }
            if (note.dynamicLevel > 0 && note.dynamicLevel <= 6)
            {
                QFont dynamicsFont("Times New Roman");
                dynamicsFont.setPixelSize(24);
                dynamicsFont.setItalic(true);
                painter.setFont(dynamicsFont);
                static constexpr const char *dynamics[] = {"", "pp", "p", "mp", "mf", "f", "ff"};
                painter.drawText(
                    QPointF(segment.centerX - 10, headerHeight + segment.row * rowHeight + lyricTopOffset - 8),
                    QString::fromLatin1(dynamics[note.dynamicLevel]));
            }
            if (note.keyOverride >= 0)
            {
                painter.setFont(smallFont);
                painter.drawText(QPointF(segment.centerX - 18, headerHeight + segment.row * rowHeight + 16),
                                 QString("1=%1").arg(keyNames[static_cast<std::size_t>(note.keyOverride)]));
            }
            const auto verses = lyricVerses(note);
            painter.setFont(lyricFont);
            for (std::size_t verse = 0; verse < lyricRows; ++verse)
            {
                const auto text = verse < verses.size() ? QString::fromStdString(verses[verse]) : QString{};
                painter.drawText(QRectF(segment.centerX - segment.cellWidth / 2,
                                        headerHeight + segment.row * rowHeight + lyricTopOffset + verse * 26,
                                        segment.cellWidth, 25),
                                 Qt::AlignHCenter | Qt::AlignTop, text);
            }
        }
    }

    if (labels)
    {
        painter.setFont(smallFont);
        painter.setPen(Ink);
        for (std::size_t i = 0; i < bars.size(); ++i)
            painter.drawText(
                QRectF(bars[i].left + 4, headerHeight + bars[i].row * rowHeight, bars[i].width - 8, 24),
                Qt::AlignLeft | Qt::AlignVCenter,
                small.elidedText((*labels)[i], Qt::ElideRight, bars[i].width - 8));
    }
    painter.setPen(QPen(Ink, 1.5));
    for (const auto &bar : bars)
    {
        const double top = headerHeight + bar.row * rowHeight + numberTopOffset - 8;
        const double endX = bar.left + bar.width;
        painter.drawLine(QPointF(endX, top),
                         QPointF(endX, top + numbers.height() + maximumUnderlines * 5 + belowDots * 7 + 19));
    }
    for (std::size_t i = 1; i < segments.size(); ++i)
    {
        const auto &previous = segments[i - 1];
        const auto &current = segments[i];
        const bool continuation = current.continuation && current.noteIndex == previous.noteIndex &&
                                  sourcePitches[current.noteIndex] >= 0;
        const bool tied = current.noteIndex == previous.noteIndex + 1 &&
                          score.notes[previous.noteIndex].tieToNext && sourcePitches[previous.noteIndex] >= 0 &&
                          sourcePitches[previous.noteIndex] == sourcePitches[current.noteIndex];
        if (!continuation && !tied)
            continue;
        const double firstY = headerHeight + previous.row * rowHeight + 26 + (measures ? 28 : 0);
        if (previous.row == current.row)
            drawArc(painter, previous.centerX + 8, current.centerX - 8, firstY);
        else
        {
            drawArc(painter, previous.centerX + 8, ImageWidth - Margin, firstY);
            drawArc(painter, Margin, current.centerX - 8,
                    headerHeight + current.row * rowHeight + 26 + (measures ? 28 : 0));
        }
    }
    painter.end();
    return image;
}

NumberedScoreLayout layoutNumberedScore(Score &score, const std::vector<NumberedMeasureSpan> *measures)
{
    NumberedScoreLayout layout;
    renderNumberedScoreImpl(score, &layout, nullptr, false, measures, nullptr);
    return layout;
}
QImage renderNumberedScore(Score &score, NumberedScoreLayout *layout,
                           const std::vector<NumberedMeasureGrid> *sharedGrid,
                           const std::vector<NumberedMeasureSpan> *measures, const std::vector<QString> *labels)
{
    return renderNumberedScoreImpl(score, layout, sharedGrid, true, measures, labels);
}

} // namespace singlilt

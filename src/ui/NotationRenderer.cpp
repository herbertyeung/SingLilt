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
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace singlilt
{
namespace
{

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
    if (duration == 720 || duration == 360 || duration == 180 || duration == 90)
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

QImage renderNumberedScore(Score &score)
{
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || !buildTimeline(score).valid())
        fail("messages.audio_import.notation_invalid_score");
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
    const auto barCount = (totalTicks + barTicks - 1) / barTicks;
    double rowHeight = 126 + 7 * (aboveDots + belowDots) + 26 * lyricRows;
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
    int maximumUnderlines = 0;
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        const auto &note = score.notes[i];
        if (note.keyOverride >= 0)
            key = note.keyOverride;
        sourcePitches.push_back(midiPitch(note, key));
        std::int64_t remaining = note.durationTicks;
        bool continuation = false;
        while (remaining > 0)
        {
            const int measure = static_cast<int>(tick / barTicks);
            const auto duration = std::min(remaining, barTicks - tick % barTicks);
            Segment segment{i, duration, continuation, measure, rhythmGlyph(duration)};
            maximumUnderlines = std::max(maximumUnderlines, segment.rhythm.underlines);
            double lyricWidth = 0;
            if (!continuation)
            {
                for (const auto &verse : lyricVerses(note))
                    lyricWidth = std::max(lyricWidth, lyrics.horizontalAdvance(QString::fromStdString(verse)));
            }
            const double rhythmWidth = segment.rhythm.dashes * 30 + (segment.rhythm.dotted ? 13 : 0);
            const double pitchWidth =
                numbers.horizontalAdvance(QString::number(note.degree)) +
                small.horizontalAdvance(accidentalText(note.degree == 0 ? 0 : note.accidental)) + rhythmWidth;
            segment.cellWidth = std::max({48.0, pitchWidth + 22, lyricWidth + 14});
            if (!segment.rhythm.exact)
                segment.cellWidth = std::max(segment.cellWidth, 88.0);
            auto &bar = bars[static_cast<std::size_t>(measure)];
            bar.width += segment.cellWidth;
            bar.segments.push_back(segments.size());
            segments.push_back(segment);
            tick += duration;
            remaining -= duration;
            continuation = true;
        }
    }

    rowHeight += maximumUnderlines * 5;

    int row = 0;
    int barsInRow = 0;
    double rowWidth = 0;
    for (auto &bar : bars)
    {
        bar.width = std::max(285.0, bar.width);
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
    const int height = static_cast<int>(std::ceil(HeaderHeight + (row + 1) * rowHeight + Margin));
    if (height > MaximumHeight)
        fail("messages.audio_import.notation_too_large");

    const double numberTopOffset = 34 + aboveDots * 7;
    const double lyricTopOffset = numberTopOffset + numbers.height() + maximumUnderlines * 5 + belowDots * 7 + 27;
    for (auto &bar : bars)
    {
        double usedWidth = 2 * BarPadding;
        for (const auto index : bar.segments)
            usedWidth += segments[index].cellWidth;
        const double extra = (bar.width - usedWidth) / bar.segments.size();
        double left = bar.left + BarPadding;
        for (const auto index : bar.segments)
        {
            auto &segment = segments[index];
            const auto &note = score.notes[segment.noteIndex];
            const double cellWidth = segment.cellWidth + extra;
            const double rhythmWidth = segment.rhythm.dashes * 30 + (segment.rhythm.dotted ? 13 : 0);
            const double accidentalWidth =
                small.horizontalAdvance(accidentalText(note.degree == 0 ? 0 : note.accidental));
            segment.row = bar.row;
            segment.centerX = left + (cellWidth - rhythmWidth + accidentalWidth) / 2;
            segment.numberTop = HeaderHeight + bar.row * rowHeight + numberTopOffset;
            const double digitWidth = numbers.horizontalAdvance(QString::number(note.degree));
            if (!segment.continuation)
                anchors[segment.noteIndex] = {
                    {segment.centerX - digitWidth / 2, segment.numberTop, digitWidth, numbers.height()},
                    segment.row,
                    segment.measure};
            left += cellWidth;
        }
    }

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
    static constexpr std::array<const char *, 12> keyNames{"C",  "C#", "D",  "Eb", "E",  "F",
                                                           "F#", "G",  "Ab", "A",  "Bb", "B"};
    painter.setFont(lyricFont);
    painter.drawText(QPointF(Margin, 118), QString("1=%1     %2/%3     %4=%5")
                                               .arg(keyNames[static_cast<std::size_t>(score.tonic)])
                                               .arg(score.beatsPerBar)
                                               .arg(score.beatUnit)
                                               .arg(QChar(0x2669))
                                               .arg(score.bpm, 0, 'g', 5));

    if (lyricRows > 1)
    {
        painter.setFont(smallFont);
        painter.setPen(QColor("#61728A"));
        for (int line = 0; line <= row; ++line)
        {
            for (std::size_t verse = 0; verse < lyricRows; ++verse)
            {
                painter.drawText(QPointF(Margin - 28, HeaderHeight + line * rowHeight + lyricTopOffset +
                                                          verse * 26 + lyrics.ascent()),
                                 QString(QChar(static_cast<char>('A' + verse))));
            }
        }
    }

    for (const auto &segment : segments)
    {
        const auto &note = score.notes[segment.noteIndex];
        const double digitWidth = numbers.horizontalAdvance(QString::number(note.degree));
        const double baseline = segment.numberTop + numbers.ascent();
        painter.setPen(Ink);
        painter.setFont(numberFont);
        painter.drawText(QPointF(segment.centerX - digitWidth / 2, baseline), QString::number(note.degree));
        painter.setFont(smallFont);
        const auto accidental = accidentalText(note.degree == 0 ? 0 : note.accidental);
        painter.drawText(
            QPointF(segment.centerX - digitWidth / 2 - small.horizontalAdvance(accidental) - 4, baseline - 9),
            accidental);
        painter.setBrush(Ink);
        const int octave = note.degree == 0 ? 0 : note.octave;
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
            if (note.keyOverride >= 0)
            {
                painter.setFont(smallFont);
                painter.drawText(QPointF(segment.centerX - 18, HeaderHeight + segment.row * rowHeight + 16),
                                 QString("1=%1").arg(keyNames[static_cast<std::size_t>(note.keyOverride)]));
            }
            const auto verses = lyricVerses(note);
            painter.setFont(lyricFont);
            for (std::size_t verse = 0; verse < lyricRows; ++verse)
            {
                const auto text = verse < verses.size() ? QString::fromStdString(verses[verse]) : QString{};
                painter.drawText(QRectF(segment.centerX - segment.cellWidth / 2,
                                        HeaderHeight + segment.row * rowHeight + lyricTopOffset + verse * 26,
                                        segment.cellWidth, 25),
                                 Qt::AlignHCenter | Qt::AlignTop, text);
            }
        }
    }

    painter.setPen(QPen(Ink, 1.5));
    for (const auto &bar : bars)
    {
        const double top = HeaderHeight + bar.row * rowHeight + numberTopOffset - 8;
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
        const double firstY = HeaderHeight + previous.row * rowHeight + 26;
        if (previous.row == current.row)
            drawArc(painter, previous.centerX + 8, current.centerX - 8, firstY);
        else
        {
            drawArc(painter, previous.centerX + 8, ImageWidth - Margin, firstY);
            drawArc(painter, Margin, current.centerX - 8, HeaderHeight + current.row * rowHeight + 26);
        }
    }
    painter.end();
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        score.notes[i].source = anchors[i].source;
        score.notes[i].line = anchors[i].line;
        score.notes[i].measure = anchors[i].measure;
    }
    return image;
}

} // namespace singlilt

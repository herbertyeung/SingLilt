// Staff notation layout, glyphs, and note positions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffRenderer.h"

#include "domain/StaffPerformance.h"
#include "domain/Timeline.h"
#include "i18n/LanguageManager.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace singlilt
{
namespace
{

constexpr int ImageWidth = 1440;
constexpr int MaximumHeight = 16000;
constexpr double Margin = 66;
constexpr double ContentWidth = ImageWidth - 2 * Margin;
constexpr double HeaderHeight = 148;
constexpr double StaffSpace = 12;
constexpr double StaffHeight = 4 * StaffSpace;
constexpr double BarPadding = 22;
const QColor Ink("#182C4C");
const QColor MutedInk("#627188");
constexpr std::array<int, 7> NaturalPitches{0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 7> SharpOrder{3, 0, 4, 1, 5, 2, 6};
constexpr std::array<int, 7> FlatOrder{6, 2, 5, 1, 4, 0, 3};

[[noreturn]] void fail(const char *key)
{
    throw std::runtime_error(trText(key).toStdString());
}

QFont musicFont()
{
    static const QString family = []
    {
        const int id = QFontDatabase::addApplicationFont(":/fonts/Bravura.otf");
        const auto families = QFontDatabase::applicationFontFamilies(id);
        if (id < 0 || families.isEmpty())
            fail("messages.audio_import.notation_layout_failed");
        return families.front();
    }();
    QFont font(family);
    // SMuFL's em contains four staff spaces.
    font.setPixelSize(static_cast<int>(4 * StaffSpace));
    const QRawFont raw = QRawFont::fromFont(font);
    for (const ushort glyph :
         {0xE050, 0xE062, 0xE0A2, 0xE0A3, 0xE0A4, 0xE240, 0xE245, 0xE260, 0xE261, 0xE262, 0xE4E3, 0xE4EA})
        if (!raw.supportsCharacter(glyph))
            fail("messages.audio_import.notation_layout_failed");
    return font;
}

struct Rhythm
{
    int denominator = 0;
    int dots = 0;
};

Rhythm rhythmFor(std::int64_t ticks)
{
    for (int denominator = 1; denominator <= 128; denominator *= 2)
    {
        const int base = TicksPerQuarter * 4 / denominator;
        if (ticks == base)
            return {denominator, 0};
        if (base % 2 == 0 && ticks == base * 3 / 2)
            return {denominator, 1};
        if (base % 4 == 0 && ticks == base * 7 / 4)
            return {denominator, 2};
    }
    // A tick label accompanies a diamond, never an ordinary note with a false duration.
    return {};
}

struct SpelledPitch
{
    int midi = -1;
    int diatonic = 0; // C0 = 0, D0 = 1, ...; MIDI octave numbering is retained.
    int alteration = 0;
};

int tonicLetter(int tonic, const StaffRenderOptions &options)
{
    const int signatureTonic = ((options.keyFifths * 7 + (options.minor ? 9 : 0)) % 12 + 12) % 12;
    if (signatureTonic == tonic)
        return ((options.keyFifths * 4 + (options.minor ? 5 : 0)) % 7 + 7) % 7;
    static constexpr std::array<int, 12> SharpLetters{0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};
    static constexpr std::array<int, 12> FlatLetters{0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
    return (options.keyFifths < 0 ? FlatLetters : SharpLetters)[static_cast<std::size_t>(tonic)];
}

SpelledPitch spellPitch(const Note &note, int tonic, const StaffRenderOptions &options)
{
    const int midi = midiPitch(note, tonic);
    if (midi < 0)
        return {};
    if (note.staffSpelling)
    {
        static constexpr std::array<char, 7> Steps{'C', 'D', 'E', 'F', 'G', 'A', 'B'};
        const auto &spelling = *note.staffSpelling;
        const auto found = std::find(Steps.begin(), Steps.end(), spelling.step);
        if (found != Steps.end() && spelling.octave >= -1 && spelling.octave <= 9 && spelling.alter >= -2 &&
            spelling.alter <= 2)
        {
            const auto letter = static_cast<std::size_t>(found - Steps.begin());
            if (12 * (spelling.octave + 1) + NaturalPitches[letter] + spelling.alter == midi)
                return {midi, spelling.octave * 7 + static_cast<int>(letter), spelling.alter};
        }
    }
    int letter = (tonicLetter(tonic, options) + note.degree - 1) % 7;
    int natural = NaturalPitches[static_cast<std::size_t>(letter)];
    int octave = static_cast<int>(std::lround((midi - natural) / 12.0)) - 1;
    int alteration = midi - (12 * (octave + 1) + natural);
    if (std::abs(alteration) > 3)
    {
        // Numbered degrees use a major scale even under a minor signature.
        // Prefer an ordinary enharmonic spelling to a quadruple accidental.
        letter = tonicLetter(midi % 12, options);
        natural = NaturalPitches[static_cast<std::size_t>(letter)];
        octave = static_cast<int>(std::lround((midi - natural) / 12.0)) - 1;
        alteration = midi - (12 * (octave + 1) + natural);
    }
    return {midi, octave * 7 + letter, alteration};
}

std::array<int, 7> keyAlterations(int fifths)
{
    std::array<int, 7> alterations{};
    for (int i = 0; i < std::abs(fifths); ++i)
        alterations[static_cast<std::size_t>(
            (fifths >= 0 ? SharpOrder : FlatOrder)[static_cast<std::size_t>(i)])] = fifths >= 0 ? 1 : -1;
    return alterations;
}

QString keyName(const StaffRenderOptions &options)
{
    static constexpr std::array<const char *, 15> MajorNames{"Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C",
                                                             "G",  "D",  "A",  "E",  "B",  "F#", "C#"};
    static constexpr std::array<const char *, 15> MinorNames{"Ab", "Eb", "Bb", "F",  "C",  "G",  "D", "A",
                                                             "E",  "B",  "F#", "C#", "G#", "D#", "A#"};
    const auto index = static_cast<std::size_t>(options.keyFifths + 7);
    return QString::fromLatin1((options.minor ? MinorNames : MajorNames)[index]) +
           (options.minor ? " min" : " maj");
}

double pitchY(const SpelledPitch &pitch, double staffTop, bool bassClef)
{
    const int bottomLine = bassClef ? 18 : 30; // G2 or E4.
    return staffTop + StaffHeight - (pitch.diatonic - bottomLine) * StaffSpace / 2;
}

struct Segment
{
    std::size_t noteIndex = 0;
    std::int64_t ticks = 0;
    bool continuation = false;
    int measure = 0;
    SpelledPitch pitch;
    Rhythm rhythm;
    bool showAccidental = false;
    double accidentalOffset = 0;
    int stemDirection = 0;
    double stemX = std::numeric_limits<double>::quiet_NaN();
    double stemEndY = std::numeric_limits<double>::quiet_NaN();
    bool drawStem = true;
    bool suppressFlags = false;
    double cellWidth = 0;
    int row = 0;
    double centerX = 0;
    double staffTop = 0;
    double y = 0;
};

struct Bar
{
    std::vector<std::size_t> segments;
    double width = 2 * BarPadding;
    int row = 0;
    int measureLabel = 0;
    double left = 0;
};

struct MeasureSpan
{
    std::int64_t endTick = 0;
    int label = 0;
    int beatsPerBar = 0;
    int beatUnit = 0;
};

struct Anchor
{
    SourceRect source;
    int row = 0;
    int measure = 0;
};

void drawGlyph(QPainter &painter, const QFont &font, ushort glyph, double x, double y)
{
    painter.setFont(font);
    painter.drawText(QPointF(x, y), QString(QChar(glyph)));
}

double centeredGlyphX(const QFont &font, ushort glyph, double center)
{
    const auto bounds = QFontMetricsF(font).tightBoundingRect(QString(QChar(glyph)));
    return center - bounds.center().x();
}

void drawAccidental(QPainter &painter, const QFont &font, int alteration, double right, double y)
{
    static constexpr std::array<ushort, 7> Glyphs{0xE266, 0xE264, 0xE260, 0xE261, 0xE262, 0xE263, 0xE265};
    const ushort glyph = Glyphs[static_cast<std::size_t>(alteration + 3)];
    const auto bounds = QFontMetricsF(font).tightBoundingRect(QString(QChar(glyph)));
    drawGlyph(painter, font, glyph, right - bounds.right(), y);
}

void drawMeter(QPainter &painter, int beats, int unit, double x, double top)
{
    QFont meterFont("Times New Roman");
    meterFont.setPixelSize(28);
    meterFont.setWeight(QFont::Bold);
    painter.setFont(meterFont);
    painter.drawText(QRectF(x, top - 7, 44, 32), Qt::AlignHCenter, QString::number(beats));
    painter.drawText(QRectF(x, top + 17, 44, 32), Qt::AlignHCenter, QString::number(unit));
}

void drawPrefix(QPainter &painter, const QFont &font, const StaffRenderOptions &options, const Score &score,
                double staffTop, bool firstRow)
{
    drawGlyph(painter, font, options.bassClef ? 0xE062 : 0xE050, Margin + 10,
              staffTop + (options.bassClef ? 1 : 3) * StaffSpace);
    static constexpr std::array<int, 7> TrebleSharps{0, 3, -1, 2, 5, 1, 4};
    static constexpr std::array<int, 7> BassSharps{2, 5, 1, 4, 7, 3, 6};
    static constexpr std::array<int, 7> TrebleFlats{4, 1, 5, 2, 6, 3, 7};
    static constexpr std::array<int, 7> BassFlats{6, 3, 7, 4, 8, 5, 2};
    const auto &positions = options.keyFifths >= 0 ? (options.bassClef ? BassSharps : TrebleSharps)
                                                   : (options.bassClef ? BassFlats : TrebleFlats);
    const double keyStart = Margin + 54;
    for (int i = 0; i < std::abs(options.keyFifths); ++i)
        drawGlyph(painter, font, options.keyFifths >= 0 ? 0xE262 : 0xE260, keyStart + i * 15,
                  staffTop + positions[static_cast<std::size_t>(i)] * StaffSpace / 2);
    if (!firstRow)
        return;
    const double meterX = keyStart + std::abs(options.keyFifths) * 15 + 12;
    drawMeter(painter, score.beatsPerBar, score.beatUnit, meterX, staffTop);
}

void drawLedgerLines(QPainter &painter, double center, double y, double staffTop, double width)
{
    painter.setPen(QPen(Ink, 1.25));
    for (double line = staffTop - StaffSpace; line >= y - 1; line -= StaffSpace)
        painter.drawLine(QPointF(center - width / 2 - 5, line), QPointF(center + width / 2 + 5, line));
    for (double line = staffTop + StaffHeight + StaffSpace; line <= y + 1; line += StaffSpace)
        painter.drawLine(QPointF(center - width / 2 - 5, line), QPointF(center + width / 2 + 5, line));
}

SourceRect drawSymbol(QPainter &painter, const QFont &font, const QFont &smallFont, const Segment &segment)
{
    painter.setPen(Ink);
    const auto &rhythm = segment.rhythm;
    const bool rest = segment.pitch.midi < 0;
    double symbolWidth = 20;
    SourceRect source{segment.centerX - 12, segment.y - 10, 24, 20};
    if (rhythm.denominator == 0)
    {
        if (!rest)
            drawLedgerLines(painter, segment.centerX, segment.y, segment.staffTop, symbolWidth);
        painter.setBrush(Qt::white);
        painter.setPen(QPen(Ink, 1.6));
        QPainterPath diamond;
        diamond.moveTo(segment.centerX - 9, segment.y);
        diamond.lineTo(segment.centerX, segment.y - 6);
        diamond.lineTo(segment.centerX + 9, segment.y);
        diamond.lineTo(segment.centerX, segment.y + 6);
        diamond.closeSubpath();
        painter.drawPath(diamond);
        if (rest)
        {
            painter.drawLine(QPointF(segment.centerX - 4, segment.y - 4),
                             QPointF(segment.centerX + 4, segment.y + 4));
            painter.drawLine(QPointF(segment.centerX + 4, segment.y - 4),
                             QPointF(segment.centerX - 4, segment.y + 4));
        }
        painter.setBrush(Qt::NoBrush);
        painter.setFont(smallFont);
        painter.drawText(QRectF(segment.centerX - segment.cellWidth / 2, segment.y - 39, segment.cellWidth, 23),
                         Qt::AlignHCenter, QString("%1t").arg(segment.ticks));
    }
    else if (rest)
    {
        int power = 0;
        for (int denominator = rhythm.denominator; denominator > 1; denominator /= 2)
            ++power;
        const ushort glyph = static_cast<ushort>(0xE4E3 + power);
        const double baseline =
            rhythm.denominator == 1 ? segment.staffTop + StaffSpace : segment.staffTop + StaffHeight / 2;
        const auto bounds = QFontMetricsF(font).tightBoundingRect(QString(QChar(glyph)));
        const double x = centeredGlyphX(font, glyph, segment.centerX);
        drawGlyph(painter, font, glyph, x, baseline);
        source = {x + bounds.left() - 3, baseline + bounds.top() - 3, bounds.width() + 6, bounds.height() + 6};
        symbolWidth = bounds.width();
    }
    else
    {
        const ushort glyph = rhythm.denominator == 1 ? 0xE0A2 : rhythm.denominator == 2 ? 0xE0A3 : 0xE0A4;
        const auto bounds = QFontMetricsF(font).tightBoundingRect(QString(QChar(glyph)));
        symbolWidth = bounds.width();
        drawLedgerLines(painter, segment.centerX, segment.y, segment.staffTop, symbolWidth);
        painter.setPen(Ink);
        drawGlyph(painter, font, glyph, centeredGlyphX(font, glyph, segment.centerX), segment.y);
        source = {segment.centerX - symbolWidth / 2 - 3, segment.y + bounds.top() - 3, symbolWidth + 6,
                  bounds.height() + 6};
        if (rhythm.denominator > 1 && segment.drawStem)
        {
            const bool stemUp = segment.stemDirection == 0 ? segment.y > segment.staffTop + StaffHeight / 2
                                                           : segment.stemDirection > 0;
            const double stemX = std::isfinite(segment.stemX)
                                     ? segment.stemX
                                     : segment.centerX + (stemUp ? 1 : -1) * (symbolWidth / 2 - 0.7);
            const double stemEnd = std::isfinite(segment.stemEndY)
                                       ? segment.stemEndY
                                       : segment.y + (stemUp ? -1 : 1) * 3.5 * StaffSpace;
            painter.setPen(QPen(Ink, 1.4));
            painter.drawLine(QPointF(stemX, segment.y), QPointF(stemX, stemEnd));
            if (rhythm.denominator >= 8 && !segment.suppressFlags)
            {
                int flags = 1;
                for (int denominator = rhythm.denominator; denominator > 8; denominator /= 2)
                    ++flags;
                const ushort flag = static_cast<ushort>(0xE240 + (flags - 1) * 2 + (stemUp ? 0 : 1));
                drawGlyph(painter, font, flag, stemX, stemEnd);
            }
        }
    }
    if (segment.showAccidental && !rest)
        drawAccidental(painter, font, segment.pitch.alteration,
                       segment.centerX - symbolWidth / 2 - 5 - segment.accidentalOffset, segment.y);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Ink);
    double dotY = rest ? segment.staffTop + 1.5 * StaffSpace : segment.y;
    const double staffPosition = (dotY - segment.staffTop) / StaffSpace;
    if (!rest && std::abs(staffPosition - std::round(staffPosition)) < 0.01)
        dotY -= StaffSpace / 2;
    for (int dot = 0; dot < rhythm.dots; ++dot)
        painter.drawEllipse(QPointF(segment.centerX + symbolWidth / 2 + 11 + dot * 8, dotY), 2.3, 2.3);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(Ink);
    return source;
}

void drawTie(QPainter &painter, double firstX, double endX, double firstY, double endY)
{
    if (endX <= firstX)
        return;
    const double rise = std::min(17.0, (endX - firstX) / 5);
    QPainterPath path;
    path.moveTo(firstX, firstY);
    path.cubicTo(firstX + (endX - firstX) / 3, firstY - rise, firstX + (endX - firstX) * 2 / 3, endY - rise, endX,
                 endY);
    painter.drawPath(path);
}

void drawRepeat(QPainter &painter, double x, double top, bool start)
{
    painter.setPen(QPen(Ink, 3.4));
    painter.drawLine(QPointF(x, top), QPointF(x, top + StaffHeight));
    const double thinX = x + (start ? 7 : -7);
    painter.setPen(QPen(Ink, 1.3));
    painter.drawLine(QPointF(thinX, top), QPointF(thinX, top + StaffHeight));
    painter.setBrush(Ink);
    painter.setPen(Qt::NoPen);
    for (const double y : {top + 1.5 * StaffSpace, top + 2.5 * StaffSpace})
        painter.drawEllipse(QPointF(x + (start ? 14 : -14), y), 2.5, 2.5);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(Ink);
}

} // namespace

QFont staffMusicFont(int pixelSize)
{
    if (pixelSize < 1 || pixelSize > 512)
        fail("messages.audio_import.notation_layout_failed");
    auto font = musicFont();
    font.setPixelSize(pixelSize);
    return font;
}

QImage renderStaffScore(Score &score, const StaffRenderOptions &options)
{
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || !buildTimeline(score).valid() ||
        options.keyFifths < -7 || options.keyFifths > 7)
        fail("messages.audio_import.notation_invalid_score");
    const QFont notationFont = musicFont();
    QFont lyricFont("Microsoft YaHei UI");
    lyricFont.setPixelSize(19);
    QFont smallFont("Microsoft YaHei UI");
    smallFont.setPixelSize(16);
    const QFontMetricsF lyrics(lyricFont);
    const QFontMetricsF small(smallFont);
    const int barTicks = ticksPerBar(score);
    std::int64_t totalTicks = 0;
    std::size_t lyricRows = 1;
    for (const auto &note : score.notes)
    {
        totalTicks += note.durationTicks;
        lyricRows = std::max(lyricRows, lyricVerses(note).size());
    }
    bool preserveMeasures =
        std::any_of(score.notes.begin(), score.notes.end(), [](const Note &note) { return note.measure > 0; });
    int previousMeasure = 0;
    for (const auto &note : score.notes)
    {
        if (note.measure < previousMeasure)
            preserveMeasures = false;
        previousMeasure = note.measure;
    }
    std::vector<MeasureSpan> measureSpans;
    const auto appendMeasures = [&](std::int64_t start, std::int64_t end, int label)
    {
        while (start < end)
        {
            start = std::min(end, start + barTicks);
            measureSpans.push_back({start, label++});
            if (measureSpans.size() > 512)
                fail("messages.audio_import.notation_too_large");
        }
    };
    if (!score.writtenMeasures.empty())
    {
        preserveMeasures = true;
        for (const auto &measure : score.writtenMeasures)
            measureSpans.push_back({measure.startTick + measure.durationTicks, measure.number, measure.beatsPerBar,
                                    measure.beatUnit});
    }
    else if (preserveMeasures)
    {
        std::int64_t writtenTick = 0;
        for (std::size_t first = 0; first < score.notes.size();)
        {
            std::size_t end = first;
            while (end < score.notes.size() && score.notes[end].measure == score.notes[first].measure)
                writtenTick += score.notes[end++].durationTicks;
            // Written labels own their boundaries, including an overfull imported measure.
            measureSpans.push_back({writtenTick, score.notes[first].measure});
            if (measureSpans.size() > 512)
                fail("messages.audio_import.notation_too_large");
            first = end;
        }
    }
    else
        appendMeasures(0, totalTicks, 0);
    std::vector<Bar> bars(measureSpans.size());
    for (std::size_t i = 0; i < bars.size(); ++i)
        bars[i].measureLabel = measureSpans[i].label;
    std::vector<Segment> segments;
    std::vector<Anchor> anchors(score.notes.size());
    std::vector<std::size_t> firstSegment(score.notes.size());
    std::vector<std::size_t> lastSegment(score.notes.size());
    const auto keyAccidentals = keyAlterations(options.keyFifths);
    std::map<int, int> measureAccidentals;
    int accidentalMeasure = -1;
    int tonic = score.tonic;
    std::int64_t tick = 0;
    std::size_t currentMeasure = 0;
    double above = 64;
    double below = 58;
    bool exactTickLabels = false;
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        const auto &note = score.notes[i];
        if (note.keyOverride >= 0)
            tonic = note.keyOverride;
        const auto pitch = spellPitch(note, tonic, options);
        const double relativeY = pitch.midi < 0 ? StaffHeight / 2 : pitchY(pitch, 0, options.bassClef);
        above = std::max(above, -relativeY + 64);
        below = std::max(below, relativeY - StaffHeight + 56);
        firstSegment[i] = segments.size();
        std::int64_t remaining = note.durationTicks;
        bool continuation = false;
        while (remaining > 0)
        {
            while (tick >= measureSpans[currentMeasure].endTick)
                ++currentMeasure;
            const int measure = static_cast<int>(currentMeasure);
            const auto duration = std::min(remaining, measureSpans[currentMeasure].endTick - tick);
            Segment segment;
            segment.noteIndex = i;
            segment.ticks = duration;
            segment.continuation = continuation;
            segment.measure = measure;
            segment.pitch = pitch;
            segment.rhythm = rhythmFor(duration);
            if (measure != accidentalMeasure)
            {
                measureAccidentals.clear();
                accidentalMeasure = measure;
            }
            const bool tied = i > 0 && !continuation && score.notes[i - 1].tieToNext &&
                              segments[firstSegment[i - 1]].pitch.midi == pitch.midi;
            if (pitch.midi >= 0 && !continuation && !tied)
            {
                const auto state = measureAccidentals.find(pitch.diatonic);
                const int letter = (pitch.diatonic % 7 + 7) % 7;
                const int previous = state == measureAccidentals.end()
                                         ? keyAccidentals[static_cast<std::size_t>(letter)]
                                         : state->second;
                segment.showAccidental = pitch.alteration != previous;
                measureAccidentals[pitch.diatonic] = pitch.alteration;
            }
            double lyricWidth = 0;
            if (!continuation)
                for (const auto &verse : lyricVerses(note))
                    lyricWidth = std::max(lyricWidth, lyrics.horizontalAdvance(QString::fromStdString(verse)));
            segment.cellWidth = std::max(68.0, lyricWidth + 18);
            if (segment.showAccidental)
                segment.cellWidth += 13 * std::max(1, std::abs(pitch.alteration));
            if (segment.rhythm.dots > 0)
                segment.cellWidth += segment.rhythm.dots * 8;
            if (segment.rhythm.denominator == 0)
            {
                exactTickLabels = true;
                segment.cellWidth =
                    std::max(segment.cellWidth, small.horizontalAdvance(QString("%1t").arg(duration)) + 26);
            }
            auto &bar = bars[static_cast<std::size_t>(measure)];
            bar.width += segment.cellWidth;
            bar.segments.push_back(segments.size());
            segments.push_back(segment);
            tick += duration;
            remaining -= duration;
            continuation = true;
        }
        lastSegment[i] = segments.size() - 1;
    }
    const double prefixWidth = 130 + std::abs(options.keyFifths) * 15;
    const double available = ContentWidth - prefixWidth;
    const double rowHeight = above + StaffHeight + below + lyricRows * 26 + 40;
    int row = 0;
    int barsInRow = 0;
    double used = 0;
    std::vector<double> rowEnd;
    for (auto &bar : bars)
    {
        bar.width = std::max(260.0, bar.width);
        if (bar.width > available)
            fail("messages.audio_import.notation_too_large");
        if (barsInRow == 4 || (barsInRow > 0 && used + bar.width > available))
        {
            rowEnd.push_back(Margin + prefixWidth + used);
            ++row;
            barsInRow = 0;
            used = 0;
        }
        bar.row = row;
        bar.left = Margin + prefixWidth + used;
        used += bar.width;
        ++barsInRow;
    }
    rowEnd.push_back(Margin + prefixWidth + used);
    const int height = static_cast<int>(std::ceil(HeaderHeight + (row + 1) * rowHeight + Margin));
    if (height > MaximumHeight)
        fail("messages.audio_import.notation_too_large");
    for (const auto &bar : bars)
    {
        double baseWidth = 2 * BarPadding;
        for (const auto index : bar.segments)
            baseWidth += segments[index].cellWidth;
        const double extra = (bar.width - baseWidth) / bar.segments.size();
        double left = bar.left + BarPadding;
        for (const auto index : bar.segments)
        {
            auto &segment = segments[index];
            segment.cellWidth += extra;
            segment.centerX = left + segment.cellWidth / 2;
            segment.row = bar.row;
            segment.staffTop = HeaderHeight + bar.row * rowHeight + above;
            segment.y = segment.pitch.midi < 0 ? segment.staffTop + StaffHeight / 2
                                               : pitchY(segment.pitch, segment.staffTop, options.bassClef);
            left += segment.cellWidth;
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
    QFont titleFont("Microsoft YaHei UI");
    titleFont.setPixelSize(32);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(Ink);
    const auto title = QFontMetricsF(titleFont).elidedText(QString::fromStdString(score.title), Qt::ElideRight,
                                                           static_cast<int>(ContentWidth));
    painter.drawText(QRectF(Margin, 28, ContentWidth, 52), Qt::AlignHCenter | Qt::AlignVCenter, title);
    painter.setFont(lyricFont);
    painter.setPen(MutedInk);
    painter.drawText(QPointF(Margin, 113), QString("%1     %2/%3     %4 = %5")
                                               .arg(keyName(options))
                                               .arg(score.beatsPerBar)
                                               .arg(score.beatUnit)
                                               .arg(QChar(0x2669))
                                               .arg(score.bpm, 0, 'g', 5));
    if (exactTickLabels)
    {
        painter.setFont(smallFont);
        painter.drawText(QRectF(ImageWidth - Margin - 230, 90, 230, 28), Qt::AlignRight,
                         QString("t = 1/%1 %2").arg(TicksPerQuarter).arg(QChar(0x2669)));
    }
    for (int line = 0; line <= row; ++line)
    {
        const double staffTop = HeaderHeight + line * rowHeight + above;
        painter.setPen(QPen(QColor("#8996A8"), 1.0));
        for (int staffLine = 0; staffLine < 5; ++staffLine)
            painter.drawLine(QPointF(Margin, staffTop + staffLine * StaffSpace),
                             QPointF(rowEnd[static_cast<std::size_t>(line)], staffTop + staffLine * StaffSpace));
        painter.setPen(Ink);
        drawPrefix(painter, notationFont, options, score, staffTop, line == 0);
        if (lyricRows > 1)
        {
            painter.setFont(smallFont);
            painter.setPen(MutedInk);
            for (std::size_t verse = 0; verse < lyricRows; ++verse)
                painter.drawText(
                    QPointF(Margin - 27, staffTop + StaffHeight + below + verse * 26 + lyrics.ascent()),
                    QString(QChar(static_cast<char>('A' + verse))));
        }
    }
    for (const auto &segment : segments)
    {
        const auto &note = score.notes[segment.noteIndex];
        const auto source = drawSymbol(painter, notationFont, smallFont, segment);
        if (segment.continuation)
            continue;
        anchors[segment.noteIndex] = {source, segment.row, preserveMeasures ? note.measure : segment.measure};
        painter.setFont(lyricFont);
        const auto verses = lyricVerses(note);
        for (std::size_t verse = 0; verse < verses.size(); ++verse)
            painter.drawText(QRectF(segment.centerX - segment.cellWidth / 2,
                                    segment.staffTop + StaffHeight + below + verse * 26, segment.cellWidth, 26),
                             Qt::AlignHCenter | Qt::AlignTop, QString::fromStdString(verses[verse]));
        if (note.keyOverride >= 0)
        {
            static constexpr std::array<const char *, 12> Keys{"C",  "C#", "D",  "Eb", "E",  "F",
                                                               "F#", "G",  "Ab", "A",  "Bb", "B"};
            painter.setFont(smallFont);
            painter.setPen(MutedInk);
            painter.drawText(QPointF(segment.centerX - 20, segment.staffTop - above + 39),
                             QString("1=%1").arg(Keys[static_cast<std::size_t>(note.keyOverride)]));
        }
    }
    for (std::size_t i = 0; i < bars.size(); ++i)
    {
        const auto &bar = bars[i];
        const double top = HeaderHeight + bar.row * rowHeight + above;
        const double x = bar.left + bar.width;
        painter.setPen(QPen(Ink, 1.4));
        painter.drawLine(QPointF(x, top), QPointF(x, top + StaffHeight));
        if (i + 1 == bars.size())
        {
            painter.setPen(QPen(Ink, 3.3));
            painter.drawLine(QPointF(x + 5, top), QPointF(x + 5, top + StaffHeight));
        }
        painter.setFont(smallFont);
        painter.setPen(MutedInk);
        painter.drawText(QPointF(bar.left + 3, top - above + 18), QString::number(bar.measureLabel + 1));
    }
    painter.setPen(QPen(Ink, 1.7));
    for (std::size_t i = 1; i < segments.size(); ++i)
    {
        const auto &previous = segments[i - 1];
        const auto &current = segments[i];
        const bool continuation = current.continuation && current.noteIndex == previous.noteIndex;
        const bool tied = current.noteIndex == previous.noteIndex + 1 && score.notes[previous.noteIndex].tieToNext;
        if ((!continuation && !tied) || previous.pitch.midi < 0 || previous.pitch.midi != current.pitch.midi)
            continue;
        if (previous.row == current.row)
            drawTie(painter, previous.centerX + 10, current.centerX - 10, previous.y - 14, current.y - 14);
        else
        {
            drawTie(painter, previous.centerX + 10, rowEnd[static_cast<std::size_t>(previous.row)],
                    previous.y - 14, previous.y - 14);
            drawTie(painter, Margin + prefixWidth - 10, current.centerX - 10, current.y - 14, current.y - 14);
        }
    }
    for (const auto &repeat : score.repeats)
    {
        const auto &first = segments[firstSegment[repeat.firstNote]];
        const auto &last = segments[lastSegment[repeat.endNote - 1]];
        const double firstX = first.centerX - first.cellWidth / 2 - 11;
        const double lastX = last.centerX + last.cellWidth / 2 + 11;
        drawRepeat(painter, firstX, first.staffTop, true);
        drawRepeat(painter, lastX, last.staffTop, false);
        if (repeat.count != 2)
        {
            painter.setFont(smallFont);
            painter.drawText(QPointF(lastX - 36, last.staffTop - above + 39), QString("%1x").arg(repeat.count));
        }
        if (repeat.firstEndingNote >= 0)
        {
            const auto &ending = segments[firstSegment[static_cast<std::size_t>(repeat.firstEndingNote)]];
            painter.setPen(QPen(Ink, 1.1));
            for (int endingRow = ending.row; endingRow <= last.row; ++endingRow)
            {
                const double left =
                    endingRow == ending.row ? ending.centerX - ending.cellWidth / 2 : Margin + prefixWidth;
                const double right = endingRow == last.row ? lastX : rowEnd[static_cast<std::size_t>(endingRow)];
                const double y = HeaderHeight + endingRow * rowHeight + 45;
                painter.drawLine(QPointF(left, y), QPointF(right, y));
                if (endingRow == ending.row)
                {
                    painter.drawLine(QPointF(left, y), QPointF(left, y + 11));
                    painter.setFont(smallFont);
                    painter.drawText(QPointF(left + 6, y - 5), "1.");
                }
                if (endingRow == last.row)
                    painter.drawLine(QPointF(right, y), QPointF(right, y + 11));
            }
        }
    }
    painter.end();
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        score.notes[i].source = anchors[i].source;
        score.notes[i].line = anchors[i].row;
        score.notes[i].measure = anchors[i].measure;
    }
    return image;
}

QImage renderGrandStaffScore(Score &score, StaffPerformance &performance, const StaffRenderOptions &options)
{
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()) || !buildTimeline(score).valid() ||
        options.keyFifths < -7 || options.keyFifths > 7 || performance.staffCount < 1 ||
        performance.staffCount > 2 || performance.primaryStaff < 1 ||
        performance.primaryStaff > performance.staffCount || performance.notes.size() > 100000)
        fail("messages.audio_import.notation_invalid_score");
    const QFont notationFont = musicFont();
    QFont lyricFont("Microsoft YaHei UI");
    lyricFont.setPixelSize(19);
    QFont smallFont("Microsoft YaHei UI");
    smallFont.setPixelSize(16);
    const QFontMetricsF lyricMetrics(lyricFont);
    const int barTicks = ticksPerBar(score);
    std::vector<std::int64_t> guideStarts{0};
    std::size_t lyricRows = 1;
    bool preserveMeasures = false;
    bool nondecreasing = true;
    int previousMeasure = 0;
    for (const auto &note : score.notes)
    {
        guideStarts.push_back(guideStarts.back() + note.durationTicks);
        lyricRows = std::max(lyricRows, lyricVerses(note).size());
        preserveMeasures |= note.measure > 0;
        nondecreasing &= note.measure >= previousMeasure;
        previousMeasure = note.measure;
    }
    preserveMeasures &= nondecreasing;
    const auto totalTicks = guideStarts.back();
    if (totalTicks != performance.durationTicks || performance.sourceTonic < 0 || performance.sourceTonic > 11)
        fail("messages.audio_import.notation_invalid_score");
    std::vector<MeasureSpan> measures;
    const auto appendMeasures = [&](std::int64_t start, std::int64_t end, int label)
    {
        while (start < end)
        {
            start = std::min(end, start + barTicks);
            measures.push_back({start, label++});
            if (measures.size() > 512)
                fail("messages.audio_import.notation_too_large");
        }
    };
    if (!score.writtenMeasures.empty())
    {
        preserveMeasures = true;
        for (const auto &measure : score.writtenMeasures)
            measures.push_back({measure.startTick + measure.durationTicks, measure.number, measure.beatsPerBar,
                                measure.beatUnit});
    }
    else if (preserveMeasures)
    {
        for (std::size_t first = 0; first < score.notes.size();)
        {
            std::size_t end = first + 1;
            while (end < score.notes.size() && score.notes[end].measure == score.notes[first].measure)
                ++end;
            measures.push_back({guideStarts[end], score.notes[first].measure});
            if (measures.size() > 512)
                fail("messages.audio_import.notation_too_large");
            first = end;
        }
    }
    else
        appendMeasures(0, totalTicks, 0);
    struct GrandSegment
    {
        Segment symbol;
        std::int64_t tick = 0;
        int staff = 0;
        double headOffset = 0;
        bool bassClef = false;
    };
    struct Column
    {
        double width = 66;
        double center = 0;
        double symbolPadding = 0;
    };
    std::vector<GrandSegment> segments;
    std::vector<Bar> bars(measures.size());
    std::vector<std::map<std::int64_t, Column>> columns(measures.size());
    std::array<double, 2> above{64, 64};
    std::array<double, 2> below{58, 58};
    const auto measureAt = [&](std::int64_t tick)
    {
        return static_cast<std::size_t>(std::upper_bound(measures.begin(), measures.end(), tick,
                                                         [](std::int64_t target, const MeasureSpan &span)
                                                         { return target < span.endTick; }) -
                                        measures.begin());
    };
    std::array<std::vector<StaffClefChange>, 2> clefs;
    for (const auto &change : performance.clefChanges)
    {
        if (change.startTick < 0 || change.startTick >= totalTicks || change.staff < 1 ||
            change.staff > performance.staffCount)
            fail("messages.audio_import.notation_invalid_score");
        clefs[change.staff - 1].push_back(change);
    }
    for (auto &changes : clefs)
        std::sort(changes.begin(), changes.end(), [](const StaffClefChange &first, const StaffClefChange &second)
                  { return first.startTick < second.startTick; });
    const auto bassAt = [&](int staff, std::int64_t tick)
    {
        const auto &changes = clefs[staff];
        const auto next = std::upper_bound(changes.begin(), changes.end(), tick,
                                           [](std::int64_t target, const StaffClefChange &change)
                                           { return target < change.startTick; });
        return next == changes.begin() ? (performance.staffCount == 2 ? staff == 1 : options.bassClef)
                                       : std::prev(next)->bassClef;
    };
    const auto appendSpan =
        [&](std::size_t original, std::int64_t start, std::int64_t end, int staff, const SpelledPitch &pitch)
    {
        bool continuation = false;
        while (start < end)
        {
            const auto measure = measureAt(start);
            auto segmentEnd = std::min(end, measures[measure].endTick);
            const auto &changes = clefs[staff];
            const auto nextClef = std::upper_bound(changes.begin(), changes.end(), start,
                                                   [](std::int64_t target, const StaffClefChange &change)
                                                   { return target < change.startTick; });
            if (nextClef != changes.end())
                segmentEnd = std::min(segmentEnd, nextClef->startTick);
            const auto duration = segmentEnd - start;
            GrandSegment segment;
            segment.symbol.noteIndex = original;
            segment.symbol.ticks = duration;
            segment.symbol.measure = static_cast<int>(measure);
            segment.symbol.continuation = continuation;
            segment.symbol.pitch = pitch;
            segment.symbol.rhythm = rhythmFor(duration);
            segment.tick = start;
            segment.staff = staff;
            segment.bassClef = bassAt(staff, start);
            const double relativeY = pitch.midi < 0 ? StaffHeight / 2 : pitchY(pitch, 0, segment.bassClef);
            above[staff] = std::max(above[staff], -relativeY + 64);
            below[staff] = std::max(below[staff], relativeY - StaffHeight + 56);
            bars[measure].segments.push_back(segments.size());
            columns[measure].try_emplace(start);
            segments.push_back(segment);
            start += duration;
            continuation = true;
        }
    };
    const int keyShift = score.tonic - performance.sourceTonic;
    std::array<std::vector<std::pair<std::int64_t, std::int64_t>>, 2> sounding;
    for (std::size_t i = 0; i < performance.notes.size(); ++i)
    {
        const auto &note = performance.notes[i];
        const int pitch = note.midiPitch + keyShift;
        if (note.staff < 1 || note.staff > performance.staffCount || note.startTick < 0 ||
            note.startTick >= totalTicks || note.durationTicks <= 0 ||
            note.durationTicks > totalTicks - note.startTick || pitch < 0 || pitch > 127)
            fail("messages.audio_import.notation_invalid_score");
        static constexpr std::array<int, 12> SharpDegrees{1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7};
        static constexpr std::array<int, 12> FlatDegrees{1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7, 7};
        static constexpr std::array<int, 12> SharpAlterations{0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
        static constexpr std::array<int, 12> FlatAlterations{0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0};
        const auto pitchClass = static_cast<std::size_t>(pitch % 12);
        Note written;
        written.degree = (options.keyFifths < 0 ? FlatDegrees : SharpDegrees)[pitchClass];
        written.accidental = (options.keyFifths < 0 ? FlatAlterations : SharpAlterations)[pitchClass];
        written.octave = pitch / 12 - 5;
        written.staffSpelling = note.staffSpelling;
        const auto spelling = spellPitch(written, 0, options);
        const int staff = note.staff - 1;
        appendSpan(i, note.startTick, note.startTick + note.durationTicks, staff, spelling);
        sounding[staff].push_back({note.startTick, note.startTick + note.durationTicks});
    }
    for (int staff = 0; staff < performance.staffCount; ++staff)
    {
        auto &intervals = sounding[staff];
        std::sort(intervals.begin(), intervals.end());
        std::int64_t cursor = 0;
        for (const auto &[start, end] : intervals)
        {
            if (cursor < start)
                appendSpan(performance.notes.size(), cursor, start, staff, {});
            cursor = std::max(cursor, end);
        }
        if (cursor < totalTicks)
            appendSpan(performance.notes.size(), cursor, totalTicks, staff, {});
    }
    for (const auto &change : performance.clefChanges)
    {
        if (change.startTick == 0)
            continue;
        auto &column = columns[measureAt(change.startTick)][change.startTick];
        column.symbolPadding = std::max(column.symbolPadding, 44.0);
    }
    for (std::size_t i = 1; i < measures.size(); ++i)
        if (measures[i].beatsPerBar > 0 && (measures[i].beatsPerBar != measures[i - 1].beatsPerBar ||
                                            measures[i].beatUnit != measures[i - 1].beatUnit))
            columns[i][measures[i - 1].endTick].symbolPadding += 44;
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        auto &column = columns[measureAt(guideStarts[i])][guideStarts[i]];
        for (const auto &lyric : lyricVerses(score.notes[i]))
            column.width =
                std::max(column.width, lyricMetrics.horizontalAdvance(QString::fromStdString(lyric)) + 18);
    }
    const auto keyAccidentals = keyAlterations(options.keyFifths);
    for (std::size_t measure = 0; measure < bars.size(); ++measure)
    {
        auto &bar = bars[measure];
        bar.measureLabel = measures[measure].label;
        std::sort(bar.segments.begin(), bar.segments.end(),
                  [&](std::size_t first, std::size_t second)
                  {
                      const auto &a = segments[first];
                      const auto &b = segments[second];
                      if (a.tick != b.tick)
                          return a.tick < b.tick;
                      if (a.staff != b.staff)
                          return a.staff < b.staff;
                      return a.symbol.pitch.diatonic < b.symbol.pitch.diatonic;
                  });
        std::array<std::map<int, int>, 2> accidentalState;
        const GrandSegment *previous = nullptr;
        int accidentalColumn = 0;
        for (const auto index : bar.segments)
        {
            auto &segment = segments[index];
            auto &symbol = segment.symbol;
            const auto &pitch = symbol.pitch;
            if (!previous || previous->tick != segment.tick || previous->staff != segment.staff)
                accidentalColumn = 0;
            else if (pitch.midi >= 0 && previous->symbol.pitch.midi >= 0 &&
                     pitch.diatonic - previous->symbol.pitch.diatonic == 1)
                segment.headOffset = previous->headOffset == 0 ? 14 : 0;
            if (pitch.midi >= 0 && !symbol.continuation && !performance.notes[symbol.noteIndex].tieStop)
            {
                auto &state = accidentalState[segment.staff];
                const int letter = (pitch.diatonic % 7 + 7) % 7;
                const auto found = state.find(pitch.diatonic);
                const int before =
                    found == state.end() ? keyAccidentals[static_cast<std::size_t>(letter)] : found->second;
                symbol.showAccidental = before != pitch.alteration;
                state[pitch.diatonic] = pitch.alteration;
                if (symbol.showAccidental)
                    symbol.accidentalOffset = accidentalColumn++ * 14;
            }
            auto &column = columns[measure][segment.tick];
            column.width = std::max(column.width, 58 + segment.headOffset + symbol.accidentalOffset +
                                                      (symbol.showAccidental ? 20 : 0) + symbol.rhythm.dots * 8);
            if (symbol.rhythm.denominator == 0)
                column.width =
                    std::max(column.width,
                             QFontMetricsF(smallFont).horizontalAdvance(QString("%1t").arg(symbol.ticks)) + 26);
            previous = &segment;
        }
        for (auto it = columns[measure].begin(); it != columns[measure].end(); ++it)
        {
            const auto next = std::next(it);
            const auto gap =
                (next == columns[measure].end() ? measures[measure].endTick : next->first) - it->first;
            it->second.width = std::max(it->second.width, 66 * std::clamp(std::sqrt(gap / 480.0), 0.75, 1.8));
            it->second.width += it->second.symbolPadding;
            bar.width += it->second.width;
        }
        bar.width = std::max(260.0, bar.width);
    }
    const double prefix = 130 + std::abs(options.keyFifths) * 15;
    const double available = ContentWidth - prefix;
    std::array<double, 2> staffOffsets{above[0], 0};
    double rowHeight = above[0] + StaffHeight + below[0] + 24;
    if (performance.primaryStaff == 1)
        rowHeight += lyricRows * 26;
    if (performance.staffCount == 2)
    {
        staffOffsets[1] = rowHeight + above[1];
        rowHeight += above[1] + StaffHeight + below[1] + 24;
        if (performance.primaryStaff == 2)
            rowHeight += lyricRows * 26;
    }
    rowHeight += 40;
    int row = 0;
    double used = 0;
    std::vector<double> rowEnd;
    for (auto &bar : bars)
    {
        if (bar.width > available)
            fail("messages.audio_import.notation_too_large");
        if (used > 0 && used + bar.width > available)
        {
            rowEnd.push_back(Margin + prefix + used);
            ++row;
            used = 0;
        }
        bar.row = row;
        bar.left = Margin + prefix + used;
        used += bar.width;
    }
    rowEnd.push_back(Margin + prefix + used);
    const int height = static_cast<int>(std::ceil(HeaderHeight + (row + 1) * rowHeight + Margin));
    if (height > MaximumHeight)
        fail("messages.audio_import.notation_too_large");
    for (std::size_t measure = 0; measure < bars.size(); ++measure)
    {
        const auto &bar = bars[measure];
        double baseWidth = 2 * BarPadding;
        for (const auto &[tick, column] : columns[measure])
            baseWidth += column.width;
        const double extra = (bar.width - baseWidth) / columns[measure].size();
        double left = bar.left + BarPadding;
        for (auto &[tick, column] : columns[measure])
        {
            column.width += extra;
            column.center = left + column.width / 2;
            left += column.width;
        }
        for (const auto index : bar.segments)
        {
            auto &segment = segments[index];
            auto &symbol = segment.symbol;
            const auto &column = columns[measure].at(segment.tick);
            symbol.row = bar.row;
            symbol.centerX = column.center + segment.headOffset + column.symbolPadding / 2;
            const auto measureStart = measure == 0 ? 0 : measures[measure - 1].endTick;
            if (symbol.pitch.midi < 0 && segment.tick == measureStart &&
                segment.tick + symbol.ticks == measures[measure].endTick)
                symbol.centerX = bar.left + bar.width / 2;
            symbol.cellWidth = column.width;
            symbol.staffTop = HeaderHeight + bar.row * rowHeight + staffOffsets[segment.staff];
            symbol.y = symbol.pitch.midi < 0 ? symbol.staffTop + StaffHeight / 2
                                             : pitchY(symbol.pitch, symbol.staffTop, segment.bassClef);
        }
    }
    std::map<std::tuple<int, std::int64_t, int, std::string>, std::vector<std::size_t>> chordGroups;
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        const auto &segment = segments[i];
        if (segment.symbol.pitch.midi >= 0)
            chordGroups[{segment.symbol.measure, segment.tick, segment.staff,
                         performance.notes[segment.symbol.noteIndex].voice}]
                .push_back(i);
    }
    const double headWidth = QFontMetricsF(notationFont).tightBoundingRect(QString(QChar(0xE0A4))).width();
    struct BeamChord
    {
        std::vector<std::size_t> indexes;
        std::map<int, StaffBeamKind> levels;
        StaffStemDirection explicitStem = StaffStemDirection::Auto;
        bool stemUp = true;
        double highest = 0;
        double lowest = 0;
    };
    std::vector<BeamChord> beamChords;
    std::map<std::tuple<int, int, int, std::string>, std::vector<std::size_t>> beamVoices;
    for (const auto &[key, indexes] : chordGroups)
    {
        double averageY = 0;
        double lowest = -std::numeric_limits<double>::infinity();
        double highest = std::numeric_limits<double>::infinity();
        for (const auto index : indexes)
        {
            const double y = segments[index].symbol.y;
            averageY += y;
            lowest = std::max(lowest, y);
            highest = std::min(highest, y);
        }
        auto &first = segments[indexes.front()];
        BeamChord chord;
        chord.indexes = indexes;
        chord.highest = highest;
        chord.lowest = lowest;
        for (const auto index : indexes)
        {
            const auto &segment = segments[index];
            const auto &note = performance.notes[segment.symbol.noteIndex];
            if (chord.explicitStem == StaffStemDirection::Auto && note.stemDirection != StaffStemDirection::Auto)
                chord.explicitStem = note.stemDirection;
            if (!segment.symbol.continuation)
                for (const auto &beam : note.beams)
                    chord.levels.try_emplace(beam.level, beam.kind);
        }
        const bool stemUp = chord.explicitStem == StaffStemDirection::Up ||
                            (chord.explicitStem != StaffStemDirection::Down &&
                             averageY / indexes.size() > first.symbol.staffTop + StaffHeight / 2);
        chord.stemUp = stemUp;
        const auto &firstColumn = columns[static_cast<std::size_t>(first.symbol.measure)].at(first.tick);
        const double commonX =
            firstColumn.center + firstColumn.symbolPadding / 2 + (stemUp ? 1 : -1) * (headWidth / 2 - 0.7);
        for (const auto index : indexes)
        {
            auto &segment = segments[index];
            segment.symbol.stemDirection = stemUp ? 1 : -1;
            segment.symbol.stemEndY = stemUp ? highest - 3.5 * StaffSpace : lowest + 3.5 * StaffSpace;
            segment.symbol.stemX = commonX;
            segment.symbol.drawStem = chord.explicitStem != StaffStemDirection::None;
            segment.symbol.suppressFlags = !chord.levels.empty();
            if (!stemUp)
                segment.symbol.centerX -= 2 * segment.headOffset;
        }
        beamVoices[{first.symbol.measure, first.symbol.row, first.staff,
                    performance.notes[first.symbol.noteIndex].voice}]
            .push_back(beamChords.size());
        beamChords.push_back(std::move(chord));
    }
    struct BeamRun
    {
        int level = 1;
        std::vector<std::size_t> chords;
    };
    struct BeamStub
    {
        int level = 1;
        std::size_t chord = 0;
        bool forward = true;
    };
    std::vector<BeamRun> beamRuns;
    std::vector<BeamStub> beamStubs;
    for (const auto &[voice, chords] : beamVoices)
    {
        for (int level = 1; level <= 8; ++level)
        {
            std::vector<std::size_t> open;
            StaffStemDirection openDirection = StaffStemDirection::Auto;
            const auto closeRun = [&]
            {
                if (open.size() > 1)
                    beamRuns.push_back({level, open});
                else if (open.size() == 1)
                {
                    const auto kind = beamChords[open.front()].levels.at(level);
                    beamStubs.push_back({level, open.front(), kind != StaffBeamKind::End});
                }
                open.clear();
                openDirection = StaffStemDirection::Auto;
            };
            for (const auto chordIndex : chords)
            {
                const auto &chord = beamChords[chordIndex];
                const auto beam = chord.levels.find(level);
                if (beam == chord.levels.end() || chord.explicitStem == StaffStemDirection::None)
                {
                    closeRun();
                    continue;
                }
                if (beam->second == StaffBeamKind::Begin)
                {
                    closeRun();
                    open.push_back(chordIndex);
                    openDirection = chord.explicitStem;
                }
                else if (beam->second == StaffBeamKind::Continue || beam->second == StaffBeamKind::End)
                {
                    if (chord.explicitStem != StaffStemDirection::Auto &&
                        openDirection != StaffStemDirection::Auto && chord.explicitStem != openDirection)
                        closeRun();
                    if (chord.explicitStem != StaffStemDirection::Auto)
                        openDirection = chord.explicitStem;
                    open.push_back(chordIndex);
                    if (beam->second == StaffBeamKind::End)
                        closeRun();
                }
                else
                {
                    closeRun();
                    beamStubs.push_back({level, chordIndex, beam->second == StaffBeamKind::ForwardHook});
                }
            }
            closeRun();
        }
    }
    for (const auto &run : beamRuns)
    {
        if (run.level != 1)
            continue;
        auto &firstChord = beamChords[run.chords.front()];
        auto &lastChord = beamChords[run.chords.back()];
        bool stemUp = firstChord.stemUp;
        for (const auto index : run.chords)
            if (beamChords[index].explicitStem == StaffStemDirection::Up ||
                beamChords[index].explicitStem == StaffStemDirection::Down)
            {
                stemUp = beamChords[index].explicitStem == StaffStemDirection::Up;
                break;
            }
        const auto stemXFor = [&](const BeamChord &chord)
        {
            const auto &segment = segments[chord.indexes.front()];
            const auto &column = columns[static_cast<std::size_t>(segment.symbol.measure)].at(segment.tick);
            return column.center + column.symbolPadding / 2 + (stemUp ? 1 : -1) * (headWidth / 2 - 0.7);
        };
        const double firstX = stemXFor(firstChord);
        const double lastX = stemXFor(lastChord);
        if (lastX <= firstX)
            continue;
        const double firstTarget = stemUp ? firstChord.highest - 42 : firstChord.lowest + 42;
        const double lastTarget = stemUp ? lastChord.highest - 42 : lastChord.lowest + 42;
        const double maximumSlope = std::min(0.12, StaffSpace / (lastX - firstX));
        const double slope =
            std::clamp((lastTarget - firstTarget) / (lastX - firstX), -maximumSlope, maximumSlope);
        double firstY = firstTarget;
        for (const auto index : run.chords)
        {
            const auto &chord = beamChords[index];
            const double x = stemXFor(chord);
            const double limit = stemUp ? chord.highest - 42 : chord.lowest + 42;
            const double intercept = limit - slope * (x - firstX);
            firstY = stemUp ? std::min(firstY, intercept) : std::max(firstY, intercept);
        }
        for (const auto index : run.chords)
        {
            auto &chord = beamChords[index];
            const auto &segment = segments[chord.indexes.front()];
            const auto &column = columns[static_cast<std::size_t>(segment.symbol.measure)].at(segment.tick);
            const double x = column.center + column.symbolPadding / 2 + (stemUp ? 1 : -1) * (headWidth / 2 - 0.7);
            chord.stemUp = stemUp;
            for (const auto member : chord.indexes)
            {
                auto &item = segments[member];
                item.symbol.stemDirection = stemUp ? 1 : -1;
                item.symbol.stemX = x;
                item.symbol.stemEndY = firstY + slope * (x - firstX);
                item.symbol.centerX =
                    column.center + column.symbolPadding / 2 + (stemUp ? item.headOffset : -item.headOffset);
            }
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
    QFont titleFont("Microsoft YaHei UI");
    titleFont.setPixelSize(32);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(Ink);
    painter.drawText(QRectF(Margin, 28, ContentWidth, 52), Qt::AlignCenter,
                     QFontMetricsF(titleFont).elidedText(QString::fromStdString(score.title), Qt::ElideRight,
                                                         static_cast<int>(ContentWidth)));
    painter.setFont(lyricFont);
    painter.setPen(MutedInk);
    painter.drawText(QPointF(Margin, 113), QString("%1     %2/%3     %4 = %5")
                                               .arg(keyName(options))
                                               .arg(score.beatsPerBar)
                                               .arg(score.beatUnit)
                                               .arg(QChar(0x2669))
                                               .arg(score.bpm, 0, 'g', 5));
    if (std::any_of(segments.begin(), segments.end(),
                    [](const GrandSegment &segment) { return segment.symbol.rhythm.denominator == 0; }))
    {
        painter.setFont(smallFont);
        painter.drawText(QRectF(ImageWidth - Margin - 230, 90, 230, 28), Qt::AlignRight,
                         QString("t = 1/%1 %2").arg(TicksPerQuarter).arg(QChar(0x2669)));
    }
    for (int line = 0; line <= row; ++line)
    {
        for (int staff = 0; staff < performance.staffCount; ++staff)
        {
            const double top = HeaderHeight + line * rowHeight + staffOffsets[staff];
            painter.setPen(QPen(QColor("#8996A8"), 1));
            for (int staffLine = 0; staffLine < 5; ++staffLine)
                painter.drawLine(QPointF(Margin, top + staffLine * StaffSpace),
                                 QPointF(rowEnd[static_cast<std::size_t>(line)], top + staffLine * StaffSpace));
            painter.setPen(Ink);
            auto staffOptions = options;
            const auto firstBar =
                std::find_if(bars.begin(), bars.end(), [line](const Bar &bar) { return bar.row == line; });
            const auto index = static_cast<std::size_t>(firstBar - bars.begin());
            const auto rowStart = index == 0 ? 0 : measures[index - 1].endTick;
            staffOptions.bassClef = bassAt(staff, rowStart);
            drawPrefix(painter, notationFont, staffOptions, score, top, line == 0);
        }
        if (performance.staffCount == 2)
        {
            const double top = HeaderHeight + line * rowHeight + staffOffsets[0];
            const double bottom = HeaderHeight + line * rowHeight + staffOffsets[1] + StaffHeight;
            const double middle = (top + bottom) / 2;
            QPainterPath brace;
            brace.moveTo(Margin - 16, top);
            brace.cubicTo(Margin - 33, top + 15, Margin - 10, middle - 15, Margin - 27, middle);
            brace.cubicTo(Margin - 10, middle + 15, Margin - 33, bottom - 15, Margin - 16, bottom);
            painter.setPen(QPen(Ink, 2.8));
            painter.drawPath(brace);
            painter.setPen(QPen(Ink, 1.4));
            painter.drawLine(QPointF(Margin, top), QPointF(Margin, bottom));
        }
    }
    std::vector<SourceRect> performanceAnchors(performance.notes.size());
    for (const auto &change : performance.clefChanges)
    {
        if (change.startTick == 0)
            continue;
        const auto measure = measureAt(change.startTick);
        const auto &bar = bars[measure];
        const auto &column = columns[measure].at(change.startTick);
        const double top = HeaderHeight + bar.row * rowHeight + staffOffsets[change.staff - 1];
        painter.setPen(Ink);
        drawGlyph(painter, notationFont, change.bassClef ? 0xE062 : 0xE050, column.center - column.width / 2 + 5,
                  top + (change.bassClef ? 1 : 3) * StaffSpace);
    }
    for (std::size_t measure = 1; measure < measures.size(); ++measure)
    {
        const auto &written = measures[measure];
        const auto &previous = measures[measure - 1];
        if (written.beatsPerBar == 0 ||
            (written.beatsPerBar == previous.beatsPerBar && written.beatUnit == previous.beatUnit))
            continue;
        const auto &bar = bars[measure];
        const auto &column = columns[measure].at(previous.endTick);
        for (int staff = 0; staff < performance.staffCount; ++staff)
        {
            const double top = HeaderHeight + bar.row * rowHeight + staffOffsets[staff];
            painter.setPen(Ink);
            drawMeter(painter, written.beatsPerBar, written.beatUnit,
                      column.center - column.width / 2 + column.symbolPadding - 44, top);
        }
    }
    std::map<std::pair<std::int64_t, int>, SourceRect> primaryAnchors;
    std::map<std::tuple<int, std::string, int>, const GrandSegment *> previousNotes;
    std::vector<const GrandSegment *> previousSegments(performance.notes.size(), nullptr);
    for (std::size_t measure = 0; measure < bars.size(); ++measure)
        for (const auto index : bars[measure].segments)
        {
            const auto &segment = segments[index];
            const auto &symbol = segment.symbol;
            const auto source = drawSymbol(painter, notationFont, smallFont, symbol);
            if (symbol.pitch.midi < 0)
            {
                if (segment.staff + 1 == performance.primaryStaff)
                    primaryAnchors.try_emplace(std::pair{segment.tick, -1}, source);
                continue;
            }
            const auto &note = performance.notes[symbol.noteIndex];
            if (!symbol.continuation)
            {
                performanceAnchors[symbol.noteIndex] = source;
                if (segment.staff + 1 == performance.primaryStaff)
                    primaryAnchors.try_emplace(std::pair{segment.tick, symbol.pitch.midi}, source);
            }
            const auto key = std::make_tuple(note.staff, note.voice, symbol.pitch.midi);
            const auto before = previousNotes.find(key);
            const GrandSegment *heldSegment = symbol.continuation             ? previousSegments[symbol.noteIndex]
                                              : before == previousNotes.end() ? nullptr
                                                                              : before->second;
            if (heldSegment && heldSegment->tick + heldSegment->symbol.ticks == segment.tick &&
                (symbol.continuation ||
                 (note.tieStop && performance.notes[heldSegment->symbol.noteIndex].tieStart)))
            {
                const auto &held = heldSegment->symbol;
                painter.setPen(QPen(Ink, 1.7));
                if (held.row == symbol.row)
                    drawTie(painter, held.centerX + 10, symbol.centerX - 10, held.y - 14, symbol.y - 14);
                else
                {
                    drawTie(painter, held.centerX + 10, rowEnd[static_cast<std::size_t>(held.row)], held.y - 14,
                            held.y - 14);
                    drawTie(painter, Margin + prefix - 10, symbol.centerX - 10, symbol.y - 14, symbol.y - 14);
                }
            }
            else if (note.tieStop && segment.tick == 0)
            {
                painter.setPen(QPen(Ink, 1.7));
                drawTie(painter, Margin + prefix - 10, symbol.centerX - 10, symbol.y - 14, symbol.y - 14);
            }
            previousNotes[key] = &segment;
            previousSegments[symbol.noteIndex] = &segment;
        }
    for (std::size_t i = 0; i < performance.notes.size(); ++i)
    {
        const auto &note = performance.notes[i];
        if (!note.tieStart || note.startTick + note.durationTicks != totalTicks || !previousSegments[i])
            continue;
        const auto &symbol = previousSegments[i]->symbol;
        painter.setPen(QPen(Ink, 1.7));
        drawTie(painter, symbol.centerX + 10, rowEnd[static_cast<std::size_t>(symbol.row)], symbol.y - 14,
                symbol.y - 14);
    }
    const auto paintBeam = [&](const Segment &first, const Segment &last, int level, double firstX, double lastX)
    {
        const bool up = first.stemDirection > 0;
        const double offset = (up ? 1 : -1) * (level - 1) * 7.0;
        const double thickness = up ? 4.5 : -4.5;
        QPainterPath polygon;
        polygon.moveTo(firstX, first.stemEndY + offset);
        polygon.lineTo(lastX, last.stemEndY + offset);
        polygon.lineTo(lastX, last.stemEndY + offset + thickness);
        polygon.lineTo(firstX, first.stemEndY + offset + thickness);
        polygon.closeSubpath();
        painter.fillPath(polygon, Ink);
    };
    for (const auto &run : beamRuns)
    {
        const auto &first = segments[beamChords[run.chords.front()].indexes.front()].symbol;
        const auto &last = segments[beamChords[run.chords.back()].indexes.front()].symbol;
        paintBeam(first, last, run.level, first.stemX, last.stemX);
    }
    for (const auto &stub : beamStubs)
    {
        const auto &symbol = segments[beamChords[stub.chord].indexes.front()].symbol;
        paintBeam(symbol, symbol, stub.level, symbol.stemX, symbol.stemX + (stub.forward ? 18 : -18));
    }
    std::vector<Anchor> guideAnchors(score.notes.size());
    int tonic = score.tonic;
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        const auto &note = score.notes[i];
        if (note.keyOverride >= 0)
            tonic = note.keyOverride;
        const auto measure = measureAt(guideStarts[i]);
        const auto &bar = bars[measure];
        const auto &column = columns[measure].at(guideStarts[i]);
        const int staff = performance.primaryStaff - 1;
        const double staffTop = HeaderHeight + bar.row * rowHeight + staffOffsets[staff];
        SourceRect source{column.center - 12, staffTop + StaffHeight / 2 - 10, 24, 20};
        const auto matched = primaryAnchors.find({guideStarts[i], midiPitch(note, tonic)});
        if (matched != primaryAnchors.end())
            source = matched->second;
        guideAnchors[i] = {source, bar.row, preserveMeasures ? note.measure : static_cast<int>(measure)};
        painter.setPen(Ink);
        painter.setFont(lyricFont);
        const auto verses = lyricVerses(note);
        for (std::size_t verse = 0; verse < verses.size(); ++verse)
            painter.drawText(QRectF(column.center - column.width / 2,
                                    staffTop + StaffHeight + below[staff] + verse * 26, column.width, 26),
                             Qt::AlignHCenter | Qt::AlignTop, QString::fromStdString(verses[verse]));
    }
    for (const auto &bar : bars)
    {
        const double top = HeaderHeight + bar.row * rowHeight + staffOffsets[0];
        const double bottom =
            HeaderHeight + bar.row * rowHeight + staffOffsets[performance.staffCount - 1] + StaffHeight;
        painter.setPen(QPen(Ink, 1.4));
        painter.drawLine(QPointF(bar.left + bar.width, top), QPointF(bar.left + bar.width, bottom));
        painter.setFont(smallFont);
        painter.setPen(MutedInk);
        painter.drawText(QPointF(bar.left + 3, HeaderHeight + bar.row * rowHeight + 18),
                         QString::number(bar.measureLabel + 1));
    }
    for (const auto &repeat : score.repeats)
    {
        const auto firstTick = guideStarts[repeat.firstNote];
        const auto endTick = guideStarts[repeat.endNote];
        const auto firstMeasure = measureAt(firstTick);
        const auto endMeasure = measureAt(endTick - 1);
        const auto &firstColumn = columns[firstMeasure].at(firstTick);
        const double firstX = firstColumn.center - firstColumn.width / 2 - 11;
        double endX = bars[endMeasure].left + bars[endMeasure].width;
        if (endTick < measures[endMeasure].endTick)
        {
            const auto &endColumn = columns[endMeasure].at(endTick);
            endX = endColumn.center - endColumn.width / 2 - 11;
        }
        for (int staff = 0; staff < performance.staffCount; ++staff)
        {
            drawRepeat(painter, firstX, HeaderHeight + bars[firstMeasure].row * rowHeight + staffOffsets[staff],
                       true);
            drawRepeat(painter, endX, HeaderHeight + bars[endMeasure].row * rowHeight + staffOffsets[staff],
                       false);
        }
    }
    painter.end();
    for (std::size_t i = 0; i < performance.notes.size(); ++i)
        performance.notes[i].source = performanceAnchors[i];
    for (std::size_t i = 0; i < score.notes.size(); ++i)
    {
        score.notes[i].source = guideAnchors[i].source;
        score.notes[i].line = guideAnchors[i].row;
        score.notes[i].measure = guideAnchors[i].measure;
    }
    return image;
}

std::vector<QImage> renderGrandStaffPages(Score &score, StaffPerformance &performance,
                                          const StaffRenderOptions &options)
{
    if (score.writtenMeasures.empty())
        return {renderGrandStaffScore(score, performance, options)};
    struct PageRange
    {
        std::int64_t start = 0;
        std::int64_t end = 0;
        bool present = false;
    };
    std::vector<PageRange> ranges;
    std::int64_t expectedStart = 0;
    int previousPage = 0;
    for (const auto &measure : score.writtenMeasures)
    {
        if (measure.pageIndex < previousPage || measure.pageIndex < 0 || measure.pageIndex > 31 ||
            measure.startTick != expectedStart || measure.durationTicks <= 0)
            fail("messages.audio_import.notation_invalid_score");
        ranges.resize(std::max(ranges.size(), static_cast<std::size_t>(measure.pageIndex + 1)));
        auto &range = ranges[static_cast<std::size_t>(measure.pageIndex)];
        if (!range.present)
            range.start = measure.startTick;
        range.end = measure.startTick + measure.durationTicks;
        range.present = true;
        expectedStart = range.end;
        previousPage = measure.pageIndex;
    }
    if (expectedStart != performance.durationTicks ||
        std::any_of(ranges.begin(), ranges.end(), [](const PageRange &range) { return !range.present; }))
        fail("messages.audio_import.notation_invalid_score");
    std::vector<std::int64_t> guideStarts{0};
    for (const auto &note : score.notes)
        guideStarts.push_back(guideStarts.back() + note.durationTicks);
    if (guideStarts.back() != expectedStart || !buildTimeline(score).valid())
        fail("messages.audio_import.notation_invalid_score");
    if (!buildStaffPerformancePlan(score, buildTimeline(score), performance).valid())
        fail("messages.audio_import.notation_invalid_score");
    std::vector<QImage> images;
    Score anchoredScore = score;
    StaffPerformance anchoredPerformance = performance;
    for (std::size_t page = 0; page < ranges.size(); ++page)
    {
        const auto &range = ranges[page];
        Score local = score;
        local.notes.clear();
        local.repeats.clear();
        local.writtenMeasures.clear();
        std::vector<std::size_t> guideMap;
        int tonic = score.tonic;
        for (std::size_t i = 0; i < score.notes.size(); ++i)
        {
            const auto &note = score.notes[i];
            if (note.keyOverride >= 0)
                tonic = note.keyOverride;
            const auto start = std::max(range.start, guideStarts[i]);
            const auto end = std::min(range.end, guideStarts[i + 1]);
            if (start >= end)
                continue;
            auto clipped = note;
            clipped.durationTicks = static_cast<int>(end - start);
            clipped.pageIndex = 0;
            if (local.notes.empty())
                clipped.keyOverride = tonic;
            local.notes.push_back(clipped);
            guideMap.push_back(i);
        }
        for (const auto &measure : score.writtenMeasures)
        {
            if (measure.pageIndex != static_cast<int>(page))
                continue;
            auto clipped = measure;
            clipped.startTick -= range.start;
            clipped.pageIndex = 0;
            local.writtenMeasures.push_back(clipped);
        }
        local.beatsPerBar = local.writtenMeasures.front().beatsPerBar;
        local.beatUnit = local.writtenMeasures.front().beatUnit;
        StaffPerformance visual = performance;
        visual.notes.clear();
        visual.clefChanges.clear();
        visual.durationTicks = range.end - range.start;
        visual.timingFingerprint = staffTimingFingerprint(local);
        std::vector<std::size_t> performanceMap;
        for (std::size_t i = 0; i < performance.notes.size(); ++i)
        {
            const auto &note = performance.notes[i];
            const auto start = std::max(range.start, note.startTick);
            const auto end = std::min(range.end, note.startTick + note.durationTicks);
            if (start >= end)
                continue;
            auto clipped = note;
            clipped.startTick = start - range.start;
            clipped.durationTicks = end - start;
            clipped.pageIndex = 0;
            clipped.tieStop = note.tieStop || note.startTick < range.start;
            clipped.tieStart = note.tieStart || note.startTick + note.durationTicks > range.end;
            visual.notes.push_back(clipped);
            performanceMap.push_back(i);
        }
        for (int staff = 1; staff <= performance.staffCount; ++staff)
        {
            bool bass = performance.staffCount == 2 ? staff == 2 : options.bassClef;
            std::int64_t latest = -1;
            for (const auto &change : performance.clefChanges)
            {
                if (change.staff == staff && change.startTick <= range.start && change.startTick >= latest)
                {
                    bass = change.bassClef;
                    latest = change.startTick;
                }
            }
            visual.clefChanges.push_back({0, staff, bass});
        }
        for (const auto &change : performance.clefChanges)
            if (change.startTick > range.start && change.startTick < range.end)
                visual.clefChanges.push_back({change.startTick - range.start, change.staff, change.bassClef});
        // Visual slices can begin/end inside a held tone. Audio validates only the untouched global plan.
        images.push_back(renderGrandStaffScore(local, visual, options));
        for (std::size_t i = 0; i < guideMap.size(); ++i)
        {
            const auto original = guideMap[i];
            if (guideStarts[original] < range.start)
                continue;
            anchoredScore.notes[original].source = local.notes[i].source;
            anchoredScore.notes[original].line = local.notes[i].line;
            anchoredScore.notes[original].pageIndex = static_cast<int>(page);
        }
        for (std::size_t i = 0; i < performanceMap.size(); ++i)
        {
            const auto original = performanceMap[i];
            if (performance.notes[original].startTick < range.start)
                continue;
            anchoredPerformance.notes[original].source = visual.notes[i].source;
            anchoredPerformance.notes[original].pageIndex = static_cast<int>(page);
        }
    }
    score = std::move(anchoredScore);
    performance = std::move(anchoredPerformance);
    return images;
}

} // namespace singlilt

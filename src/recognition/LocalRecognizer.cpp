// Local numbered-notation recognition from score images.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "LocalRecognizer.h"
#include "WindowsOcr.h"
#include "domain/NumberedPerformance.h"
#include "i18n/LanguageManager.h"

#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace singlilt
{
namespace
{
struct Component
{
    QRect box;
    int area = 0;
};
struct Row
{
    double y = 0;
    double height = 0;
    int count = 0;
};
struct Glyph
{
    QRect box;
    int digit = 0;
    double score = 0;
};
struct Template
{
    int digit = 0;
    QImage pixels;
    double aspect = 0;
};
struct RepeatMarker
{
    std::size_t note = 0;
    bool opening = false;
    int line = 0;
};
struct EndingMarker
{
    std::size_t note = 0;
    int number = 0;
    int line = 0;
};
struct Raster
{
    QImage gray;
    int threshold = 150;
    std::vector<Component> components;
    bool ink(int x, int y) const
    {
        return x >= 0 && x < gray.width() && y >= 0 && y < gray.height() && gray.constScanLine(y)[x] < threshold;
    }
};

Raster makeRaster(const QImage &input)
{
    Raster raster;
    raster.gray = input.convertToFormat(QImage::Format_Grayscale8);
    std::array<int, 256> histogram{};
    for (int y = 0; y < raster.gray.height(); ++y)
        for (int x = 0; x < raster.gray.width(); ++x)
            ++histogram[raster.gray.constScanLine(y)[x]];
    const double count = double(raster.gray.width()) * raster.gray.height();
    double sum = 0;
    for (int i = 0; i < 256; ++i)
        sum += i * double(histogram[i]);
    double leftSum = 0, leftCount = 0, best = 0;
    for (int i = 0; i < 255; ++i)
    {
        leftCount += histogram[i];
        leftSum += i * double(histogram[i]);
        if (!leftCount || leftCount == count)
            continue;
        const double difference = leftSum / leftCount - (sum - leftSum) / (count - leftCount);
        const double variance = leftCount * (count - leftCount) * difference * difference;
        if (variance > best)
        {
            best = variance;
            raster.threshold = i + 1;
        }
    }
    raster.threshold = std::clamp(raster.threshold, 90, 210);
    const int width = raster.gray.width(), height = raster.gray.height();
    std::vector<unsigned char> seen(static_cast<std::size_t>(width) * height);
    std::vector<int> pending;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int start = y * width + x;
            if (seen[start] || !raster.ink(x, y))
                continue;
            seen[start] = 1;
            pending.clear();
            pending.push_back(start);
            int left = x, right = x, top = y, bottom = y;
            for (std::size_t i = 0; i < pending.size(); ++i)
            {
                const int px = pending[i] % width, py = pending[i] / width;
                left = std::min(left, px);
                right = std::max(right, px);
                top = std::min(top, py);
                bottom = std::max(bottom, py);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int nx = px + dx, ny = py + dy;
                        if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                            continue;
                        const int next = ny * width + nx;
                        if (!seen[next] && raster.ink(nx, ny))
                        {
                            seen[next] = 1;
                            pending.push_back(next);
                        }
                    }
            }
            if (pending.size() > 1)
                raster.components.push_back(
                    {QRect(QPoint(left, top), QPoint(right, bottom)), int(pending.size())});
        }
    return raster;
}

QRect tightBox(const Raster &raster, QRect region)
{
    region = region.intersected(raster.gray.rect());
    int left = region.right() + 1, top = region.bottom() + 1, right = -1, bottom = -1;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
            if (raster.ink(x, y))
            {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
    return right < left ? QRect{} : QRect(QPoint(left, top), QPoint(right, bottom));
}

QImage normalized(const Raster &raster, const QRect &box)
{
    QImage binary(box.size(), QImage::Format_Grayscale8);
    binary.fill(255);
    for (int y = 0; y < box.height(); ++y)
        for (int x = 0; x < box.width(); ++x)
            binary.scanLine(y)[x] = raster.ink(box.x() + x, box.y() + y) ? 0 : 255;
    return binary.scaled(20, 28, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

double difference(const QImage &a, const QImage &b)
{
    double difference = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            difference += std::abs(int(a.constScanLine(y)[x]) - int(b.constScanLine(y)[x]));
    return difference / (255.0 * a.width() * a.height());
}

bool onlyDigits(const QString &text)
{
    return !text.isEmpty() && std::all_of(text.begin(), text.end(),
                                          [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('7'); });
}

std::vector<QRect> segments(const Raster &raster, const QRect &region)
{
    std::vector<QRect> result;
    int start = -1;
    for (int x = region.left(); x <= region.right() + 1; ++x)
    {
        bool black = false;
        if (x <= region.right())
            for (int y = region.top(); y <= region.bottom(); ++y)
                if (raster.ink(x, y))
                {
                    black = true;
                    break;
                }
        if (black && start < 0)
            start = x;
        if (!black && start >= 0)
        {
            auto box = tightBox(raster, QRect(start, region.top(), x - start, region.height()));
            if (!box.isEmpty())
                result.push_back(box);
            start = -1;
        }
    }
    return result;
}

std::vector<Row> musicRows(const OcrText &ocr)
{
    std::vector<Row> rows;
    std::vector<OcrWord> words;
    for (const auto &word : ocr.words)
        if (onlyDigits(word.text) && word.box.height() >= 6 &&
            word.box.width() / word.text.size() < word.box.height() * 1.2)
            words.push_back(word);
    std::sort(words.begin(), words.end(),
              [](const auto &a, const auto &b) { return a.box.center().y() < b.box.center().y(); });
    for (const auto &word : words)
    {
        auto found = std::find_if(
            rows.begin(), rows.end(), [&](const Row &row)
            { return std::abs(row.y - word.box.y()) < std::min(row.height, word.box.height()) * 0.42; });
        if (found == rows.end())
            rows.push_back({word.box.y(), word.box.height(), int(word.text.size())});
        else
        {
            const int total = found->count + int(word.text.size());
            found->y = (found->y * found->count + word.box.y() * word.text.size()) / total;
            found->height = (found->height * found->count + word.box.height() * word.text.size()) / total;
            found->count = total;
        }
    }
    std::erase_if(rows, [](const Row &row) { return row.count < 6; });
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.y < b.y; });
    return rows;
}

std::vector<Template> fontTemplates()
{
    std::vector<Template> result;
    for (const QString family :
         {QStringLiteral("Times New Roman"), QStringLiteral("Arial"), QStringLiteral("SimSun")})
        for (bool bold : {false, true})
            for (bool italic : {false, true})
                for (int digit = 0; digit <= 7; ++digit)
                {
                    QImage image(60, 70, QImage::Format_RGB32);
                    image.fill(Qt::white);
                    {
                        QPainter painter(&image);
                        QFont font(family);
                        font.setPixelSize(48);
                        font.setBold(bold);
                        font.setItalic(italic);
                        painter.setFont(font);
                        painter.setPen(Qt::black);
                        painter.drawText(QRect(0, 0, 60, 70), Qt::AlignCenter, QString::number(digit));
                    }
                    const auto raster = makeRaster(image);
                    const QRect box = tightBox(raster, image.rect());
                    if (!box.isEmpty())
                        result.push_back({digit, normalized(raster, box), double(box.width()) / box.height()});
                }
    return result;
}

Glyph classify(const Raster &raster, const QRect &box, const std::vector<Template> &templates)
{
    Glyph result{box, 0, 1.0};
    const auto pixels = normalized(raster, box);
    const double aspect = double(box.width()) / box.height();
    for (const auto &sample : templates)
    {
        const double score = difference(pixels, sample.pixels) + 0.15 * std::abs(aspect - sample.aspect);
        if (score < result.score)
        {
            result.digit = sample.digit;
            result.score = score;
        }
    }
    return result;
}

bool dot(const Component &component, double h)
{
    return component.box.width() <= h * 0.43 && component.box.height() <= h * 0.43 && component.box.width() >= 2 &&
           component.box.height() >= 2 && component.area >= 3 &&
           double(component.box.width()) / component.box.height() < 2.0;
}

int underlineCount(const Raster &raster, const QRect &box)
{
    const int center = box.center().x();
    int count = 0;
    bool inLine = false;
    for (int y = box.bottom() + 1; y <= box.bottom() + std::lround(box.height() * 0.7); ++y)
    {
        int left = center, right = center;
        while (raster.ink(left - 1, y) && center - left < box.height() * 5)
            --left;
        while (raster.ink(right + 1, y) && right - center < box.height() * 5)
            ++right;
        const bool horizontal = raster.ink(center, y) && right - left + 1 >= std::max(5.0, box.width() * 0.65);
        if (horizontal && !inLine)
            ++count;
        inLine = horizontal;
    }
    return std::min(count, 3);
}

bool cjk(QChar character)
{
    return character.unicode() >= 0x3400 && character.unicode() <= 0x9fff;
}
int letterKey(QChar letter)
{
    switch (letter.toUpper().unicode())
    {
    case 'C':
        return 0;
    case 'D':
        return 2;
    case 'E':
        return 4;
    case 'F':
        return 5;
    case 'G':
        return 7;
    case 'A':
        return 9;
    case 'B':
        return 11;
    default:
        return -1;
    }
}

std::vector<Template> letterTemplates()
{
    std::vector<Template> result;
    for (const QString family : {QStringLiteral("Times New Roman"), QStringLiteral("Arial")})
        for (bool bold : {false, true})
            for (char letter = 'A'; letter <= 'G'; ++letter)
            {
                QImage image(70, 80, QImage::Format_RGB32);
                image.fill(Qt::white);
                {
                    QPainter painter(&image);
                    QFont font(family);
                    font.setPixelSize(48);
                    font.setBold(bold);
                    painter.setFont(font);
                    painter.setPen(Qt::black);
                    painter.drawText(image.rect(), Qt::AlignCenter, QString(QChar(letter)));
                }
                const auto raster = makeRaster(image);
                const auto box = tightBox(raster, image.rect());
                result.push_back({letter, normalized(raster, box), double(box.width()) / box.height()});
            }
    return result;
}

void rasterMetadata(const Raster &raster, const Row &row, RecognitionResult &result, bool &keyFound,
                    bool &meterFound)
{
    const auto letters = letterTemplates();
    const auto numbers = fontTemplates();
    for (const auto &upper : raster.components)
        for (const auto &lower : raster.components)
        {
            const auto &a = upper.box;
            const auto &b = lower.box;
            if (a.top() > row.y - row.height || a.left() > raster.gray.width() * 0.35 || a.width() < 5 ||
                a.width() < a.height() * 2.5)
                continue;
            if (std::abs(a.left() - b.left()) > 2 || std::abs(a.width() - b.width()) > 3 ||
                b.top() <= a.bottom() || b.top() - a.bottom() > a.width() * 0.8 || b.width() < b.height() * 2.5)
                continue;
            const double centerY = (a.top() + b.bottom()) * 0.5;
            const bool precedingOne = std::any_of(
                raster.components.begin(), raster.components.end(),
                [&](const Component &component)
                {
                    if (component.box.right() >= a.left() || a.left() - component.box.right() > a.width() * 1.5 ||
                        std::abs(component.box.center().y() - centerY) > component.box.height() * 0.4)
                        return false;
                    const auto glyph = classify(raster, component.box, numbers);
                    return glyph.digit == 1 && glyph.score < 0.3;
                });
            if (!precedingOne)
                continue;
            double best = 0.27;
            const Component *chosen = nullptr;
            int keyLetter = 0;
            for (const auto &candidate : raster.components)
            {
                const auto &c = candidate.box;
                if (c.left() < a.right() || c.left() > a.right() + a.width() * 3.3 ||
                    c.height() < a.width() * 0.75 || c.height() > a.width() * 2.4 ||
                    std::abs(c.center().y() - centerY) > c.height() * 0.4 || c.width() < c.height() * 0.5)
                    continue;
                auto glyph = classify(raster, c, letters);
                if (glyph.score < best)
                {
                    best = glyph.score;
                    chosen = &candidate;
                    keyLetter = glyph.digit;
                }
            }
            if (chosen)
            {
                result.score.tonic = letterKey(QChar(keyLetter));
                keyFound = true;
                for (const auto &accidental : raster.components)
                {
                    const auto &sign = accidental.box;
                    if (sign.left() <= a.right() || sign.right() >= chosen->box.left() ||
                        sign.height() < chosen->box.height() * 0.65 || sign.top() > chosen->box.center().y() ||
                        sign.bottom() < chosen->box.top())
                        continue;
                    int longColumns = 0;
                    for (int x = sign.left(); x <= sign.right(); ++x)
                    {
                        int count = 0;
                        for (int y = sign.top(); y <= sign.bottom(); ++y)
                            count += raster.ink(x, y);
                        if (count > sign.height() * 0.55)
                            ++longColumns;
                    }
                    result.score.tonic = (result.score.tonic + (longColumns >= 2 ? 1 : 11)) % 12;
                }
            }
        }
    // Stacked meter has two compact numeral glyphs sharing the same center.
    for (const auto &upper : raster.components)
        for (const auto &lower : raster.components)
        {
            const auto &a = upper.box;
            const auto &b = lower.box;
            if (a.top() >= row.y - row.height || a.left() >= raster.gray.width() * 0.4 || a.height() < 7 ||
                a.height() > row.height * 1.3 || a.width() < a.height() * 0.4)
                continue;
            if (b.top() <= a.bottom() || b.top() - a.bottom() > a.height() * 0.9 ||
                b.height() < a.height() * 0.7 || b.height() > a.height() * 1.35 ||
                std::abs(a.center().x() - b.center().x()) > a.width() * 0.3)
                continue;
            const auto numerator = classify(raster, a, numbers), denominator = classify(raster, b, numbers);
            if (numerator.score < 0.38 && denominator.score < 0.38 && numerator.digit >= 2 &&
                (denominator.digit == 2 || denominator.digit == 4))
            {
                result.score.beatsPerBar = numerator.digit;
                result.score.beatUnit = denominator.digit;
                meterFound = true;
            }
        }
}
void metadata(const QImage &image, const Raster &raster, const OcrText &full, const std::vector<Row> &rows,
              RecognitionResult &result)
{
    if (rows.empty())
        return;
    const int headerEnd = std::max(1, int(rows.front().y - rows.front().height * 0.7));
    const int headerWidth = std::max(1, image.width() / 3);
    const QRect headerBox =
        tightBox(raster, QRect(0, 0, headerWidth, headerEnd)).adjusted(-8, -8, 8, 8).intersected(image.rect());
    const auto header = recognizeWindowsText(
        image.copy(headerBox).scaledToHeight(headerBox.height() * 4, Qt::SmoothTransformation),
        QStringLiteral("en-US"));
    result.debugText += QStringLiteral("\n--- Enlarged header ---\n") + header.text + header.error;
    QString condensed = header.text;
    condensed.remove(QRegularExpression(QStringLiteral("\\s")));
    auto keyMatch = QRegularExpression(QStringLiteral("[1lI][=＝]([#♯b♭]?)([A-Ga-g])")).match(condensed);
    bool keyFound = keyMatch.hasMatch();
    if (keyFound)
    {
        result.score.tonic = letterKey(keyMatch.captured(2).at(0));
        if (keyMatch.captured(1) == QStringLiteral("#") || keyMatch.captured(1) == QStringLiteral("♯"))
            ++result.score.tonic;
        if (keyMatch.captured(1) == QStringLiteral("b") || keyMatch.captured(1) == QStringLiteral("♭"))
            --result.score.tonic;
        result.score.tonic = (result.score.tonic + 12) % 12;
    }
    QString tempoText = condensed + full.text;
    tempoText.remove(QRegularExpression(QStringLiteral("\\s")));
    auto tempo = QRegularExpression(QStringLiteral("[=＝](\\d{2,3})(?!\\d)")).match(tempoText);
    if (tempo.hasMatch() && tempo.captured(1).toInt() >= 20 && tempo.captured(1).toInt() <= 300)
        result.score.bpm = tempo.captured(1).toInt();
    else
        result.warnings << trText("messages.recognition.tempo_unknown");
    bool meterFound = false;
    auto meter = QRegularExpression(QStringLiteral("(2|3|4|6|9|12)[/／](2|4|8|16)")).match(condensed);
    if (meter.hasMatch())
    {
        result.score.beatsPerBar = meter.captured(1).toInt();
        result.score.beatUnit = meter.captured(2).toInt();
        meterFound = true;
    }
    for (const auto &a : header.words)
        for (const auto &b : header.words)
        {
            bool okayA = false, okayB = false;
            const int numerator = a.text.toInt(&okayA), denominator = b.text.toInt(&okayB);
            if (!okayA || !okayB || numerator < 2 || numerator > 12 ||
                (denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16))
                continue;
            if (std::abs(a.box.center().x() - b.box.center().x()) < a.box.width() * 0.5 &&
                b.box.top() > a.box.top() + a.box.height() * 0.7 &&
                b.box.top() < a.box.bottom() + a.box.height() * 1.2)
            {
                result.score.beatsPerBar = numerator;
                result.score.beatUnit = denominator;
                meterFound = true;
            }
        }
    // A spatial key fallback tolerates OCR splitting or omitting the equals sign.
    for (const auto &letter : header.words)
        if (letter.text.size() == 1 && letterKey(letter.text[0]) >= 0)
        {
            for (const auto &one : header.words)
                if (one.text == QStringLiteral("1") &&
                    std::abs(one.box.center().y() - letter.box.center().y()) < letter.box.height() * 0.6 &&
                    letter.box.left() > one.box.right() &&
                    letter.box.left() - one.box.right() < letter.box.height() * 3)
                {
                    result.score.tonic = letterKey(letter.text[0]);
                    keyFound = true;
                    const double left = one.box.right(), right = letter.box.left();
                    for (const auto &sign : header.words)
                        if (sign.box.center().x() > left && sign.box.center().x() < right &&
                            std::abs(sign.box.center().y() - letter.box.center().y()) < letter.box.height() &&
                            (sign.text.contains('#') || sign.text.contains(QChar(0x266f))))
                            ++result.score.tonic;
                }
        }
    rasterMetadata(raster, rows.front(), result, keyFound, meterFound);
    if (!keyFound)
        result.warnings << trText("messages.recognition.key_unknown");
    if (!meterFound)
        result.warnings << trText("messages.recognition.meter_unknown");
    std::vector<OcrWord> title;
    for (const auto &word : full.words)
        if (word.box.bottom() < headerEnd && word.box.height() > rows.front().height * 1.7 &&
            std::any_of(word.text.begin(), word.text.end(), cjk))
            title.push_back(word);
    std::sort(title.begin(), title.end(), [](const auto &a, const auto &b) { return a.box.x() < b.box.x(); });
    QString name;
    for (const auto &word : title)
        name += word.text;
    if (!name.isEmpty())
        result.score.title = name.toStdString();
}
std::vector<NumberedSystem> numberedSystems(const Raster &raster, const std::vector<Row> &rows, Score &score,
                                            double sourceScale)
{
    // A brace is a curved, tall component left of both note rows, not a barline.
    std::vector<NumberedSystem> systems;
    bool braced = false;
    std::vector<Component> braces;
    for (const auto &component : raster.components)
        if (component.box.height() > rows.front().height * 1.5 &&
            component.box.width() >= rows.front().height * 0.25 &&
            component.box.width() <= rows.front().height * 1.5 &&
            double(component.area) / (component.box.width() * component.box.height()) < 0.65)
        {
            // Downsampling can disconnect the two thin halves of a printed brace.
            auto adjacent =
                std::find_if(braces.begin(), braces.end(),
                             [&](const Component &before)
                             {
                                 return std::abs(before.box.center().x() - component.box.center().x()) <
                                            rows.front().height * 0.4 &&
                                        component.box.top() > before.box.bottom() &&
                                        component.box.top() - before.box.bottom() < rows.front().height * 1.5;
                             });
            if (adjacent == braces.end())
                braces.push_back(component);
            else
            {
                adjacent->box = adjacent->box.united(component.box);
                adjacent->area += component.area;
            }
        }
    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        const auto &upper = rows[rowIndex];
        std::size_t lastRow = rowIndex;
        for (const auto &component : braces)
        {
            const auto &box = component.box;
            if (box.height() < upper.height * 3 || box.width() < upper.height * 0.25 ||
                box.width() > upper.height * 1.5 || box.top() > upper.y + upper.height * 0.3 ||
                box.top() < upper.y - upper.height * 2.5 ||
                double(component.area) / (box.width() * box.height()) > 0.65)
                continue;
            std::size_t end = rowIndex;
            while (end + 1 < rows.size() && rows[end + 1].y + rows[end + 1].height * 0.6 < box.bottom())
                ++end;
            if (end == rowIndex || box.bottom() > rows[end].y + rows[end].height * 2.0)
                continue;
            const bool leftOfNotes =
                std::all_of(score.notes.begin(), score.notes.end(),
                            [&](const Note &note)
                            {
                                return note.line < int(rowIndex) || note.line > int(end) ||
                                       box.right() < note.source.x / sourceScale - upper.height * 0.3;
                            });
            if (leftOfNotes)
                lastRow = std::max(lastRow, end);
        }
        NumberedSystem system{int(rowIndex), -1, {}};
        if (lastRow > rowIndex)
        {
            braced = true;
            // Extra digit rows inside a hand are vertically stacked chord tones.
            std::size_t split = rowIndex;
            for (std::size_t row = rowIndex + 1; row < lastRow; ++row)
                if (rows[row + 1].y - rows[row].y > rows[split + 1].y - rows[split].y)
                    split = row;
            system.upperLine = int(split);
            system.lowerLine = int(lastRow);
            for (auto &note : score.notes)
                if (note.line >= int(rowIndex) && note.line <= int(lastRow))
                    note.line = note.line <= int(split) ? system.upperLine : system.lowerLine;
        }
        const auto &bottom = rows[lastRow];
        double firstX = raster.gray.width(), finalX = 0;
        for (const auto &note : score.notes)
            if (note.line == system.upperLine || note.line == system.lowerLine)
            {
                firstX = std::min(firstX, (note.source.x + note.source.width / 2) / sourceScale);
                finalX = std::max(finalX, (note.source.x + note.source.width / 2) / sourceScale);
            }
        for (const auto &component : raster.components)
        {
            const auto &box = component.box;
            if (box.center().x() <= firstX || box.center().x() >= finalX || box.width() > bottom.height * 0.3 ||
                box.top() > bottom.y - bottom.height * 0.25 || box.bottom() < bottom.y + bottom.height * 1.15)
                continue;
            const double x = box.center().x() * sourceScale;
            system.barlines.push_back(x);
        }
        std::sort(system.barlines.begin(), system.barlines.end());
        system.barlines.erase(std::unique(system.barlines.begin(), system.barlines.end(),
                                          [&](double a, double b) { return b - a < bottom.height * sourceScale; }),
                              system.barlines.end());
        systems.push_back(std::move(system));
        rowIndex = lastRow;
    }
    if (!braced)
        systems.clear();
    return systems;
}

void recoverNumberedChords(const Raster &raster, const std::vector<Template> &templates,
                           const std::vector<NumberedSystem> &systems, double sourceScale, Score &score)
{
    const auto originalCount = score.notes.size();
    for (std::size_t i = 0; i < originalCount; ++i)
    {
        const auto base = score.notes[i];
        const bool hand = std::any_of(
            systems.begin(), systems.end(), [&](const NumberedSystem &system)
            { return system.lowerLine >= 0 && (base.line == system.upperLine || base.line == system.lowerLine); });
        if (!hand || base.degree == 0)
            continue;
        const double height = base.source.height / sourceScale;
        const double top = base.source.y / sourceScale;
        const double center = (base.source.x + base.source.width / 2) / sourceScale;
        for (const auto &component : raster.components)
        {
            const auto &box = component.box;
            if (box.height() < height * 0.75 || box.height() > height * 1.3 || box.width() < height * 0.23 ||
                box.width() > height || box.bottom() >= top - height * 0.3 || box.top() < top - height * 3 ||
                std::abs(box.center().x() - center) > height * 0.3)
                continue;
            const SourceRect source{box.x() * sourceScale, box.y() * sourceScale, box.width() * sourceScale,
                                    box.height() * sourceScale};
            const bool recognized =
                std::any_of(score.notes.begin(), score.notes.end(),
                            [&](const Note &note)
                            {
                                return std::abs(note.source.x - source.x) < source.width * 0.3 &&
                                       std::abs(note.source.y - source.y) < source.height * 0.3;
                            });
            if (recognized)
                continue;
            const auto glyph = classify(raster, box, templates);
            if (glyph.digit == 0 || glyph.score > 0.25)
                continue;
            Note chord = base;
            chord.degree = glyph.digit;
            chord.source = source;
            chord.tieToNext = false;
            chord.lyric.clear();
            chord.verseLyrics.clear();
            chord.octave = 0;
            for (const auto &mark : raster.components)
                if (dot(mark, int(height)) && std::abs(mark.box.center().x() - box.center().x()) < height * 0.32)
                {
                    if (mark.box.bottom() < box.top() && mark.box.top() >= box.top() - height * 1.5)
                        ++chord.octave;
                    if (mark.box.top() > box.bottom() && mark.box.bottom() <= box.bottom() + height)
                        --chord.octave;
                }
            score.notes.push_back(std::move(chord));
        }
    }
    for (auto &repeat : score.repeats)
        if (repeat.endNote == originalCount)
            repeat.endNote = score.notes.size();
}

} // namespace

RecognitionResult LocalRecognizer::recognize(const QImage &input, const QString &imagePath)
{
    RecognitionResult result;
    result.score.imagePath = imagePath.toStdString();
    result.score.title = QFileInfo(imagePath).completeBaseName().toStdString();
    if (input.isNull())
    {
        result.warnings << trText("messages.recognition.invalid_image");
        return result;
    }
    // Cap working pixels but preserve anchors in original-image coordinates.
    QImage image = input.width() > 2200 || input.height() > 3200
                       ? input.scaled(2200, 3200, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                       : input;
    // Normalize large portrait pages while keeping original-image anchors.
    if (image.width() > 1100 && image.height() > image.width() * 1.1)
        image = image.scaledToWidth(900, Qt::FastTransformation);
    auto ocr = recognizeWindowsText(image);
    auto rows = musicRows(ocr);
    // Normalize glyph size, not page dimensions: low-resolution OCR often drops
    // entire notation rows. Enlargement helps detection but cannot recover lost strokes.
    if (!rows.empty())
    {
        std::vector<double> heights;
        for (const auto &row : rows)
            heights.push_back(row.height);
        std::sort(heights.begin(), heights.end());
        const double median = heights[heights.size() / 2];
        if (median < 12)
        {
            const double factor = std::min({14.5 / median, 2200.0 / image.width(), 3200.0 / image.height()});
            image = image.scaledToWidth(std::max(1, int(std::lround(image.width() * factor))),
                                        Qt::SmoothTransformation);
            ocr = recognizeWindowsText(image);
            rows = musicRows(ocr);
            if (median < 12)
                result.warnings << trText("messages.recognition.small_notes");
        }
    }
    const double sourceScale = double(input.width()) / image.width();
    const auto raster = makeRaster(image);
    result.debugText = QStringLiteral("Windows OCR language: %1\n%2").arg(ocr.language, ocr.text);
    if (!ocr.error.isEmpty())
        result.warnings << ocr.error;
    auto templates = fontTemplates();
    // The score's own confidently read digits adapt templates to its print font.
    for (const auto &word : ocr.words)
        if (onlyDigits(word.text))
        {
            auto row = std::find_if(rows.begin(), rows.end(), [&](const Row &row)
                                    { return std::abs(row.y - word.box.y()) < row.height * 0.4; });
            if (row == rows.end() || std::abs(word.box.height() - row->height) > row->height * 0.35)
                continue;
            const auto pieces = segments(raster, word.box.toAlignedRect());
            if (pieces.size() != std::size_t(word.text.size()))
                continue;
            for (std::size_t i = 0; i < pieces.size(); ++i)
                if (pieces[i].height() >= row->height * 0.7)
                    templates.push_back({word.text[int(i)].digitValue(), normalized(raster, pieces[i]),
                                         double(pieces[i].width()) / pieces[i].height()});
        }
    if (rows.empty())
    {
        // No OCR language is required for clean, unrotated printed digits.
        for (const auto &component : raster.components)
        {
            const auto &box = component.box;
            if (box.height() < 9 || box.height() > image.height() / 20 || box.width() < box.height() * 0.22 ||
                box.width() > box.height() * 0.86)
                continue;
            if (classify(raster, box, templates).score > 0.18)
                continue;
            auto found = std::find_if(rows.begin(), rows.end(), [&](const Row &row)
                                      { return std::abs(row.y - box.y()) < row.height * 0.3; });
            if (found == rows.end())
                rows.push_back({double(box.y()), double(box.height()), 1});
            else
                ++found->count;
        }
        std::erase_if(rows, [](const Row &row) { return row.count < 8; });
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.y < b.y; });
    }
    if (rows.empty())
    {
        result.warnings << trText("messages.recognition.no_rows");
        return result;
    }
    metadata(image, raster, ocr, rows, result);
    int measure = 0, uncertain = 0;
    bool hasRepeatSymbols = false, hasEnding = false;
    std::vector<RepeatMarker> repeatMarkers;
    std::vector<EndingMarker> endingMarkers;
    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        const Row row = rows[rowIndex];
        const int top = int(std::lround(row.y));
        const int height = int(std::lround(row.height));
        double lyricTop = top + height * 2.2;
        for (const auto &word : ocr.words)
            // OCR can label a duration underline as the CJK character "一".
            // Only character-height words may bound the lyric region; thin
            // underline boxes must not exclude octave dots below the notes.
            if (word.box.top() > top + height * 1.05 && word.box.top() < lyricTop &&
                word.box.height() >= height * 0.6 && std::any_of(word.text.begin(), word.text.end(), cjk))
                lyricTop = word.box.top();
        const auto pieces = segments(raster, QRect(0, top, image.width(), height));
        std::vector<Glyph> glyphs;
        std::vector<int> bars;
        for (const auto &box : pieces)
        {
            if (box.height() >= height * 0.75 && box.width() <= height * 0.3 &&
                raster.ink(box.center().x(), top - int(height * 0.4)) &&
                raster.ink(box.center().x(), top + int(height * 1.25)))
            {
                bars.push_back(box.center().x());
                continue;
            }
            if (box.height() < height * 0.7 || box.width() < height * 0.23 || box.width() > height * 1.02)
                continue;
            // Parentheses extend beyond the digit band and must not become 1s.
            const bool bracket = std::any_of(raster.components.begin(), raster.components.end(),
                                             [&](const Component &component)
                                             {
                                                 return component.box.left() >= box.left() - 2 &&
                                                        component.box.right() <= box.right() + 2 &&
                                                        component.box.intersects(box) &&
                                                        component.box.top() < top - height * 0.10 &&
                                                        component.box.bottom() >= top + height * 1.0;
                                             });
            if (bracket)
                continue;
            const bool connectedDigit =
                std::any_of(raster.components.begin(), raster.components.end(),
                            [&](const Component &component)
                            {
                                return component.box.intersects(box) &&
                                       component.box.intersected(box).height() >= height * 0.65;
                            });
            if (!connectedDigit)
                continue;
            auto glyph = classify(raster, box, templates);
            if (glyph.score <= 0.37)
                glyphs.push_back(glyph);
        }
        const std::size_t firstNote = result.score.notes.size();
        int previousBar = 0;
        for (std::size_t index = 0; index < glyphs.size(); ++index)
        {
            const auto &glyph = glyphs[index];
            const auto &box = glyph.box;
            const int barCount =
                int(std::count_if(bars.begin(), bars.end(), [&](int x) { return x < box.center().x(); }));
            if (barCount > previousBar)
            {
                ++measure;
                previousBar = barCount;
            }
            Note note;
            note.id = int(result.score.notes.size());
            note.degree = glyph.digit;
            note.measure = measure;
            note.line = int(rowIndex);
            note.source = {box.x() * sourceScale, box.y() * sourceScale, box.width() * sourceScale,
                           box.height() * sourceScale};
            note.confidence = glyph.score < 0.11 ? 0.97 : glyph.score < 0.19 ? 0.88 : 0.68;
            note.durationTicks = TicksPerQuarter / (1 << underlineCount(raster, box));
            const double nextLeft = index + 1 < glyphs.size() ? glyphs[index + 1].box.left() : image.width();
            bool dotted = false;
            for (const auto &component : raster.components)
            {
                const auto &mark = component.box;
                if (mark.right() < box.left() - height || mark.left() > nextLeft ||
                    mark.top() > box.bottom() + height || mark.bottom() < box.top() - height * 1.6)
                    continue;
                if (dot(component, height))
                {
                    if (std::abs(mark.center().x() - box.center().x()) < height * 0.32)
                    {
                        const bool labelDot = std::any_of(
                            raster.components.begin(), raster.components.end(),
                            [&](const Component &other)
                            {
                                return other.box.right() < mark.left() &&
                                       mark.left() - other.box.right() < height * 0.6 &&
                                       other.box.height() > height * 0.45 &&
                                       std::abs(other.box.center().y() - mark.center().y()) < height * 0.3;
                            });
                        const bool stackedDot = std::any_of(
                            raster.components.begin(), raster.components.end(),
                            [&](const Component &other)
                            {
                                return dot(other, height) &&
                                       std::abs(other.box.center().x() - mark.center().x()) < height * 0.2 &&
                                       other.box.top() > mark.bottom() && other.box.bottom() < box.top();
                            });
                        if (!labelDot && mark.bottom() < box.top() && mark.top() >= box.top() - height * 1.5 &&
                            (box.top() - mark.bottom() < height * 0.7 || stackedDot))
                            ++note.octave;
                        if (mark.top() > box.bottom() && mark.bottom() <= box.bottom() + height * 1.0 &&
                            mark.bottom() < lyricTop - 1)
                            --note.octave;
                    }
                    if (mark.left() > box.right() && mark.right() < nextLeft &&
                        mark.left() - box.right() < height * 0.85 &&
                        mark.center().y() > box.top() + height * 0.2 && mark.center().y() < box.bottom())
                        dotted = true;
                }
            }
            if (dotted)
                note.durationTicks = note.durationTicks * 3 / 2;
            // Extension dashes add one quarter-note each; rhythm dots do not.
            for (const auto &component : raster.components)
            {
                const auto &mark = component.box;
                if (mark.left() <= box.right() || mark.right() >= nextLeft || mark.width() < height * 0.47 ||
                    mark.height() > height * 0.3)
                    continue;
                if (mark.center().y() > box.top() + height * 0.2 &&
                    mark.center().y() < box.bottom() - height * 0.1 &&
                    std::none_of(bars.begin(), bars.end(),
                                 [&](int x) { return x > box.right() && x < mark.left(); }))
                    note.durationTicks += TicksPerQuarter;
            }
            note.octave = std::clamp(note.octave, -2, 2);
            if (note.confidence < 0.8)
                ++uncertain;
            result.score.notes.push_back(note);
        }
        // Full-height characters establish verse rows. Thin OCR "一" boxes may
        // be duration underlines, so they can join a row but never create one.
        const double nextTop =
            rowIndex + 1 < rows.size() ? rows[rowIndex + 1].y - rows[rowIndex + 1].height : image.height();
        std::vector<const OcrWord *> lyrics;
        for (const auto &word : ocr.words)
            if (word.box.top() > top + height * 1.15 && word.box.bottom() < nextTop &&
                std::any_of(word.text.begin(), word.text.end(), cjk))
                lyrics.push_back(&word);
        // Geometric clustering is separate from reading order: a fuzzy y/x
        // comparator is not transitive and can put A/B lines in arbitrary order.
        std::sort(lyrics.begin(), lyrics.end(),
                  [](const auto *a, const auto *b)
                  {
                      if (a->box.center().y() != b->box.center().y())
                          return a->box.center().y() < b->box.center().y();
                      return a->box.x() < b->box.x();
                  });
        struct LyricRow
        {
            double centerY = 0;
            double characterHeight = 0;
            std::vector<const OcrWord *> words;
        };
        std::vector<LyricRow> lyricRows;
        for (const auto *word : lyrics)
        {
            if (word->box.height() < height * 0.6)
                continue;
            auto lyricRow = std::find_if(lyricRows.begin(), lyricRows.end(),
                                         [&](const LyricRow &candidate)
                                         {
                                             return std::abs(candidate.centerY - word->box.center().y()) <
                                                    std::min(candidate.characterHeight, word->box.height()) * 0.45;
                                         });
            if (lyricRow == lyricRows.end())
                lyricRows.push_back({word->box.center().y(), word->box.height(), {word}});
            else
            {
                const double count = double(lyricRow->words.size());
                lyricRow->centerY = (lyricRow->centerY * count + word->box.center().y()) / (count + 1);
                lyricRow->characterHeight = (lyricRow->characterHeight * count + word->box.height()) / (count + 1);
                lyricRow->words.push_back(word);
            }
        }
        std::sort(lyricRows.begin(), lyricRows.end(),
                  [](const LyricRow &a, const LyricRow &b) { return a.centerY < b.centerY; });
        for (const auto *word : lyrics)
        {
            if (word->box.height() >= height * 0.6 ||
                !std::all_of(word->text.begin(), word->text.end(), [](QChar c) { return c == QChar(0x4e00); }))
                continue;
            auto lyricRow =
                std::find_if(lyricRows.begin(), lyricRows.end(),
                             [&](const LyricRow &candidate)
                             {
                                 return std::abs(candidate.centerY - word->box.center().y()) <
                                            candidate.characterHeight * 0.35 &&
                                        word->box.top() > candidate.centerY - candidate.characterHeight * 0.55 &&
                                        word->box.bottom() < candidate.centerY + candidate.characterHeight * 0.55;
                             });
            if (lyricRow != lyricRows.end())
                lyricRow->words.push_back(word);
        }
        std::vector<std::vector<QString>> verseText(lyricRows.size(), std::vector<QString>(glyphs.size()));
        for (std::size_t verse = 0; verse < lyricRows.size(); ++verse)
        {
            auto &lyricRow = lyricRows[verse];
            std::sort(lyricRow.words.begin(), lyricRow.words.end(),
                      [](const auto *a, const auto *b)
                      {
                          if (a->box.x() != b->box.x())
                              return a->box.x() < b->box.x();
                          return a->box.y() < b->box.y();
                      });
            for (const auto *word : lyricRow.words)
                for (int character = 0; character < word->text.size(); ++character)
                {
                    if (!cjk(word->text[character]))
                        continue;
                    const double x = word->box.x() + word->box.width() * (character + 0.5) / word->text.size();
                    double distance = height * 1.5;
                    std::size_t nearest = glyphs.size();
                    for (std::size_t j = 0; j < glyphs.size(); ++j)
                        if (glyphs[j].digit != 0 && std::abs(glyphs[j].box.center().x() - x) < distance)
                        {
                            distance = std::abs(glyphs[j].box.center().x() - x);
                            nearest = j;
                        }
                    if (nearest < glyphs.size())
                        verseText[verse][nearest] += word->text[character];
                }
        }
        for (std::size_t i = 0; i < glyphs.size(); ++i)
        {
            auto &note = result.score.notes[firstNote + i];
            QStringList legacyVerses;
            for (const auto &verse : verseText)
            {
                note.verseLyrics.push_back(verse[i].toStdString());
                legacyVerses.push_back(verse[i]);
            }
            // Keep absent slots: ["", "B"] must never become shared ["B"].
            // A staff with one visible lyric row deliberately has one shared slot.
            note.lyric = legacyVerses.join(QLatin1Char('\n')).toStdString();
        }
        result.debugText += QStringLiteral("Lyric rows line=%1 count=%2\n").arg(rowIndex).arg(lyricRows.size());
        // Recognize simple ties only when an arc connects adjacent equal pitches.
        for (std::size_t i = 0; i + 1 < glyphs.size(); ++i)
        {
            auto &note = result.score.notes[firstNote + i];
            const auto &next = result.score.notes[firstNote + i + 1];
            if (note.degree == 0 || note.degree != next.degree || note.octave != next.octave)
                continue;
            for (const auto &component : raster.components)
                if (component.box.width() > height * 1.0 && component.box.top() < top - 1 &&
                    component.box.bottom() < top + 2 && component.box.top() > top - height &&
                    component.box.left() <= glyphs[i].box.center().x() + 2 &&
                    component.box.right() >= glyphs[i + 1].box.center().x() - 2 &&
                    component.box.left() >= glyphs[i].box.left() - 2 &&
                    component.box.right() <= glyphs[i + 1].box.right() + 2)
                    note.tieToNext = true;
        }
        auto noteAfter = [&](double x)
        {
            for (std::size_t i = 0; i < glyphs.size(); ++i)
                if (glyphs[i].box.center().x() > x)
                    return firstNote + i;
            return firstNote + glyphs.size();
        };
        for (const auto &component : raster.components)
        {
            const auto &bracket = component.box;
            if (bracket.width() < image.width() * 0.15 || bracket.height() > height * 1.7 ||
                bracket.top() < top - height * 3 || bracket.bottom() > top - height * 0.5)
                continue;
            hasEnding = true;
            int number = 0;
            double best = 0.4;
            for (const auto &label : raster.components)
            {
                const auto &box = label.box;
                if (box.left() < bracket.left() || box.right() > bracket.left() + height * 3 ||
                    box.top() <= bracket.top() || box.bottom() >= top - height * 0.45 ||
                    box.height() < height * 0.4 || box.height() > height * 0.95 || box.width() > height)
                    continue;
                const auto glyph = classify(raster, box, templates);
                result.debugText += QStringLiteral("Ending label row=%1 x=%2 y=%3 w=%4 h=%5 digit=%6 score=%7\n")
                                        .arg(rowIndex)
                                        .arg(box.x())
                                        .arg(box.y())
                                        .arg(box.width())
                                        .arg(box.height())
                                        .arg(glyph.digit)
                                        .arg(glyph.score);
                if ((glyph.digit == 1 || glyph.digit == 2) && glyph.score < best)
                {
                    best = glyph.score;
                    number = glyph.digit;
                }
            }
            result.debugText += QStringLiteral("Ending candidate row=%1 x=%2 y=%3 label=%4 score=%5\n")
                                    .arg(rowIndex)
                                    .arg(bracket.x())
                                    .arg(bracket.y())
                                    .arg(number)
                                    .arg(best);
            if (number)
                endingMarkers.push_back({noteAfter(bracket.left()), number, int(rowIndex)});
        }
        for (const auto &a : raster.components)
            if (dot(a, height) && a.box.center().y() > top - height * 0.25 &&
                a.box.center().y() < top + height * 0.7)
            {
                for (const auto &b : raster.components)
                    if (dot(b, height) && std::abs(a.box.center().x() - b.box.center().x()) < height * 0.2 &&
                        b.box.center().y() - a.box.center().y() > height * 0.35 &&
                        b.box.center().y() - a.box.center().y() < height * 0.95)
                    {
                        const auto nearest = std::min_element(
                            bars.begin(), bars.end(), [&](int x, int y)
                            { return std::abs(x - a.box.center().x()) < std::abs(y - a.box.center().x()); });
                        if (nearest != bars.end() && std::abs(*nearest - a.box.center().x()) < height)
                        {
                            hasRepeatSymbols = true;
                            const bool opening = a.box.center().x() > *nearest;
                            const std::size_t note = noteAfter(std::max(*nearest, a.box.center().x()));
                            if (std::none_of(repeatMarkers.begin(), repeatMarkers.end(), [&](const auto &marker)
                                             { return marker.note == note && marker.opening == opening; }))
                            {
                                repeatMarkers.push_back({note, opening, int(rowIndex)});
                                result.debugText +=
                                    QStringLiteral("Repeat candidate row=%1 x=%2 note=%3 opening=%4\n")
                                        .arg(rowIndex)
                                        .arg(*nearest)
                                        .arg(note)
                                        .arg(opening);
                            }
                        }
                    }
            }
        ++measure;
    }
    const auto systems = numberedSystems(raster, rows, result.score, sourceScale);
    const bool braced = !systems.empty();
    if (braced)
    {
        std::erase_if(repeatMarkers,
                      [&](const RepeatMarker &marker)
                      {
                          return std::any_of(systems.begin(), systems.end(), [&](const NumberedSystem &system)
                                             { return marker.line == system.lowerLine; });
                      });
        std::erase_if(endingMarkers,
                      [&](const EndingMarker &marker)
                      {
                          return std::any_of(systems.begin(), systems.end(), [&](const NumberedSystem &system)
                                             { return marker.line == system.lowerLine; });
                      });
    }
    // Commit only structurally consistent repeat spans, with matched volta labels.
    int pendingStart = -1;
    bool ambiguousRepeat = false;
    for (const auto &marker : repeatMarkers)
    {
        if (marker.opening)
        {
            if (pendingStart >= 0)
                ambiguousRepeat = true;
            pendingStart = int(marker.note);
            continue;
        }
        if (pendingStart < 0 || marker.note <= std::size_t(pendingStart))
        {
            ambiguousRepeat = true;
            continue;
        }
        RepeatSection repeat{std::size_t(pendingStart), marker.note, 2, -1};
        const auto firstEnding = std::find_if(
            endingMarkers.begin(), endingMarkers.end(), [&](const auto &ending)
            { return ending.number == 1 && ending.note >= repeat.firstNote && ending.note < repeat.endNote; });
        const auto secondEnding = std::find_if(endingMarkers.begin(), endingMarkers.end(), [&](const auto &ending)
                                               { return ending.number == 2 && ending.note == repeat.endNote; });
        if (firstEnding != endingMarkers.end() && secondEnding != endingMarkers.end())
            repeat.firstEndingNote = int(firstEnding->note);
        else if (hasEnding)
        {
            ambiguousRepeat = true;
            pendingStart = -1;
            continue;
        }
        result.score.repeats.push_back(repeat);
        result.debugText += QStringLiteral("Repeat accepted first=%1 end=%2 firstEnding=%3\n")
                                .arg(repeat.firstNote)
                                .arg(repeat.endNote)
                                .arg(repeat.firstEndingNote);
        pendingStart = -1;
    }
    ambiguousRepeat |= pendingStart >= 0;
    // Spatially detect inline key changes. Keep the new tonic from this note onward.
    for (const auto &letter : ocr.words)
        if (letter.text.size() == 1 && letterKey(letter.text[0]) >= 0 && letter.box.top() > rows.front().y)
        {
            for (const auto &one : ocr.words)
                if (one.text == QStringLiteral("1") && one.box.right() < letter.box.left() &&
                    letter.box.left() - one.box.right() < letter.box.height() * 3.0 &&
                    std::abs(one.box.center().y() - letter.box.center().y()) < letter.box.height() * 0.45)
                {
                    auto found = std::find_if(
                        result.score.notes.begin(), result.score.notes.end(),
                        [&](const Note &note)
                        {
                            return note.source.y / sourceScale > letter.box.bottom() &&
                                   note.source.y / sourceScale < letter.box.bottom() + letter.box.height() * 3 &&
                                   note.source.x / sourceScale >= one.box.x() - letter.box.height();
                        });
                    if (found != result.score.notes.end())
                        found->keyOverride = letterKey(letter.text[0]);
                }
        }
    if (uncertain)
        result.warnings << trText("messages.recognition.uncertain_notes").arg(uncertain);
    if ((hasRepeatSymbols || hasEnding) && (ambiguousRepeat || result.score.repeats.empty()))
        result.warnings << trText("messages.recognition.unpaired_repeats");
    if (!result.score.repeats.empty())
        result.warnings << trText("messages.recognition.recognized_repeats").arg(result.score.repeats.size());
    int irregular = 0;
    for (std::size_t i = 0; i < result.score.notes.size();)
    {
        const int current = result.score.notes[i].measure;
        int ticks = 0;
        while (i < result.score.notes.size() && result.score.notes[i].measure == current)
            ticks += result.score.notes[i++].durationTicks;
        if (ticks != ticksPerBar(result.score))
            ++irregular;
    }
    if (irregular)
        result.warnings
            << trText("messages.recognition.irregular_groups")
                   .arg(irregular);
    if (braced)
    {
        recoverNumberedChords(raster, templates, systems, sourceScale, result.score);
        result.staffPerformance = buildNumberedPerformance(result.score, systems);
        result.debugText += QStringLiteral("Braced numbered systems=%1 performanceNotes=%2\n")
                                .arg(systems.size())
                                .arg(result.staffPerformance->notes.size());
    }
    result.debugText += QStringLiteral("\n--- Local recognition ---\nrows=%1 notes=%2 uncertain=%3 "
                                       "irregularMeasures=%4 key=%5 bpm=%6 meter=%7/%8\n")
                            .arg(rows.size())
                            .arg(result.score.notes.size())
                            .arg(uncertain)
                            .arg(irregular)
                            .arg(result.score.tonic)
                            .arg(result.score.bpm)
                            .arg(result.score.beatsPerBar)
                            .arg(result.score.beatUnit);
    for (const auto &note : result.score.notes)
        result.debugText +=
            QStringLiteral("%1 line=%2 degree=%3 octave=%4 ticks=%5 x=%6 y=%7 confidence=%8 lyric=%9\n")
                .arg(note.id)
                .arg(note.line)
                .arg(note.degree)
                .arg(note.octave)
                .arg(note.durationTicks)
                .arg(note.source.x)
                .arg(note.source.y)
                .arg(note.confidence)
                .arg(QString::fromStdString(note.lyric));
    return result;
}
} // namespace singlilt

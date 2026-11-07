// Shared-measure, stacked-voice numbered notation with stable hit identities.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#include "NotationRenderer.h"
#include "i18n/LanguageManager.h"
#include <QPainter>
#include <QFontMetricsF>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
namespace singlilt
{
QImage renderNumberedComposition(Score &metadata, NumberedComposition &composition, int activeVoice,
                                 NumberedScoreLayout &layout)
{
    validateNumberedComposition(metadata, composition);
    numberedVoice(composition, activeVoice);
    std::vector<Score> scores;
    std::vector<NumberedScoreLayout> layouts;
    const auto measures = numberedMeasures(metadata, composition);
    const bool structured = !composition.structure.measures.empty();
    const auto *measureLayout = structured ? &measures : nullptr;
    const int bars = int(measures.size());
    std::vector<QString> labels(measures.size());
    for (std::size_t i = 0; i < measures.size(); ++i)
    {
        const auto &measure = measures[i];
        const auto *previous = i ? &measures[i - 1] : nullptr;
        QStringList changes;
        if (!previous || previous->beats != measure.beats || previous->unit != measure.unit)
            changes << QString("%1/%2").arg(measure.beats).arg(measure.unit);
        if (!previous || previous->bpm != measure.bpm)
            changes << QString("BPM %1").arg(measure.bpm);
        if (!previous || previous->tonic != measure.tonic)
            changes << trText(qPrintable(QString("ui.key.%1").arg(measure.tonic)));
        for (const auto &repeat : composition.structure.repeats)
        {
            if (measure.id == repeat.first)
                changes << "|:";
            if (repeat.firstEnding && measure.id == repeat.firstEnding)
                changes << "1.";
            if (measure.id == repeat.last)
                changes << QString(":| x%1").arg(repeat.count);
            if (previous && repeat.firstEnding && previous->id == repeat.last)
                changes << "2.";
        }
        labels[i] = changes.join("  ");
    }
    std::vector<NumberedMeasureGrid> shared(std::size_t(bars), NumberedMeasureGrid{});
    std::vector<std::map<std::int64_t, NumberedSpacingPoint>> points(
        static_cast<std::size_t>(bars), std::map<std::int64_t, NumberedSpacingPoint>{});
    for (const auto &voice : composition.voices)
    {
        auto score = metadata;
        score.notes = voice.notes;
        score.editorMinimumBars = bars;
        auto measured = layoutNumberedScore(score, measureLayout);
        for (int i = 0; i < bars; ++i)
            for (const auto &point : measured.grid[std::size_t(i)].points)
            {
                auto &merged = points[std::size_t(i)][point.tick];
                merged.tick = point.tick;
                merged.left = std::max(merged.left, point.left);
                merged.right = std::max(merged.right, point.right);
            }
        scores.push_back(std::move(score));
        layouts.push_back(std::move(measured));
    }
    for (int i = 0; i < bars; ++i)
    {
        auto &grid = shared[std::size_t(i)];
        double required = 40;
        for (const auto &[tick, point] : points[std::size_t(i)])
            required += point.left + point.right;
        grid.width = std::max(285.0, required);
        if (grid.width > 1308)
            throw std::runtime_error("messages.audio_import.notation_too_large");
        double used = 20;
        for (const auto &[tick, point] : points[std::size_t(i)])
        {
            auto positioned = point;
            positioned.position =
                used + point.left + (grid.width - required) * double(tick) / (measures[i].end - measures[i].start);
            grid.points.push_back(positioned);
            used += point.left + point.right;
        }
    }
    int projectedRows = 1;
    int barsInRow = 0;
    double rowWidth = 0;
    for (const auto &grid : shared)
    {
        const double width = grid.width;
        if (barsInRow == 4 || (barsInRow > 0 && rowWidth + width > 1308))
        {
            ++projectedRows;
            barsInRow = 0;
            rowWidth = 0;
        }
        rowWidth += width;
        ++barsInRow;
    }
    double projectedHeight = layouts.front().systems.front().top() + 100;
    for (const auto &voiceLayout : layouts)
        projectedHeight += projectedRows * (voiceLayout.systems.front().height() + 12);
    if (projectedHeight > 16000)
        throw std::runtime_error("messages.audio_import.notation_too_large");
    std::vector<QImage> images;
    for (std::size_t i = 0; i < scores.size(); ++i)
        images.push_back(renderNumberedScore(scores[i], &layouts[i], &shared, measureLayout,
                                             structured && i == 0 ? &labels : nullptr));
    const int header = int(layouts.front().systems.front().top());
    double totalHeight = header + 100;
    for (const auto &voiceLayout : layouts)
        for (const auto &system : voiceLayout.systems)
            totalHeight += system.height() + 12;
    if (totalHeight > 16000)
        throw std::runtime_error("messages.audio_import.notation_too_large");
    recordNumberedRasterization();
    QImage result(1440, std::max(1900, int(std::ceil(totalHeight))), QImage::Format_RGB32);
    if (result.isNull())
        throw std::runtime_error("messages.audio_import.notation_layout_failed");
    result.fill(Qt::white);
    QPainter painter(&result);
    painter.drawImage(QPointF(0, 0), images.front(), QRectF(0, 0, 1440, header));
    QFont font("Microsoft YaHei UI");
    font.setPixelSize(16);
    painter.setFont(font);
    NumberedScoreLayout combined;
    double y = header;
    for (std::size_t row = 0; row < layouts.front().systems.size(); ++row)
    {
        for (std::size_t voice = 0; voice < scores.size(); ++voice)
        {
            const auto source = layouts[voice].systems[row];
            const double offset = y - source.top();
            painter.drawImage(QPointF(0, y), images[voice], source);
            const int id = composition.voices[voice].id;
            painter.setPen(QColor("#526477"));
            combined.voiceLabels.push_back({QRectF(3, y + 4, 60, 26), id});
            painter.drawText(QRectF(3, y + 4, 60, 26), Qt::AlignLeft,
                             QFontMetricsF(font).elidedText(QString::fromStdString(composition.voices[voice].name),
                                                            Qt::ElideRight, 60));
            for (std::size_t note = 0; note < scores[voice].notes.size(); ++note)
            {
                auto &entry = scores[voice].notes[note];
                if (entry.line != int(row))
                    continue;
                entry.source.y += offset;
                combined.notes.push_back(
                    {QRectF(entry.source.x, entry.source.y, entry.source.width, entry.source.height), id,
                     int(note)});
            }
            for (auto slot : layouts[voice].slots)
                if (source.contains(slot.bounds.center()))
                {
                    slot.bounds.translate(0, offset);
                    slot.voiceId = id;
                    combined.slots.push_back(slot);
                }
            if (id == activeVoice)
                for (auto measure : layouts[voice].measures)
                    if (source.contains(measure.center()))
                    {
                        measure.translate(0, offset);
                        combined.measures.push_back(measure);
                    }
            y += source.height() + 12;
        }
    }
    combined.addBars = QRectF(66, y + 12, 1308, 48);
    painter.end();
    for (std::size_t i = 0; i < scores.size(); ++i)
        composition.voices[i].notes = std::move(scores[i].notes);
    metadata.notes = numberedVoice(composition, activeVoice).notes;
    layout = std::move(combined);
    return result;
}
} // namespace singlilt

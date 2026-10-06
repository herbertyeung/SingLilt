// Recorded pitch and target-note feedback display.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "PitchCurve.h"
#include "i18n/LanguageManager.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace singlilt
{
PitchCurve::PitchCurve(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(125);
    setObjectName("singingPitchCurve");
}
void PitchCurve::setFrames(std::vector<ExpectedTone> targets, std::vector<PitchObservation> frames, bool reveal)
{
    targets_ = std::move(targets);
    frames_ = std::move(frames);
    reveal_ = reveal;
    update();
}
void PitchCurve::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    if (!reveal_)
    {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, trText("ui.classroom.hidden"));
        return;
    }
    double end = 1.0, low = 60.0, high = 72.0;
    for (const auto &tone : targets_)
    {
        end = std::max(end, tone.endSeconds);
        low = std::min(low, double(tone.midiPitch) - 2.0);
        high = std::max(high, double(tone.midiPitch) + 2.0);
    }
    for (const auto &frame : frames_)
    {
        end = std::max(end, frame.seconds + frame.durationSeconds);
        if (frame.confidence >= 0.85 && frame.midiPitch >= 24.0 && frame.midiPitch <= 108.0)
        {
            low = std::min(low, frame.midiPitch - 1.0);
            high = std::max(high, frame.midiPitch + 1.0);
        }
    }
    const QRectF plot(48, 24, std::max(1, width() - 65), std::max(1, height() - 47));
    const auto x = [&](double seconds) { return plot.left() + plot.width() * seconds / end; };
    const auto y = [&](double pitch) { return plot.bottom() - plot.height() * (pitch - low) / (high - low); };
    painter.setPen(palette().color(QPalette::Mid));
    for (int pitch = static_cast<int>(std::ceil(low)); pitch <= static_cast<int>(high); pitch += 2)
    {
        painter.drawLine(QPointF(plot.left(), y(pitch)), QPointF(plot.right(), y(pitch)));
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(QRectF(0, y(pitch) - 8, 43, 16), Qt::AlignRight, QString::number(pitch));
        painter.setPen(palette().color(QPalette::Mid));
    }
    painter.setPen(QPen(palette().color(QPalette::Highlight), 3));
    for (const auto &tone : targets_)
        painter.drawLine(QPointF(x(tone.startSeconds), y(tone.midiPitch)),
                         QPointF(x(tone.endSeconds), y(tone.midiPitch)));
    QPainterPath path;
    bool connected = false;
    double previous = -1.0;
    for (const auto &frame : frames_)
    {
        if (frame.midiPitch < 0.0 || frame.confidence < 0.85 || frame.clipped || frame.seconds < 0.0)
        {
            connected = false;
            continue;
        }
        const QPointF point(x(frame.seconds), y(frame.midiPitch));
        if (connected && frame.seconds - previous < 0.06)
            path.lineTo(point);
        else
            path.moveTo(point);
        connected = true;
        previous = frame.seconds;
    }
    painter.setPen(QPen(palette().color(QPalette::LinkVisited), 2));
    painter.drawPath(path);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(48, 16, trText("ui.classroom.curve_legend"));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 4, plot.width(), 18), Qt::AlignRight,
                     trText("ui.classroom.curve_end").arg(end, 0, 'f', 1));
}
} // namespace singlilt

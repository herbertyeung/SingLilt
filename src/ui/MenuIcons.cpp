// Painter-based icons for application menu actions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MenuIcons.h"
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <algorithm>

namespace singlilt
{
namespace
{
void document(QPainter &painter)
{
    QPainterPath outline;
    outline.moveTo(6, 3);
    outline.lineTo(14, 3);
    outline.lineTo(19, 8);
    outline.lineTo(19, 21);
    outline.lineTo(6, 21);
    outline.closeSubpath();
    painter.drawPath(outline);
    painter.drawPolyline(QPolygonF{{14, 3}, {14, 8}, {19, 8}});
}
void folder(QPainter &painter)
{
    QPainterPath outline;
    outline.moveTo(3, 19);
    outline.lineTo(3, 6);
    outline.lineTo(9, 6);
    outline.lineTo(11, 8);
    outline.lineTo(21, 8);
    outline.lineTo(21, 19);
    outline.closeSubpath();
    painter.drawPath(outline);
}
void note(QPainter &painter, qreal x, qreal y)
{
    painter.drawEllipse(QRectF(x, y + 7, 5, 3));
    painter.drawLine(QPointF(x + 5, y + 8), QPointF(x + 5, y));
    painter.drawLine(QPointF(x + 5, y), QPointF(x + 9, y + 2));
}
void arrow(QPainter &painter, bool right)
{
    QPainterPath curve;
    curve.moveTo(19, 18);
    curve.cubicTo(20, 8, 12, 6, 5, 10);
    if (right)
    {
        painter.translate(24, 0);
        painter.scale(-1, 1);
    }
    painter.drawPath(curve);
    painter.drawPolyline(QPolygonF{{5, 5}, {5, 10}, {10, 10}});
}
void drawGlyph(QPainter &painter, MenuIcon icon)
{
    switch (icon)
    {
    case MenuIcon::NewProject:
        document(painter);
        painter.drawLine(9, 14, 16, 14);
        painter.drawLine(12, 11, 12, 17);
        break;
    case MenuIcon::OpenProject:
        folder(painter);
        painter.drawLine(8, 14, 16, 14);
        painter.drawPolyline(QPolygonF{{13, 11}, {16, 14}, {13, 17}});
        break;
    case MenuIcon::Folder:
        folder(painter);
        break;
    case MenuIcon::ImportImage:
        painter.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        painter.drawEllipse(QRectF(6, 7, 3, 3));
        painter.drawPolyline(QPolygonF{{4, 17}, {10, 11}, {14, 15}, {17, 12}, {20, 16}});
        break;
    case MenuIcon::ImportAudio:
        document(painter);
        note(painter, 9, 10);
        break;
    case MenuIcon::Paste:
        painter.drawRoundedRect(QRectF(5, 5, 14, 16), 2, 2);
        painter.drawRoundedRect(QRectF(9, 3, 6, 4), 1, 1);
        painter.drawLine(9, 12, 15, 12);
        painter.drawLine(9, 16, 14, 16);
        break;
    case MenuIcon::Save:
    case MenuIcon::SaveAs:
        painter.drawRoundedRect(QRectF(4, 3, 16, 18), 2, 2);
        painter.drawRect(QRectF(8, 3, 8, 6));
        painter.drawRect(QRectF(8, 14, 8, 7));
        if (icon == MenuIcon::SaveAs)
        {
            painter.setBrush(painter.background());
            painter.drawEllipse(QRectF(13, 12, 10, 10));
            painter.drawLine(16, 17, 20, 17);
            painter.drawLine(18, 15, 18, 19);
        }
        break;
    case MenuIcon::ExportAudio:
        painter.drawPolyline(QPolygonF{{4, 14}, {4, 21}, {20, 21}, {20, 14}});
        painter.drawLine(12, 16, 12, 3);
        painter.drawPolyline(QPolygonF{{8, 7}, {12, 3}, {16, 7}});
        break;
    case MenuIcon::Exit:
        painter.drawPolyline(QPolygonF{{10, 4}, {4, 4}, {4, 20}, {10, 20}});
        painter.drawLine(10, 12, 21, 12);
        painter.drawPolyline(QPolygonF{{17, 8}, {21, 12}, {17, 16}});
        break;
    case MenuIcon::Recent:
        painter.drawEllipse(QRectF(4, 4, 16, 16));
        painter.drawPolyline(QPolygonF{{12, 7}, {12, 12}, {16, 14}});
        break;
    case MenuIcon::Undo:
    case MenuIcon::Redo:
        arrow(painter, icon == MenuIcon::Redo);
        break;
    case MenuIcon::Lyrics:
        painter.drawLine(3, 6, 15, 6);
        painter.drawLine(3, 10, 12, 10);
        painter.drawLine(3, 14, 9, 14);
        note(painter, 12, 10);
        break;
    case MenuIcon::Repeat:
    case MenuIcon::Replay:
    case MenuIcon::Refresh:
        painter.drawArc(QRectF(4, 4, 16, 16), 25 * 16, 275 * 16);
        painter.drawPolyline(QPolygonF{{17, 3}, {20, 7}, {15, 8}});
        if (icon == MenuIcon::Repeat)
        {
            painter.drawEllipse(QRectF(9, 9, 1.5, 1.5));
            painter.drawEllipse(QRectF(9, 14, 1.5, 1.5));
            painter.drawLine(13, 9, 13, 17);
        }
        else if (icon == MenuIcon::Replay)
            painter.drawPolygon(QPolygonF{{10, 8}, {16, 12}, {10, 16}});
        break;
    case MenuIcon::FitWidth:
        painter.drawLine(3, 5, 3, 19);
        painter.drawLine(21, 5, 21, 19);
        painter.drawLine(5, 12, 19, 12);
        painter.drawPolyline(QPolygonF{{8, 9}, {5, 12}, {8, 15}});
        painter.drawPolyline(QPolygonF{{16, 9}, {19, 12}, {16, 15}});
        break;
    case MenuIcon::Practice:
    case MenuIcon::Play:
        painter.drawPolygon(QPolygonF{{7, 4}, {20, 12}, {7, 20}});
        if (icon == MenuIcon::Practice)
            painter.drawLine(3, 6, 3, 18);
        break;
    case MenuIcon::Correction:
        painter.drawPolygon(QPolygonF{{5, 16}, {16, 5}, {20, 9}, {9, 20}, {4, 21}});
        painter.drawLine(14, 7, 18, 11);
        break;
    case MenuIcon::Classroom:
        painter.drawPolygon(QPolygonF{{2, 9}, {12, 4}, {22, 9}, {12, 14}});
        painter.drawPolyline(QPolygonF{{6, 12}, {6, 18}, {12, 21}, {18, 18}, {18, 12}});
        painter.drawLine(22, 9, 22, 17);
        break;
    case MenuIcon::EarTraining:
        painter.drawArc(QRectF(4, 3, 15, 18), -70 * 16, 290 * 16);
        painter.drawArc(QRectF(8, 7, 7, 8), -50 * 16, 240 * 16);
        painter.drawPolyline(QPolygonF{{13, 12}, {11, 15}, {11, 19}});
        break;
    case MenuIcon::Accompaniment:
        painter.drawRoundedRect(QRectF(3, 5, 18, 15), 1, 1);
        for (int x : {9, 15})
            painter.drawLine(x, 5, x, 20);
        for (int x : {7, 13})
            painter.drawRect(QRectF(x, 5, 3, 8));
        break;
    case MenuIcon::Samples:
        painter.drawRoundedRect(QRectF(3, 3, 7, 7), 1, 1);
        painter.drawRoundedRect(QRectF(14, 3, 7, 7), 1, 1);
        painter.drawRoundedRect(QRectF(3, 14, 7, 7), 1, 1);
        painter.drawRoundedRect(QRectF(14, 14, 7, 7), 1, 1);
        break;
    case MenuIcon::Scale:
        painter.drawPolyline(QPolygonF{{3, 20}, {3, 15}, {9, 15}, {9, 9}, {15, 9}, {15, 3}, {21, 3}});
        break;
    case MenuIcon::Score:
        document(painter);
        painter.drawLine(9, 11, 16, 11);
        painter.drawLine(9, 15, 16, 15);
        painter.drawLine(9, 18, 13, 18);
        break;
    case MenuIcon::Recognize:
        painter.drawPolyline(QPolygonF{{3, 9}, {3, 3}, {9, 3}});
        painter.drawPolyline(QPolygonF{{15, 3}, {21, 3}, {21, 9}});
        painter.drawPolyline(QPolygonF{{3, 15}, {3, 21}, {9, 21}});
        painter.drawPolyline(QPolygonF{{21, 15}, {21, 21}, {15, 21}});
        painter.drawPolygon(
            QPolygonF{{12, 6}, {14, 10}, {18, 12}, {14, 14}, {12, 18}, {10, 14}, {6, 12}, {10, 10}});
        break;
    case MenuIcon::Settings:
        for (int x : {5, 12, 19})
            painter.drawLine(x, 3, x, 21);
        painter.drawRoundedRect(QRectF(2, 6, 6, 4), 1, 1);
        painter.drawRoundedRect(QRectF(9, 14, 6, 4), 1, 1);
        painter.drawRoundedRect(QRectF(16, 8, 6, 4), 1, 1);
        break;
    case MenuIcon::About:
        painter.drawEllipse(QRectF(3, 3, 18, 18));
        painter.drawLine(12, 11, 12, 17);
        painter.drawPoint(12, 7);
        break;
    case MenuIcon::Microphone:
        painter.drawRoundedRect(QRectF(9, 3, 6, 12), 3, 3);
        painter.drawArc(QRectF(6, 8, 12, 10), 180 * 16, 180 * 16);
        painter.drawLine(12, 18, 12, 21);
        painter.drawLine(8, 21, 16, 21);
        break;
    case MenuIcon::Stop:
        painter.drawRoundedRect(QRectF(5, 5, 14, 14), 1, 1);
        break;
    case MenuIcon::Calibration:
        painter.drawEllipse(QRectF(5, 5, 14, 14));
        painter.drawEllipse(QRectF(10, 10, 4, 4));
        painter.drawLine(12, 2, 12, 7);
        painter.drawLine(12, 17, 12, 22);
        painter.drawLine(2, 12, 7, 12);
        painter.drawLine(17, 12, 22, 12);
        break;
    }
}

class MenuIconEngine final : public QIconEngine
{
  public:
    explicit MenuIconEngine(MenuIcon icon) : icon_(icon) {}
    QIconEngine *clone() const override
    {
        return new MenuIconEngine(icon_);
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette palette = QApplication::palette();
        const QColor ink = mode == QIcon::Disabled ? palette.color(QPalette::Disabled, QPalette::Text)
                           : mode == QIcon::Active || mode == QIcon::Selected
                               ? palette.color(QPalette::HighlightedText)
                               : palette.color(QPalette::Text);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const qreal size = std::min(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - size) / 2, rect.y() + (rect.height() - size) / 2);
        painter->scale(size / 24.0, size / 24.0);
        painter->setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBackground(mode == QIcon::Active || mode == QIcon::Selected
                                   ? palette.color(QPalette::Highlight)
                                   : palette.color(QPalette::Base));
        painter->setBrush(Qt::NoBrush);
        drawGlyph(*painter, icon_);
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap result = pixmap(QSize(qRound(size.width() * scale), qRound(size.height() * scale)), mode, state);
        result.setDevicePixelRatio(scale);
        return result;
    }

  private:
    MenuIcon icon_;
};
} // namespace

QIcon menuIcon(MenuIcon icon)
{
    return QIcon(new MenuIconEngine(icon));
}
} // namespace singlilt

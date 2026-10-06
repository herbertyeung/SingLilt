// Original staff-image display and source-anchor checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "OriginalStaffViewCheck.h"

#include "ui/ScoreView.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QScrollBar>
#include <QTimer>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace singlilt
{
namespace
{
QImage originalPage(int page)
{
    QImage image(1000, 1800, QImage::Format_RGB32);
    image.fill(page == 0 ? QColor("#FFFDF7") : QColor("#F6FAFF"));
    QPainter painter(&image);
    painter.setPen(QPen(Qt::black, 1.4));
    for (int line = 0; line < 5; ++line)
    {
        painter.drawLine(65, 160 + line * 12, 925, 160 + line * 12);
        painter.drawLine(65, 1300 + line * 12, 925, 1300 + line * 12);
    }
    painter.setBrush(Qt::black);
    painter.drawEllipse(QRectF(96, 166, 24, 16));
    painter.drawEllipse(QRectF(196, 166, 24, 16));
    painter.drawEllipse(QRectF(746, 1306, 24, 16));
    painter.setPen(QColor("#234F71"));
    painter.drawText(QPoint(75, 75), QString("Original page %1").arg(page + 1));
    return image;
}

std::vector<QRectF> greenRectangles(const ScoreView &view)
{
    std::vector<QRectF> rectangles;
    for (auto *item : view.scene()->items())
        if (const auto *rectangle = qgraphicsitem_cast<QGraphicsRectItem *>(item))
            if (rectangle->isVisible() && rectangle->pen().color() == QColor("#12A78C"))
                rectangles.push_back(rectangle->rect());
    return rectangles;
}

bool blueVisible(const ScoreView &view)
{
    for (auto *item : view.scene()->items())
        if (const auto *rectangle = qgraphicsitem_cast<QGraphicsRectItem *>(item))
            if (rectangle->isVisible() && rectangle->pen().color() == QColor("#2463EB"))
                return true;
    return false;
}

QRectF blueRectangle(const ScoreView &view)
{
    for (auto *item : view.scene()->items())
        if (const auto *rectangle = qgraphicsitem_cast<QGraphicsRectItem *>(item))
            if (rectangle->isVisible() && rectangle->pen().color() == QColor("#2463EB"))
                return rectangle->rect();
    return {};
}

void mouseEvent(ScoreView &view, QEvent::Type type, QPoint local, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, QPointF(local), QPointF(view.viewport()->mapToGlobal(local)), button, buttons,
                      modifiers);
    QApplication::sendEvent(view.viewport(), &event);
}

bool originalPixels(const ScoreView &view, const QImage &image)
{
    for (auto *item : view.scene()->items())
        if (const auto *pixmap = qgraphicsitem_cast<QGraphicsPixmapItem *>(item))
            return pixmap->pixmap().toImage().convertToFormat(QImage::Format_RGB32) == image;
    return false;
}

void clickScene(ScoreView &view, QPointF scenePoint)
{
    const QPoint local = view.mapFromScene(scenePoint);
    const QPoint global = view.viewport()->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), QPointF(global), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
}
} // namespace

void runOriginalStaffViewCheck(const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        0, &app,
        [&args, &app]
        {
            QJsonArray checks;
            bool passed = true;
            const auto check = [&](const QString &name, bool valid)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", valid}});
                passed &= valid;
            };
            try
            {
                const QString folder = QFileInfo(args.value("report")).absolutePath();
                if (!QDir().mkpath(folder))
                    throw std::runtime_error("Original staff view report directory creation failed");
                const QImage first = originalPage(0);
                const QImage second = originalPage(1);
                const QImage firstBefore = first.copy();
                const QImage secondBefore = second.copy();
                Score score;
                Note lead;
                lead.source = {95, 165, 28, 20};
                lead.hasImageAnchor = true;
                score.notes.push_back(lead);
                auto missing = lead;
                missing.hasImageAnchor = false;
                missing.source = {395, 165, 28, 20};
                score.notes.push_back(missing);
                StaffPerformance performance;
                StaffPerformanceNote primary;
                primary.durationTicks = 960;
                primary.source = lead.source;
                primary.hasImageAnchor = true;
                performance.notes.push_back(primary);
                auto other = primary;
                other.staff = 2;
                other.source = {195, 165, 28, 20};
                performance.notes.push_back(other);
                auto unmapped = primary;
                unmapped.hasImageAnchor = false;
                unmapped.source = missing.source;
                performance.notes.push_back(unmapped);
                auto zero = primary;
                zero.source = {};
                performance.notes.push_back(zero);
                auto invalid = primary;
                invalid.source.x = std::numeric_limits<double>::quiet_NaN();
                performance.notes.push_back(invalid);
                auto outside = primary;
                outside.source = {990, 165, 28, 20};
                performance.notes.push_back(outside);
                auto nextPage = primary;
                nextPage.pageIndex = 1;
                nextPage.startTick = 960;
                nextPage.source = {745, 1305, 28, 20};
                performance.notes.push_back(nextPage);
                auto lower = primary;
                lower.startTick = 960;
                lower.source = {745, 1305, 28, 20};
                performance.notes.push_back(lower);
                ScoreView view;
                view.resize(700, 520);
                view.setScore(first, score, 0, true);
                view.setStaffPerformance(&performance);
                view.show();
                QApplication::processEvents();
                view.fitWidth();
                int selectedStaff = -1;
                int selectedGuide = -1;
                view.staffNoteClicked = [&](int index) { selectedStaff = index; };
                view.noteClicked = [&](int index) { selectedGuide = index; };
                view.setStaffCurrent(120, true, true, true, true);
                check("Original playing overlays include only real mapped same-page finite anchors",
                      greenRectangles(view).size() == 2);
                view.setCurrent(0, false);
                check("A mapped guide can remain blue independently of playing overlays", blueVisible(view));
                view.setStaffCurrent(120, false, true, true, true);
                check("Paused original view clears green while preserving selected mapped blue",
                      greenRectangles(view).empty() && blueVisible(view));
                view.setCurrent(1, true);
                check("A positive but explicitly unmapped guide never draws a blue cursor", !blueVisible(view));
                for (qreal scale : {1.0, 1.25, 1.5})
                {
                    view.resetTransform();
                    view.scale(scale, scale);
                    view.ensureVisible(QRectF(80, 150, 400, 60), 20, 20);
                    QApplication::processEvents();
                    selectedStaff = selectedGuide = -1;
                    clickScene(view, QPointF(109, 175));
                    check(QString("Original bbox click resolves the actual staff event at zoom %1").arg(scale),
                          selectedStaff == 0 && selectedGuide == -1);
                    selectedStaff = selectedGuide = -1;
                    clickScene(view, QPointF(409, 175));
                    check(QString("Unmapped positive bbox is not clickable at zoom %1").arg(scale),
                          selectedStaff == -1 && selectedGuide == -1);
                    selectedStaff = selectedGuide = -1;
                    clickScene(view, QPointF(127, 175));
                    check(QString("Unknown original margin does not use nearest-note fallback at zoom %1")
                              .arg(scale),
                          selectedStaff == -1 && selectedGuide == -1);
                }
                view.resetTransform();
                view.scale(0.5, 0.5);
                view.ensureVisible(QRectF(980, 155, 30, 40), 10, 10);
                selectedStaff = selectedGuide = -1;
                clickScene(view, QPointF(1004, 175));
                check("A positive bbox extending outside the source image is not clickable",
                      selectedStaff == -1 && selectedGuide == -1);
                view.resetTransform();
                view.scale(1.25, 1.25);
                view.setStaffCurrent(120, true, false, true, true);
                check("Muting the primary hand keeps only the mapped other-staff overlay",
                      greenRectangles(view).size() == 1);
                view.setStaffCurrent(120, true, true, true, true);
                view.verticalScrollBar()->setValue(view.verticalScrollBar()->value() + 90);
                const QTransform heldTransform = view.transform();
                const int heldScroll = view.verticalScrollBar()->value();
                const auto heldRects = greenRectangles(view);
                for (int tick = 121; tick < 200; ++tick)
                    view.setStaffCurrent(tick, true, true, true, true);
                check("Sustained original frames preserve user zoom, scroll and rectangles without jitter",
                      view.transform() == heldTransform && view.verticalScrollBar()->value() == heldScroll &&
                          greenRectangles(view) == heldRects);
                view.setStaffCurrent(1100, true, true, true, true);
                const QPoint lowerCenter = view.mapFromScene(QPointF(759, 1315));
                check("A new mapped active note follows into the viewport without changing zoom",
                      view.viewport()->rect().contains(lowerCenter) && view.transform() == heldTransform &&
                          greenRectangles(view).size() == 1);
                selectedStaff = -1;
                clickScene(view, QPointF(759, 1315));
                check("Identical coordinates on another page never replace the current-page staff event",
                      selectedStaff == 7);
                check("Original page zero pixmap remains pixel-identical after playback, clicks and follow",
                      originalPixels(view, first) && first == firstBefore);
                view.setScore(second, score, 1, true);
                view.setStaffPerformance(&performance);
                view.setStaffCurrent(1100, true, true, true, true);
                selectedStaff = -1;
                clickScene(view, QPointF(759, 1315));
                check("Page-qualified original coordinates select only the second-page event",
                      selectedStaff == 6 && greenRectangles(view).size() == 1 && originalPixels(view, second));
                check("Both input source images retain their exact original pixels",
                      first == firstBefore && second == secondBefore);
                view.setScore(first, score);
                selectedGuide = -1;
                view.staffNoteClicked = {};
                view.ensureVisible(QRectF(395, 165, 28, 20), 20, 20);
                clickScene(view, QPointF(409, 175));
                check("Generated legacy view preserves positive display-anchor semantics by default",
                      selectedGuide == 1);
                check("View operations do not change stored musical event pitches or ticks",
                      performance.notes[0].midiPitch == 60 && performance.notes[0].startTick == 0 &&
                          performance.notes[0].durationTicks == 960 && performance.notes[6].pageIndex == 1 &&
                          score.notes[0].degree == 1);

                auto editingPerformance = performance;
                auto overlap = primary;
                overlap.staff = 2;
                overlap.midiPitch = 72;
                editingPerformance.notes.push_back(overlap);
                ScoreView editor;
                editor.resize(700, 520);
                editor.setScore(first, score, 0, true);
                editor.setStaffPerformance(&editingPerformance);
                editor.setAnchorEditingEnabled(true);
                editor.show();
                QApplication::processEvents();
                editor.resetTransform();
                editor.ensureVisible(QRectF(80, 150, 400, 60), 20, 20);
                int starts = 0, commits = 0, clicks = 0, movedIndex = -1, clickedIndex = -1;
                SourceRect movedBox;
                editor.staffAnchorEditStarted = [&](int) { ++starts; };
                editor.staffAnchorMoved = [&](int index, const SourceRect &box)
                {
                    ++commits;
                    movedIndex = index;
                    movedBox = box;
                };
                editor.staffNoteClicked = [&](int index)
                {
                    ++clicks;
                    clickedIndex = index;
                };
                const auto otherBox = editingPerformance.notes[1].source;
                const QRectF otherBlue(otherBox.x - 4, otherBox.y - 6, otherBox.width + 8, otherBox.height + 12);
                editor.setSelectedStaffNote(1);
                editor.setCurrent(0, true);
                check("Independent other-hand selection owns blue without guide replacement",
                      blueRectangle(editor) == otherBlue);
                auto begin = editor.mapFromScene(QPointF(209, 175));
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                check("Editing mouse press selects without seek, audition or undo callbacks",
                      clicks == 0 && starts == 0 && commits == 0);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(1, 0));
                mouseEvent(editor, QEvent::MouseButtonRelease, begin + QPoint(1, 0));
                check("Below-threshold release is a pure staff click, not an anchor move",
                      clicks == 1 && clickedIndex == 1 && starts == 0 && commits == 0);
                for (qreal zoom : {1.0, 1.5})
                {
                    starts = commits = clicks = 0;
                    editor.resetTransform();
                    editor.scale(zoom, zoom);
                    editor.ensureVisible(QRectF(180, 145, 100, 65), 20, 20);
                    editor.setSelectedStaffNote(1);
                    editor.setStaffCurrent(0, false, true, true, false);
                    begin = editor.mapFromScene(QPointF(209, 175));
                    const QPoint end = begin + QPoint(36, -24);
                    const QPointF sceneDelta = editor.mapToScene(end) - editor.mapToScene(begin);
                    mouseEvent(editor, QEvent::MouseButtonPress, begin);
                    mouseEvent(editor, QEvent::MouseMove, end);
                    const auto preview = blueRectangle(editor);
                    const int scroll = editor.verticalScrollBar()->value();
                    const auto transform = editor.transform();
                    editor.setCurrent(0, true);
                    editor.setStaffCurrent(1100, true, true, true, true);
                    check(QString("Dragging at zoom %1 locks preview blue and playback follow").arg(zoom),
                          starts == 1 && commits == 0 && clicks == 0 && blueRectangle(editor) == preview &&
                              editor.verticalScrollBar()->value() == scroll && editor.transform() == transform);
                    mouseEvent(editor, QEvent::MouseButtonRelease, end);
                    check(QString("Zoom %1 commits one independent scene-coordinate translation").arg(zoom),
                          starts == 1 && commits == 1 && clicks == 0 && movedIndex == 1 &&
                              std::abs(movedBox.x - otherBox.x - sceneDelta.x()) < 0.000001 &&
                              std::abs(movedBox.y - otherBox.y - sceneDelta.y()) < 0.000001 &&
                              movedBox.width == otherBox.width && movedBox.height == otherBox.height);
                }
                editor.resetTransform();
                editor.ensureVisible(QRectF(180, 145, 100, 65), 20, 20);
                starts = commits = clicks = 0;
                begin = editor.mapFromScene(QPointF(209, 175));
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(45, 30));
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(&editor, &escape);
                mouseEvent(editor, QEvent::MouseButtonRelease, begin + QPoint(45, 30));
                check("Escape cancels preview movement without commit or release-click",
                      starts == 1 && commits == 0 && clicks == 0 && blueRectangle(editor) == otherBlue);
                starts = commits = clicks = 0;
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(45, 30));
                QFocusEvent lostFocus(QEvent::FocusOut, Qt::OtherFocusReason);
                QApplication::sendEvent(&editor, &lostFocus);
                mouseEvent(editor, QEvent::MouseButtonRelease, begin + QPoint(45, 30));
                check("Focus loss cancels an owned drag without writing its preview",
                      starts == 1 && commits == 0 && clicks == 0 && blueRectangle(editor) == otherBlue);
                starts = commits = clicks = 0;
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(45, 30));
                mouseEvent(editor, QEvent::MouseMove, begin);
                mouseEvent(editor, QEvent::MouseButtonRelease, begin);
                check("A drag returning to its original rectangle creates no material edit or audition",
                      starts == 1 && commits == 0 && clicks == 0 && blueRectangle(editor) == otherBlue);
                for (const QPointF target : {QPointF(-2000, -2000), QPointF(5000, 5000)})
                {
                    starts = commits = clicks = 0;
                    mouseEvent(editor, QEvent::MouseButtonPress, begin);
                    const auto end = editor.mapFromScene(target);
                    mouseEvent(editor, QEvent::MouseMove, end);
                    mouseEvent(editor, QEvent::MouseButtonRelease, end);
                    const bool topLeft = target.x() < 0;
                    check(topLeft ? "Drag translation clamps at original image top/left edges"
                                  : "Drag translation clamps at original image bottom/right edges",
                          starts == 1 && commits == 1 && clicks == 0 && movedIndex == 1 &&
                              movedBox.x == (topLeft ? 0 : first.width() - otherBox.width) &&
                              movedBox.y == (topLeft ? 0 : first.height() - otherBox.height) &&
                              movedBox.width == otherBox.width && movedBox.height == otherBox.height);
                    mouseEvent(editor, QEvent::MouseButtonRelease, end);
                    check("Additional release never duplicates an already emitted anchor commit", commits == 1);
                }
                starts = commits = clicks = 0;
                editor.setSelectedStaffNote(8);
                begin = editor.mapFromScene(QPointF(109, 175));
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseButtonRelease, begin);
                check("Already selected overlapping full event wins an ordinary editing click", clickedIndex == 8);
                mouseEvent(editor, QEvent::MouseButtonPress, begin, Qt::AltModifier);
                mouseEvent(editor, QEvent::MouseButtonRelease, begin, Qt::AltModifier);
                check("Alt-click cycles an overlapping original head to the other independent event",
                      clickedIndex == 0);
                mouseEvent(editor, QEvent::MouseButtonPress, begin, Qt::AltModifier);
                mouseEvent(editor, QEvent::MouseButtonRelease, begin, Qt::AltModifier);
                check("Repeated Alt-click cycles back without moving either overlapping rectangle",
                      clickedIndex == 8 && starts == 0 && commits == 0);
                editor.setSelectedStaffNote(2);
                check("Unmapped positive bbox has no editable selected cursor", !blueVisible(editor));
                editor.setSelectedStaffNote(4);
                check("Nonfinite bbox has no editable selected cursor", !blueVisible(editor));
                editor.setSelectedStaffNote(5);
                check("Out-of-image bbox has no editable selected cursor", !blueVisible(editor));
                editor.setSelectedStaffNote(6);
                check("Other-page bbox has no editable selected cursor", !blueVisible(editor));
                editor.setSelectedStaffNote(1);
                begin = editor.mapFromScene(QPointF(209, 175));
                starts = commits = clicks = 0;
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(45, 30));
                editor.setScore(second, score, 1, true);
                editor.setStaffPerformance(&editingPerformance);
                editor.setSelectedStaffNote(6);
                mouseEvent(editor, QEvent::MouseButtonRelease, begin + QPoint(45, 30));
                check("Scene/page replacement cancels drag and preserves the new source bitmap",
                      starts == 1 && commits == 0 && clicks == 0 && originalPixels(editor, second));
                editor.setScore(first, score, 0, true);
                editor.setStaffPerformance(&editingPerformance);
                editor.setSelectedStaffNote(1);
                editor.resetTransform();
                editor.ensureVisible(QRectF(180, 145, 100, 65), 20, 20);
                starts = commits = clicks = 0;
                editor.staffAnchorEditStarted = [&](int index)
                {
                    ++starts;
                    editor.setScore(first, score, 0, true);
                    editor.setStaffPerformance(&editingPerformance);
                    editor.setSelectedStaffNote(index);
                };
                begin = editor.mapFromScene(QPointF(209, 175));
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(45, 30));
                mouseEvent(editor, QEvent::MouseButtonRelease, begin + QPoint(45, 30));
                check("Start callback may replace the scene without dangling pointers or a stale commit",
                      starts == 1 && commits == 0 && clicks == 0 && originalPixels(editor, first));
                editor.staffAnchorEditStarted = [&](int) { ++starts; };
                editor.staffAnchorMoved = [&](int index, const SourceRect &box)
                {
                    ++commits;
                    movedIndex = index;
                    movedBox = box;
                    editingPerformance.notes[std::size_t(index)].source = box;
                    editor.setScore(first, score, 0, true);
                    editor.setStaffPerformance(&editingPerformance);
                    editor.setSelectedStaffNote(index);
                };
                starts = commits = clicks = 0;
                begin = editor.mapFromScene(QPointF(209, 175));
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(45, 30));
                mouseEvent(editor, QEvent::MouseButtonRelease, begin + QPoint(45, 30));
                check("Release callback may rebuild and retain the newly committed independent selection",
                      starts == 1 && commits == 1 && clicks == 0 && movedIndex == 1 &&
                          blueRectangle(editor) ==
                              QRectF(movedBox.x - 4, movedBox.y - 6, movedBox.width + 8, movedBox.height + 12));
                editor.setAnchorEditingEnabled(false);
                starts = commits = clicks = 0;
                begin = editor.mapFromScene(
                    QPointF(movedBox.x + movedBox.width / 2, movedBox.y + movedBox.height / 2));
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(45, 30));
                mouseEvent(editor, QEvent::MouseButtonRelease, begin + QPoint(45, 30));
                check("Editing-disabled original mode retains ordinary staff click and never emits movement",
                      clicks == 1 && starts == 0 && commits == 0);
                editor.setScore(first, score);
                editor.setStaffPerformance(&editingPerformance);
                editor.setAnchorEditingEnabled(true);
                starts = commits = clicks = 0;
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(45, 30));
                mouseEvent(editor, QEvent::MouseButtonRelease, begin + QPoint(45, 30));
                check("Generated mode never permits anchor editing even if the editing switch remains enabled",
                      clicks == 1 && starts == 0 && commits == 0);
                editor.setScore(first, score, 0, true);
                editor.setStaffPerformance(&editingPerformance);
                editor.resetTransform();
                editor.ensureVisible(QRectF(580, 780, 40, 40), 20, 20);
                begin = editor.mapFromScene(QPointF(600, 800));
                starts = commits = clicks = 0;
                const int panScroll = editor.verticalScrollBar()->value();
                mouseEvent(editor, QEvent::MouseButtonPress, begin);
                mouseEvent(editor, QEvent::MouseMove, begin + QPoint(0, 45));
                mouseEvent(editor, QEvent::MouseButtonRelease, begin + QPoint(0, 45));
                check("Blank original-image drag remains ordinary hand-pan without an anchor edit",
                      editor.verticalScrollBar()->value() != panScroll && starts == 0 && commits == 0 &&
                          clicks == 0);
                check("Dragging and callback rebuilds preserve original pixels and all note pitches/times",
                      originalPixels(editor, first) && first == firstBefore && second == secondBefore &&
                          editingPerformance.notes[1].midiPitch == performance.notes[1].midiPitch &&
                          editingPerformance.notes[1].startTick == performance.notes[1].startTick &&
                          editingPerformance.notes[1].durationTicks == performance.notes[1].durationTicks);
                editor.hide();
                view.hide();
            }
            catch (const std::exception &error)
            {
                check(QString::fromUtf8(error.what()), false);
            }
            QSaveFile report(args.value("report"));
            const QByteArray bytes = QJsonDocument(QJsonObject{{"passed", passed}, {"checks", checks}}).toJson();
            const bool saved =
                report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() && report.commit();
            app.exit(passed && saved ? 0 : 2);
        });
}
} // namespace singlilt

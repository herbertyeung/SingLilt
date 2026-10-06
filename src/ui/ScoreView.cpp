// Score images, note cursors, and source-anchor gestures.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ScoreView.h"
#include "StaffRenderer.h"
#include "i18n/LanguageManager.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QGraphicsItemGroup>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainterPath>
#include <QRawFont>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace singlilt
{
namespace
{
SourceRect visualSource(const QJsonValue &value)
{
    const auto box = value.toArray();
    if (box.size() != 4)
        return {};
    return {box[0].toDouble(), box[1].toDouble(), box[2].toDouble(), box[3].toDouble()};
}

using VisualNoteKey = std::tuple<int, int, std::string, std::int64_t, int, double, double, double, double>;

VisualNoteKey visualNoteKey(const StaffPerformanceNote &note)
{
    return {note.pageIndex, note.staff,    note.voice,        note.startTick,    note.midiPitch,
            note.source.x,  note.source.y, note.source.width, note.source.height};
}

QGraphicsItemGroup *visualGroup(QGraphicsScene &scene, double z)
{
    auto *group = scene.createItemGroup({});
    group->setAcceptedMouseButtons(Qt::NoButton);
    group->setZValue(z);
    return group;
}

void addVisualPath(QGraphicsScene &scene, QGraphicsItemGroup *group, const QPainterPath &path, const QPen &pen,
                   const QBrush &brush, const QString &kind)
{
    auto *item = scene.addPath(path, pen, brush);
    item->setData(0, kind);
    item->setAcceptedMouseButtons(Qt::NoButton);
    group->addToGroup(item);
}

QPainterPath glyphPath(const QFont &font, ushort character)
{
    const auto raw = QRawFont::fromFont(font);
    const auto indices = raw.glyphIndexesForString(QString(QChar(character)));
    return indices.isEmpty() ? QPainterPath{} : raw.pathForGlyph(indices.front());
}

void drawVisualNote(QGraphicsScene &scene, QGraphicsItemGroup *group, const StaffPerformanceNote &note,
                    const SourceRect &source, const QJsonObject &properties)
{
    const double requestedSpacing = properties.value("spacing").toDouble(source.height);
    const double spacing =
        std::clamp(std::isfinite(requestedSpacing) ? requestedSpacing : source.height, 5.0, 100.0);
    const auto font = staffMusicFont(static_cast<int>(std::round(spacing * 4)));
    const ushort headGlyph = note.durationTicks >= 4 * TicksPerQuarter   ? 0xE0A2
                             : note.durationTicks >= 2 * TicksPerQuarter ? 0xE0A3
                                                                         : 0xE0A4;
    const auto head = glyphPath(font, headGlyph);
    const auto glyphBounds = head.boundingRect();
    if (glyphBounds.isEmpty())
        return;
    QTransform transform;
    transform.translate(source.x, source.y);
    transform.scale(source.width / glyphBounds.width(), source.height / glyphBounds.height());
    transform.translate(-glyphBounds.x(), -glyphBounds.y());
    const QColor ink("#D73535");
    addVisualPath(scene, group, transform.map(head), Qt::NoPen, ink, "staffVisualNotehead");
    const double centerX = source.x + source.width / 2;
    const double centerY = source.y + source.height / 2;
    const auto staffLines = properties.value("staffLines").toArray();
    QPainterPath lines;
    if (headGlyph != 0xE0A2 && note.stemDirection != StaffStemDirection::None)
    {
        const bool down = note.stemDirection == StaffStemDirection::Down ||
                          (note.stemDirection == StaffStemDirection::Auto && staffLines.size() == 5 &&
                           centerY < staffLines[2].toDouble());
        const double stemX = down ? source.x : source.x + source.width;
        lines.moveTo(stemX, centerY);
        lines.lineTo(stemX, centerY + (down ? spacing * 3 : -spacing * 3));
    }
    if (staffLines.size() == 5)
    {
        const double top = staffLines[0].toDouble();
        const double bottom = staffLines[4].toDouble();
        if (std::isfinite(top) && std::isfinite(bottom) && bottom > top && centerY >= top - spacing * 8 &&
            centerY <= bottom + spacing * 8)
        {
            for (double y = top - spacing; y >= centerY - spacing * 0.25; y -= spacing)
            {
                lines.moveTo(centerX - source.width * 0.8, y);
                lines.lineTo(centerX + source.width * 0.8, y);
            }
            for (double y = bottom + spacing; y <= centerY + spacing * 0.25; y += spacing)
            {
                lines.moveTo(centerX - source.width * 0.8, y);
                lines.lineTo(centerX + source.width * 0.8, y);
            }
        }
    }
    addVisualPath(scene, group, lines, QPen(ink, std::max(1.0, spacing / 12)), Qt::NoBrush,
                  "staffVisualStemLedger");
    if (properties.contains("alter"))
    {
        const int alter = properties.value("alter").toInt();
        static constexpr std::array<ushort, 5> Accidentals{0xE264, 0xE260, 0xE261, 0xE262, 0xE263};
        if (alter >= -2 && alter <= 2)
        {
            const auto accidental = glyphPath(font, Accidentals[static_cast<std::size_t>(alter + 2)]);
            const auto bounds = accidental.boundingRect();
            QTransform placement;
            placement.translate(source.x - spacing * 0.25 - bounds.right(), centerY);
            addVisualPath(scene, group, placement.map(accidental), Qt::NoPen, ink, "staffVisualAccidental");
        }
    }
}
} // namespace

ScoreView::ScoreView(QWidget *parent) : QGraphicsView(parent), scene_(this)
{
    setScene(&scene_);
    setBackgroundBrush(palette().color(QPalette::Midlight));
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
}
void ScoreView::changeEvent(QEvent *event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        setBackgroundBrush(palette().color(QPalette::Midlight));
}

bool ScoreView::acceptsAnchor(const SourceRect &anchor, bool hasImageAnchor) const
{
    if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y) || !std::isfinite(anchor.width) ||
        !std::isfinite(anchor.height) || anchor.width <= 0 || anchor.height <= 0)
        return false;
    if (!requireImageAnchor_)
        return true;
    return hasImageAnchor && anchor.x >= 0 && anchor.y >= 0 && anchor.x + anchor.width <= imageSize_.width() &&
           anchor.y + anchor.height <= imageSize_.height();
}
void ScoreView::setScore(const QImage &image, const Score &score, int pageIndex, bool requireImageAnchor)
{
    cancelAnchorGesture();
    pageIndex_ = pageIndex;
    imageSize_ = image.size();
    requireImageAnchor_ = requireImageAnchor;
    score_ = score;
    scene_.clear();
    staffVisualLayer_ = nullptr;
    staffAnchorPreview_ = nullptr;
    staffVisualEdits_ = {};
    staffPerformance_.reset();
    staffCursors_.clear();
    activeStaffNotes_.clear();
    staffPlaying_ = false;
    uncertain_.clear();
    cursor_ = nullptr;
    current_ = -1;
    selectedStaffNote_ = -1;
    scene_.addPixmap(QPixmap::fromImage(image));
    scene_.setSceneRect(-20, -20, image.width() + 40, image.height() + 40);
    for (const auto &n : score.notes)
        if (n.pageIndex == pageIndex_ && n.confidence < 0.75 && acceptsAnchor(n.source, n.hasImageAnchor))
        {
            auto *item =
                scene_.addRect(QRectF(n.source.x - 2, n.source.y - 2, n.source.width + 4, n.source.height + 4),
                               QPen(QColor("#DA941C"), 1.4), QColor(245, 176, 57, 25));
            item->setVisible(showUncertain_);
            uncertain_.push_back(item);
        }
    cursor_ = scene_.addRect({}, QPen(QColor("#2463EB"), 2), QColor(36, 99, 235, 55));
    cursor_->setZValue(10);
    cursor_->hide();
}
void ScoreView::setCurrent(int index, bool follow)
{
    if (anchorGesture_ || (requireImageAnchor_ && selectedStaffNote_ >= 0))
        return;
    if (!cursor_ || index < 0 || index >= int(score_.notes.size()))
    {
        if (cursor_)
            cursor_->hide();
        return;
    }
    const auto &b = score_.notes[size_t(index)].source;
    if (score_.notes[size_t(index)].pageIndex != pageIndex_ ||
        !acceptsAnchor(b, score_.notes[size_t(index)].hasImageAnchor))
    {
        cursor_->hide();
        current_ = -1;
        return;
    }
    cursor_->setRect(b.x - 4, b.y - 6, b.width + 8, b.height + 12);
    cursor_->show();
    if (follow && current_ != index)
        ensureVisible(cursor_, 65, 110);
    current_ = index;
}
void ScoreView::fitWidth()
{
    cancelAnchorGesture();
    if (scene_.sceneRect().width() > 0)
    {
        resetTransform();
        double scaleValue = std::max(0.15, (viewport()->width() - 24) / scene_.sceneRect().width());
        scale(scaleValue, scaleValue);
    }
}
void ScoreView::showUncertain(bool enabled)
{
    showUncertain_ = enabled;
    for (auto *item : uncertain_)
        item->setVisible(enabled);
}
void ScoreView::setStaffPerformance(const StaffPerformance *performance)
{
    cancelAnchorGesture();
    for (auto *item : staffCursors_)
        delete item;
    staffCursors_.clear();
    activeStaffNotes_.clear();
    staffPerformance_ = performance ? std::optional<StaffPerformance>(*performance) : std::nullopt;
    if (selectedStaffNote_ >= 0)
        refreshSelectedStaffCursor();
    refreshStaffVisualEdits();
}
void ScoreView::setAnchorEditingEnabled(bool enabled)
{
    if (!enabled)
        cancelAnchorGesture();
    anchorEditingEnabled_ = enabled;
}
void ScoreView::setPitchEditingEnabled(bool enabled)
{
    pitchEditingEnabled_ = enabled;
}
void ScoreView::setTimingEditingEnabled(bool enabled)
{
    timingEditingEnabled_ = enabled;
}
void ScoreView::setStaffVisualEdits(const QJsonArray &edits)
{
    staffVisualEdits_ = edits;
    refreshStaffVisualEdits();
}
void ScoreView::refreshStaffVisualEdits()
{
    delete staffVisualLayer_;
    staffVisualLayer_ = nullptr;
    if (!requireImageAnchor_ || !staffPerformance_ || staffVisualEdits_.isEmpty())
        return;
    std::map<VisualNoteKey, int> indices;
    for (std::size_t index = 0; index < staffPerformance_->notes.size(); ++index)
    {
        const auto &note = staffPerformance_->notes[index];
        if (note.pageIndex != pageIndex_ || !acceptsAnchor(note.source, note.hasImageAnchor))
            continue;
        const auto [found, inserted] = indices.emplace(visualNoteKey(note), static_cast<int>(index));
        if (!inserted)
            found->second = -1;
    }
    std::set<VisualNoteKey> drawn;
    staffVisualLayer_ = visualGroup(scene_, 5);
    staffVisualLayer_->setData(0, "staffVisualEdits");
    for (const auto &value : staffVisualEdits_)
    {
        const auto edit = value.toObject();
        StaffPerformanceNote identity;
        identity.pageIndex = edit.value("pageIndex").toInt(-1);
        identity.staff = edit.value("staff").toInt();
        identity.voice = edit.value("voice").toString().toStdString();
        const double tick = edit.value("startTick").toDouble(-1);
        if (!std::isfinite(tick) || tick < 0 || tick > 1000000000 || tick != std::floor(tick))
            continue;
        identity.startTick = static_cast<std::int64_t>(tick);
        identity.midiPitch = edit.value("midiPitch").toInt(-1);
        identity.source = visualSource(edit.value("source"));
        if (!acceptsAnchor(identity.source, true))
            continue;
        const auto key = visualNoteKey(identity);
        const auto found = indices.find(key);
        if (found == indices.end() || found->second < 0 || !drawn.insert(key).second)
            continue;
        const auto original = visualSource(edit.value("originalSource"));
        if (acceptsAnchor(original, true))
        {
            auto *hint = scene_.addRect(QRectF(original.x, original.y, original.width, original.height),
                                        QPen(QColor("#B78C74"), 1.5, Qt::DashLine), QColor(255, 255, 255, 120));
            hint->setData(0, "staffVisualOriginalHint");
            hint->setAcceptedMouseButtons(Qt::NoButton);
            staffVisualLayer_->addToGroup(hint);
            hint->setZValue(-1);
        }
        drawVisualNote(scene_, staffVisualLayer_, staffPerformance_->notes[std::size_t(found->second)],
                       identity.source, edit);
    }
}
void ScoreView::refreshAnchorPreview()
{
    delete staffAnchorPreview_;
    staffAnchorPreview_ = nullptr;
    if (!anchorGesture_ || !anchorGesture_->dragging || !anchorGesture_->pitchEditing || !staffPerformance_ ||
        anchorGesture_->noteIndex < 0 || anchorGesture_->noteIndex >= int(staffPerformance_->notes.size()))
        return;
    const auto &note = staffPerformance_->notes[std::size_t(anchorGesture_->noteIndex)];
    QJsonObject properties;
    for (const auto &value : staffVisualEdits_)
    {
        const auto edit = value.toObject();
        if (edit.value("pageIndex").toInt(-1) == note.pageIndex && edit.value("staff").toInt() == note.staff &&
            edit.value("voice").toString().toStdString() == note.voice &&
            edit.value("startTick").toDouble(-1) == note.startTick &&
            edit.value("midiPitch").toInt(-1) == note.midiPitch)
        {
            properties = edit;
            break;
        }
    }
    staffAnchorPreview_ = visualGroup(scene_, 8);
    staffAnchorPreview_->setData(0, "staffPitchDragPreview");
    drawVisualNote(scene_, staffAnchorPreview_, note, anchorGesture_->preview, properties);
}
void ScoreView::setSelectedStaffNote(int index)
{
    if (anchorGesture_)
        return;
    selectedStaffNote_ = index;
    refreshSelectedStaffCursor();
}
void ScoreView::refreshSelectedStaffCursor()
{
    if (!cursor_)
        return;
    if (requireImageAnchor_ && staffPerformance_ && selectedStaffNote_ >= 0 &&
        selectedStaffNote_ < int(staffPerformance_->notes.size()))
    {
        const auto &note = staffPerformance_->notes[std::size_t(selectedStaffNote_)];
        if (note.pageIndex == pageIndex_ && acceptsAnchor(note.source, note.hasImageAnchor))
        {
            const auto &box = note.source;
            cursor_->setRect(box.x - 4, box.y - 6, box.width + 8, box.height + 12);
            cursor_->show();
            return;
        }
    }
    selectedStaffNote_ = -1;
    cursor_->hide();
}
void ScoreView::cancelAnchorGesture()
{
    delete staffAnchorPreview_;
    staffAnchorPreview_ = nullptr;
    if (!anchorGesture_)
        return;
    anchorGesture_.reset();
    refreshSelectedStaffCursor();
}
int ScoreView::editableStaffHit(QPointF position, bool cycle) const
{
    if (!staffPerformance_)
        return -1;
    std::vector<int> hits;
    for (std::size_t index = 0; index < staffPerformance_->notes.size(); ++index)
    {
        const auto &note = staffPerformance_->notes[index];
        if (note.pageIndex != pageIndex_ || !acceptsAnchor(note.source, note.hasImageAnchor))
            continue;
        const auto &box = note.source;
        if (QRectF(box.x - 4, box.y - 6, box.width + 8, box.height + 12).contains(position))
            hits.push_back(int(index));
    }
    if (hits.empty())
        return -1;
    const auto selected = std::find(hits.begin(), hits.end(), selectedStaffNote_);
    if (selected == hits.end())
        return hits.front();
    if (!cycle)
        return *selected;
    const auto next = std::next(selected);
    return next == hits.end() ? hits.front() : *next;
}
void ScoreView::updateAnchorGesture(QPoint position)
{
    if (!anchorGesture_)
        return;
    const bool started = !anchorGesture_->dragging;
    if (started &&
        (position - anchorGesture_->pressPosition).manhattanLength() < QApplication::startDragDistance())
        return;
    anchorGesture_->dragging = true;
    const QPointF delta = mapToScene(position) - anchorGesture_->pressScenePosition;
    const auto original = anchorGesture_->original;
    anchorGesture_->preview = {std::clamp(original.x + delta.x(), 0.0, imageSize_.width() - original.width),
                               std::clamp(original.y + delta.y(), 0.0, imageSize_.height() - original.height),
                               original.width, original.height};
    const auto preview = anchorGesture_->preview;
    cursor_->setRect(preview.x - 4, preview.y - 6, preview.width + 8, preview.height + 12);
    cursor_->show();
    refreshAnchorPreview();
    if (started && staffAnchorEditStarted)
    {
        const int index = anchorGesture_->noteIndex;
        const auto callback = staffAnchorEditStarted;
        callback(index);
    }
}
void ScoreView::setStaffCurrent(std::int64_t sourceTick, bool playing, bool primaryEnabled, bool otherEnabled,
                                bool follow)
{
    staffPlaying_ = playing;
    std::size_t count = 0;
    std::vector<int> active;
    active.reserve(128);
    QRectF followRect;
    if (playing && staffPerformance_)
        for (std::size_t index = 0; index < staffPerformance_->notes.size(); ++index)
        {
            const auto &note = staffPerformance_->notes[index];
            const bool primary = note.staff == staffPerformance_->primaryStaff;
            if (note.pageIndex != pageIndex_ || !(primary ? primaryEnabled : otherEnabled) ||
                note.startTick > sourceTick || sourceTick >= note.startTick + note.durationTicks ||
                !acceptsAnchor(note.source, note.hasImageAnchor) || count >= 128)
                continue;
            if (count >= staffCursors_.size())
            {
                auto *item = scene_.addRect({}, QPen(QColor("#12A78C"), 1.5), QColor(18, 167, 140, 45));
                item->setZValue(9);
                staffCursors_.push_back(item);
            }
            auto *item = staffCursors_[count++];
            item->setRect(note.source.x - 3, note.source.y - 3, note.source.width + 6, note.source.height + 6);
            item->show();
            active.push_back(static_cast<int>(index));
            followRect = followRect.isNull() ? item->rect() : followRect.united(item->rect());
        }
    for (std::size_t index = count; index < staffCursors_.size(); ++index)
        staffCursors_[index]->hide();
    if (follow && !anchorGesture_ && !active.empty() && active != activeStaffNotes_)
        ensureVisible(followRect, 65, 110);
    activeStaffNotes_ = std::move(active);
}
void ScoreView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        auto pos = mapToScene(event->pos());
        if (requireImageAnchor_ && anchorEditingEnabled_)
        {
            const int index = editableStaffHit(pos, event->modifiers() & Qt::AltModifier);
            if (index >= 0)
            {
                cancelAnchorGesture();
                setFocus(Qt::MouseFocusReason);
                setSelectedStaffNote(index);
                const auto box = staffPerformance_->notes[std::size_t(index)].source;
                anchorGesture_ = AnchorGesture{
                    index, event->pos(), pos, box, box, false, pitchEditingEnabled_, timingEditingEnabled_};
                event->accept();
                return;
            }
        }
        if (staffPerformance_ && staffNoteClicked)
            for (std::size_t index = 0; index < staffPerformance_->notes.size(); ++index)
            {
                const auto &box = staffPerformance_->notes[index].source;
                if (staffPerformance_->notes[index].pageIndex != pageIndex_)
                    continue;
                const QRectF bounds(box.x, box.y, box.width, box.height);
                const auto hit = requireImageAnchor_ ? bounds : bounds.adjusted(-3, -3, 3, 3);
                if (acceptsAnchor(box, staffPerformance_->notes[index].hasImageAnchor) && hit.contains(pos))
                {
                    staffNoteClicked(int(index));
                    event->accept();
                    return;
                }
            }
        int nearest = -1;
        double best = 35 * 35;
        for (size_t i = 0; i < score_.notes.size(); ++i)
        {
            if (score_.notes[i].pageIndex != pageIndex_)
                continue;
            const auto &b = score_.notes[i].source;
            if (!acceptsAnchor(b, score_.notes[i].hasImageAnchor))
                continue;
            QRectF r(b.x, b.y, b.width, b.height);
            const auto hit = requireImageAnchor_ ? r : r.adjusted(-5, -8, 5, 8);
            if (hit.contains(pos))
            {
                nearest = int(i);
                break;
            }
            if (requireImageAnchor_)
                continue;
            auto d = r.center() - pos;
            double distance = d.x() * d.x() + d.y() * d.y();
            if (distance < best)
            {
                best = distance;
                nearest = int(i);
            }
        }
        if (nearest >= 0 && noteClicked)
        {
            noteClicked(nearest);
            event->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}
void ScoreView::mouseMoveEvent(QMouseEvent *event)
{
    if (anchorGesture_)
    {
        if (event->buttons() & Qt::LeftButton)
            updateAnchorGesture(event->pos());
        else
            cancelAnchorGesture();
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}
void ScoreView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && anchorGesture_)
    {
        updateAnchorGesture(event->pos());
        event->accept();
        if (!anchorGesture_)
            return;
        const auto gesture = *anchorGesture_;
        anchorGesture_.reset();
        delete staffAnchorPreview_;
        staffAnchorPreview_ = nullptr;
        refreshSelectedStaffCursor();
        if (gesture.dragging)
        {
            if (gesture.preview.x != gesture.original.x || gesture.preview.y != gesture.original.y)
            {
                if ((gesture.pitchEditing || gesture.timingEditing) && staffMusicalMoved)
                {
                    const auto callback = staffMusicalMoved;
                    callback(gesture.noteIndex, gesture.preview, gesture.pitchEditing, gesture.timingEditing);
                }
                else if (!gesture.pitchEditing && !gesture.timingEditing && staffAnchorMoved)
                {
                    const auto callback = staffAnchorMoved;
                    callback(gesture.noteIndex, gesture.preview);
                }
            }
        }
        else if (staffNoteClicked)
        {
            const auto callback = staffNoteClicked;
            callback(gesture.noteIndex);
        }
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}
void ScoreView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && anchorGesture_)
    {
        cancelAnchorGesture();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier && requireImageAnchor_ &&
        anchorEditingEnabled_ && !staffPlaying_ && (hasFocus() || viewport()->hasFocus()) && staffPerformance_ &&
        staffPerformance_->notes.size() > 1 && selectedStaffNote_ >= 0 &&
        selectedStaffNote_ < int(staffPerformance_->notes.size()) &&
        staffPerformance_->notes[std::size_t(selectedStaffNote_)].pageIndex == pageIndex_ &&
        staffNoteDeleteRequested)
    {
        event->accept();
        if (!event->isAutoRepeat())
        {
            cancelAnchorGesture();
            const int index = selectedStaffNote_;
            const auto callback = staffNoteDeleteRequested;
            callback(index);
        }
        return;
    }
    QGraphicsView::keyPressEvent(event);
}
void ScoreView::contextMenuEvent(QContextMenuEvent *event)
{
    if (!requireImageAnchor_ || !anchorEditingEnabled_ || staffPlaying_ || !staffPerformance_)
    {
        QGraphicsView::contextMenuEvent(event);
        return;
    }
    const QPointF position = mapToScene(event->pos());
    if (!QRectF(QPointF(0, 0), QSizeF(imageSize_)).contains(position))
    {
        event->ignore();
        return;
    }
    cancelAnchorGesture();
    setFocus(Qt::MouseFocusReason);
    const int index = editableStaffHit(position, event->modifiers() & Qt::AltModifier);
    if (index >= 0)
    {
        setSelectedStaffNote(index);
        if (staffNoteClicked)
        {
            const auto callback = staffNoteClicked;
            callback(index);
        }
    }
    QMenu menu(this);
    auto *action =
        menu.addAction(trText(index >= 0 ? "ui.original_playback.delete_note" : "ui.original_playback.add_note"));
    action->setEnabled(index >= 0 ? bool(staffNoteDeleteRequested) && staffPerformance_->notes.size() > 1
                                  : bool(staffNoteAddRequested));
    QAction *chord = nullptr;
    if (index >= 0)
    {
        chord = menu.addAction(trText("ui.original_playback.add_same_beat_note"));
        chord->setEnabled(bool(staffNoteChordAddRequested));
    }
    event->accept();
    const int menuPage = pageIndex_;
    const auto *chosen = menu.exec(event->globalPos());
    if (!chosen || !requireImageAnchor_ || !anchorEditingEnabled_ || staffPlaying_ || pageIndex_ != menuPage)
        return;
    if (index >= 0)
    {
        if (!staffPerformance_ || selectedStaffNote_ != index || index >= int(staffPerformance_->notes.size()) ||
            staffPerformance_->notes[std::size_t(index)].pageIndex != pageIndex_)
            return;
        if (chosen == chord && staffNoteChordAddRequested)
        {
            const auto callback = staffNoteChordAddRequested;
            callback(index, position);
        }
        else if (chosen == action && staffPerformance_->notes.size() > 1 && staffNoteDeleteRequested)
        {
            const auto callback = staffNoteDeleteRequested;
            callback(index);
        }
    }
    else if (chosen == action && staffNoteAddRequested)
    {
        const auto callback = staffNoteAddRequested;
        callback(position);
    }
}
void ScoreView::focusOutEvent(QFocusEvent *event)
{
    cancelAnchorGesture();
    QGraphicsView::focusOutEvent(event);
}
void ScoreView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (emptyDoubleClicked && event->button() == Qt::LeftButton)
    {
        emptyDoubleClicked(mapToScene(event->pos()));
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}
void ScoreView::wheelEvent(QWheelEvent *event)
{
    if (anchorGesture_)
        cancelAnchorGesture();
    if (event->modifiers() & Qt::ControlModifier)
    {
        double f = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
        double s = transform().m11() * f;
        if (s >= .15 && s <= 5)
            scale(f, f);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}
} // namespace singlilt

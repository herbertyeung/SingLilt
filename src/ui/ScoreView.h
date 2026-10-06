// Score images, note cursors, and source-anchor gestures.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "domain/Score.h"
#include "domain/StaffPerformance.h"
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QJsonArray>
#include <functional>
class QGraphicsRectItem;
class QGraphicsItemGroup;
namespace singlilt
{
class ScoreView : public QGraphicsView
{
  public:
    explicit ScoreView(QWidget *parent = nullptr);
    void setScore(const QImage &image, const Score &score, int pageIndex = 0, bool requireImageAnchor = false);
    void setCurrent(int index, bool follow);
    void fitWidth();
    void showUncertain(bool enabled);
    void setStaffPerformance(const StaffPerformance *performance);
    void setAnchorEditingEnabled(bool enabled);
    void setPitchEditingEnabled(bool enabled);
    void setTimingEditingEnabled(bool enabled);
    void setStaffVisualEdits(const QJsonArray &edits);
    void setSelectedStaffNote(int index);
    void setStaffCurrent(std::int64_t sourceTick, bool playing, bool primaryEnabled, bool otherEnabled,
                         bool follow = false);
    int currentNoteIndex() const
    {
        return current_;
    }
    std::function<void(int)> noteClicked;
    std::function<void(int)> staffNoteClicked;
    std::function<void(int)> staffAnchorEditStarted;
    std::function<void(int, const SourceRect &)> staffAnchorMoved;
    std::function<void(int, const SourceRect &, bool, bool)> staffMusicalMoved;
    std::function<void(QPointF)> staffNoteAddRequested;
    std::function<void(int, QPointF)> staffNoteChordAddRequested;
    std::function<void(int)> staffNoteDeleteRequested;
    std::function<void(QPointF)> emptyDoubleClicked;

  protected:
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private:
    bool acceptsAnchor(const SourceRect &anchor, bool hasImageAnchor) const;
    void refreshSelectedStaffCursor();
    void cancelAnchorGesture();
    void updateAnchorGesture(QPoint position);
    void refreshStaffVisualEdits();
    void refreshAnchorPreview();
    int editableStaffHit(QPointF position, bool cycle) const;
    struct AnchorGesture
    {
        int noteIndex = -1;
        QPoint pressPosition;
        QPointF pressScenePosition;
        SourceRect original;
        SourceRect preview;
        bool dragging = false;
        bool pitchEditing = false;
        bool timingEditing = false;
    };
    QGraphicsScene scene_;
    Score score_;
    QGraphicsRectItem *cursor_ = nullptr;
    std::vector<QGraphicsRectItem *> uncertain_;
    std::optional<StaffPerformance> staffPerformance_;
    std::vector<QGraphicsRectItem *> staffCursors_;
    std::vector<int> activeStaffNotes_;
    QSize imageSize_;
    int current_ = -1;
    int pageIndex_ = 0;
    bool showUncertain_ = true;
    bool requireImageAnchor_ = false;
    bool anchorEditingEnabled_ = false;
    bool staffPlaying_ = false;
    int selectedStaffNote_ = -1;
    std::optional<AnchorGesture> anchorGesture_;
    bool pitchEditingEnabled_ = false;
    bool timingEditingEnabled_ = false;
    QJsonArray staffVisualEdits_;
    QGraphicsItemGroup *staffVisualLayer_ = nullptr;
    QGraphicsItemGroup *staffAnchorPreview_ = nullptr;
};
} // namespace singlilt

// Staff views, source-anchor editing, and voice controls.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ClassroomDialog.h"
#include "InstrumentNames.h"
#include "MainWindow.h"
#include "NotationRenderer.h"
#include "ScoreView.h"
#include "StaffCorrectionDialog.h"
#include "StaffNoteDialog.h"
#include "StaffNoteEditor.h"
#include "StaffRenderer.h"
#include "application/StaffAnchorCorrection.h"
#include "application/StaffNoteEditing.h"
#include "application/StaffPositionMapping.h"
#include "application/StaffTimingMapping.h"
#include "i18n/LanguageManager.h"
#include "recognition/StaffPageSplitter.h"
#include "storage/MusicXmlImporter.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace singlilt
{

namespace
{
QJsonArray staffBoxJson(const SourceRect &box)
{
    return {box.x, box.y, box.width, box.height};
}

bool matchesStaffEdit(const QJsonObject &edit, const StaffPerformanceNote &note)
{
    return edit.value("pageIndex").toInt(-1) == note.pageIndex && edit.value("staff").toInt() == note.staff &&
           edit.value("voice").toString().toStdString() == note.voice &&
           edit.value("startTick").toDouble(-1) == note.startTick &&
           edit.value("midiPitch").toInt(-1) == note.midiPitch &&
           edit.value("source").toArray() == staffBoxJson(note.source);
}

void recordStaffVisualEdit(Project &project, const StaffPerformanceNote *before, const StaffPerformanceNote &after)
{
    QJsonArray edits;
    QJsonArray original = before && before->hasImageAnchor ? staffBoxJson(before->source) : QJsonArray{};
    QString timingSource;
    for (const auto &entry : project.processing.value("staffVisualEdits").toArray())
    {
        const auto edit = entry.toObject();
        if (before && matchesStaffEdit(edit, *before))
        {
            original = edit.value("originalSource").toArray();
            timingSource = edit.value("timingSource").toString();
        }
        else
            edits.append(edit);
    }
    const QPointF center(after.source.x + after.source.width / 2, after.source.y + after.source.height / 2);
    const auto placement = suggestStaffNotePosition(project, after, center);
    if (!placement)
        return;
    QJsonArray lines;
    for (const double y : placement->staffLines)
        lines.append(y);
    QJsonObject edit{{"pageIndex", after.pageIndex},
                     {"staff", after.staff},
                     {"voice", QString::fromStdString(after.voice)},
                     {"startTick", double(after.startTick)},
                     {"midiPitch", after.midiPitch},
                     {"source", staffBoxJson(after.source)},
                     {"originalSource", original},
                     {"staffLines", lines},
                     {"spacing", placement->lineSpacing},
                     {"alter", after.staffSpelling ? after.staffSpelling->alter : 0}};
    if (before && (before->startTick != after.startTick || before->durationTicks != after.durationTicks))
        timingSource = "manual";
    if (!timingSource.isEmpty())
        edit.insert("timingSource", timingSource);
    edits.append(edit);
    project.processing.insert("staffVisualEdits", edits);
}

void setStaffTimingSource(Project &project, const StaffPerformanceNote &note, const QString &source)
{
    QJsonArray edits;
    for (const auto &entry : project.processing.value("staffVisualEdits").toArray())
    {
        auto edit = entry.toObject();
        if (matchesStaffEdit(edit, note))
            edit.insert("timingSource", source);
        edits.append(edit);
    }
    project.processing.insert("staffVisualEdits", edits);
}

int findEditedStaffNote(const Project &project, const StaffPerformanceNote &note)
{
    int found = -1;
    for (std::size_t index = 0; index < project.staffPerformance->notes.size(); ++index)
    {
        const auto &candidate = project.staffPerformance->notes[index];
        if (candidate.midiPitch == note.midiPitch && candidate.staff == note.staff &&
            candidate.voice == note.voice && candidate.startTick == note.startTick &&
            candidate.durationTicks == note.durationTicks && candidate.pageIndex == note.pageIndex &&
            staffBoxJson(candidate.source) == staffBoxJson(note.source))
            found = int(index);
    }
    return found;
}
} // namespace

void MainWindow::selectStaffNote(int index)
{
    if (!project_.staffPerformance || index < 0 || index >= int(project_.staffPerformance->notes.size()) ||
        busy_ || audioLoading_ || notePreviewLoading_)
        return;
    const int guideIndex = project_.staffPerformance->notes[std::size_t(index)].sourceNoteIndex;
    if (!project_.staffImagePlayback && project_.notationStyle == NotationStyle::Numbered && guideIndex >= 0)
    {
        selectNote(guideIndex, false);
        if (selected_ != guideIndex || hasNoteDraft() || correctionMode_)
            return;
        const auto event = std::find_if(timeline_.events.begin(), timeline_.events.end(),
                                        [guideIndex](const TimelineEvent &entry)
                                        { return entry.sourceNoteIndex == std::size_t(guideIndex); });
        if (event != timeline_.events.end())
            seekPracticeTick(event->startTick);
        const auto &notes = project_.staffPerformance->notes;
        const auto current = std::find_if(notes.begin(), notes.end(), [guideIndex](const StaffPerformanceNote &note)
                                          { return note.sourceNoteIndex == guideIndex; });
        if (current != notes.end() && (!player_.isPlaying() || !player_.practiceMix().melodyEnabled) &&
            !originalAudio_.isPlaying())
            auditionStaffNote(*current);
        return;
    }
    if (project_.staffImagePlayback && !loadingNote_ && hasNoteDraft())
    {
        if (index == selectedStaffNote_)
            return;
        const auto requested = project_.staffPerformance->notes[std::size_t(index)];
        if (!resolveNoteDraft())
        {
            view_->setSelectedStaffNote(selectedStaffNote_);
            return;
        }
        index = findEditedStaffNote(project_, requested);
        if (index < 0)
            return;
    }
    const auto &note = project_.staffPerformance->notes[std::size_t(index)];
    if (!project_.staffPages.empty() && note.pageIndex != staffPageIndex_)
        setStaffPage(note.pageIndex);
    if (project_.staffImagePlayback)
        selectedStaffNote_ = index;
    if (correctionMode_ && project_.staffImagePlayback)
    {
        view_->setSelectedStaffNote(index);
        noteTitle_->setText(trText("ui.original_playback.selected")
                                .arg(index + 1)
                                .arg(project_.staffPerformance->notes.size())
                                .arg(note.midiPitch)
                                .arg(note.staff)
                                .arg(QString::fromStdString(note.voice)));
        setStatus("ui.original_playback.drag_help");
        refreshStaffAnchorEditing();
        return;
    }
    std::int64_t sourceTick = 0;
    for (std::size_t guide = 0; guide < project_.score.notes.size(); ++guide)
    {
        const auto end = sourceTick + project_.score.notes[guide].durationTicks;
        if (note.startTick < end)
        {
            const auto event = std::find_if(timeline_.events.begin(), timeline_.events.end(),
                                            [guide](const auto &value) { return value.sourceNoteIndex == guide; });
            if (event != timeline_.events.end())
                player_.seek(event->startTick + note.startTick - sourceTick);
            selectNote(int(guide), false);
            break;
        }
        sourceTick = end;
    }
    if (!player_.isPlaying())
        player_.previewNote(
            note.midiPitch + player_.transpose() + project_.score.tonic - project_.staffPerformance->sourceTonic,
            note.staff == project_.staffPerformance->primaryStaff ? project_.staffPerformance->primaryProgram
                                                                  : project_.staffPerformance->otherProgram,
            note.velocity);
    refreshStaffAnchorEditing();
}
void MainWindow::refreshStaffAnchorEditing()
{
    if (!view_)
        return;
    refreshStaffNoteInspector();
    const bool enabled = canEditOriginalStaff();
    view_->setAnchorEditingEnabled(enabled);
    const auto *pitchDrag = findChild<QCheckBox *>("staffPitchDrag");
    view_->setPitchEditingEnabled(enabled && pitchDrag && pitchDrag->isChecked());
    const auto *timingDrag = findChild<QCheckBox *>("staffTimingDrag");
    view_->setTimingEditingEnabled(enabled && timingDrag && timingDrag->isChecked());
    if (!staffNoteEditor_ || !staffNoteEditor_->hasDraft())
        view_->setStaffVisualEdits(project_.processing.value("staffVisualEdits").toArray());
    for (const auto *name : {"staffPitchDrag", "staffTimingDrag"})
        if (auto *control = findChild<QCheckBox *>(name))
        {
            control->setVisible(project_.staffImagePlayback && project_.staffPerformance.has_value());
            control->setEnabled(enabled);
        }
    view_->setSelectedStaffNote(canEditOriginalStaff(true) ? selectedStaffNote_ : -1);
    if (project_.staffImagePlayback)
        view_->setToolTip(trText(enabled ? "ui.original_playback.drag_help" : "ui.original_playback.help"));
    if (auto *hint = findChild<QLabel *>("inspectorHint"))
        bindText(hint, project_.staffImagePlayback ? "ui.original_playback.inspector_help" : "ui.inspector.hint");
    for (const auto *name : {"staffImageAddNote", "staffImageDeleteNote", "staffImageNoteAudition"})
        if (auto *button = findChild<QPushButton *>(name))
        {
            button->setVisible(project_.staffImagePlayback && project_.staffPerformance.has_value());
            const bool selection =
                project_.staffPerformance && selectedStaffNote_ >= 0 &&
                selectedStaffNote_ < int(project_.staffPerformance->notes.size()) &&
                project_.staffPerformance->notes[std::size_t(selectedStaffNote_)].pageIndex == staffPageIndex_ &&
                (QString::fromLatin1(name) != "staffImageDeleteNote" ||
                 project_.staffPerformance->notes.size() > 1);
            button->setEnabled(enabled && (QString::fromLatin1(name) == "staffImageAddNote" || selection));
        }
}
bool MainWindow::canEditOriginalStaff(bool allowDraft) const
{
    return correctionMode_ && project_.staffImagePlayback && project_.staffPerformance &&
           project_.processing.value("local").toBool() && !busy_ && !audioLoading_ && !notePreviewLoading_ &&
           !classroom_ && (allowDraft || !hasNoteDraft()) && !localStaffTask_.isRunning() &&
           localStaffTask_.state() != LocalStaffRecognitionTask::State::Ready && !cloudTask_.isRunning() &&
           cloudTask_.state() != CloudRecognitionTask::State::Ready && !audioTask_.isRunning() &&
           audioTask_.state() != AudioTranscriptionTask::State::Ready && !player_.isPlaying() &&
           !originalAudio_.isPlaying();
}

void MainWindow::addStaffNoteAt(QPointF position, std::optional<int> sameBeatReference)
{
    if (!canEditOriginalStaff())
        return;
    if (sameBeatReference &&
        (*sameBeatReference < 0 || std::size_t(*sameBeatReference) >= project_.staffPerformance->notes.size() ||
         project_.staffPerformance->notes[std::size_t(*sameBeatReference)].pageIndex != staffPageIndex_))
        return;
    const QImage &image = project_.staffPages.empty()
                              ? project_.image
                              : project_.staffPages[std::size_t(staffPageIndex_)].sourceImage;
    if (!QRectF(image.rect()).contains(position))
        return;
    try
    {
        const StaffPerformanceNote *reference = nullptr;
        if (sameBeatReference)
        {
            reference = &project_.staffPerformance->notes[std::size_t(*sameBeatReference)];
            position.setX(reference->source.x + reference->source.width / 2);
        }
        double closest = std::numeric_limits<double>::max();
        for (const auto &note : project_.staffPerformance->notes)
            if (!sameBeatReference && note.pageIndex == staffPageIndex_ && note.hasImageAnchor)
            {
                const QPointF center(note.source.x + note.source.width / 2,
                                     note.source.y + note.source.height / 2);
                const auto difference = position - center;
                const double distance = difference.x() * difference.x() + difference.y() * difference.y();
                if (distance < closest)
                {
                    reference = &note;
                    closest = distance;
                }
            }
        StaffPerformanceNote draft;
        draft.pageIndex = staffPageIndex_;
        if (reference)
        {
            draft.midiPitch = reference->midiPitch;
            draft.startTick = reference->startTick;
            draft.durationTicks = reference->durationTicks;
            draft.staff = reference->staff;
            draft.voice = reference->voice;
            draft.velocity = reference->velocity;
        }
        else
        {
            const auto measure =
                std::find_if(project_.score.writtenMeasures.begin(), project_.score.writtenMeasures.end(),
                             [this](const auto &entry) { return entry.pageIndex == staffPageIndex_; });
            if (measure == project_.score.writtenMeasures.end())
                return;
            draft.startTick = measure->startTick;
            draft.durationTicks = std::min<std::int64_t>(TicksPerQuarter, measure->durationTicks);
            draft.staff = project_.staffPerformance->primaryStaff;
        }
        const double width = reference ? reference->source.width : std::max(8.0, image.width() / 100.0);
        const double height = reference ? reference->source.height : width * 0.7;
        draft.source = {std::clamp(position.x() - width / 2, 0.0, std::max(0.0, image.width() - width)),
                        std::clamp(position.y() - height / 2, 0.0, std::max(0.0, image.height() - height)),
                        std::min(width, double(image.width())), std::min(height, double(image.height()))};
        draft.hasImageAnchor = true;

        auto hint = suggestStaffNoteAt(project_, staffPageIndex_, position, sameBeatReference);
        if (sameBeatReference && hint && reference && hint->note.midiPitch == reference->midiPitch)
        {
            for (int step = 1; step <= 12; ++step)
            {
                const QPointF above(position.x(), reference->source.y + reference->source.height / 2 -
                                                      step * hint->lineSpacing / 2);
                const auto candidate = suggestStaffNoteAt(project_, staffPageIndex_, above, sameBeatReference);
                if (!candidate)
                    break;
                const bool occupied =
                    std::any_of(project_.staffPerformance->notes.begin(), project_.staffPerformance->notes.end(),
                                [&](const auto &note)
                                {
                                    return note.staff == candidate->note.staff &&
                                           note.voice == candidate->note.voice &&
                                           note.startTick == candidate->note.startTick &&
                                           note.midiPitch == candidate->note.midiPitch;
                                });
                if (!occupied)
                {
                    hint = candidate;
                    break;
                }
            }
        }
        if (hint)
            draft = hint->note;
        const bool timingAvailable = (hint && hint->timingSuggested) || sameBeatReference.has_value();
        const bool chord = (hint && hint->sameBeat) || sameBeatReference.has_value();
        const auto suggested = draft;
        StaffNoteDialog dialog(project_, draft, this);
        QString timingMessage = trText("ui.original_playback.timing_manual");
        if (timingAvailable)
        {
            const auto measure =
                std::find_if(project_.score.writtenMeasures.begin(), project_.score.writtenMeasures.end(),
                             [&](const auto &entry)
                             {
                                 return draft.startTick >= entry.startTick &&
                                        draft.startTick < entry.startTick + entry.durationTicks;
                             });
            if (measure != project_.score.writtenMeasures.end())
            {
                timingMessage =
                    trText(chord ? "ui.original_playback.timing_chord" : "ui.original_playback.timing_distance")
                        .arg(std::distance(project_.score.writtenMeasures.begin(), measure) + 1)
                        .arg(double(draft.startTick - measure->startTick) / TicksPerQuarter, 0, 'g', 8)
                        .arg(double(draft.durationTicks) / TicksPerQuarter, 0, 'g', 8);
                if (!chord && hint && hint->timingReferenceIndex >= 0 &&
                    std::size_t(hint->timingReferenceIndex) < project_.staffPerformance->notes.size())
                {
                    const auto &clockReference =
                        project_.staffPerformance->notes[std::size_t(hint->timingReferenceIndex)];
                    timingMessage =
                        timingMessage
                            .arg(draft.source.x + draft.source.width / 2 - clockReference.source.x -
                                     clockReference.source.width / 2,
                                 0, 'g', 8)
                            .arg(double(draft.startTick - clockReference.startTick) / TicksPerQuarter, 0, 'g', 8);
                }
            }
        }
        dialog.setTimingSuggestion(timingMessage, timingAvailable);
        dialog.auditionRequested = [this](const auto &note) { auditionStaffNote(note); };
        dialog.draftPreviewChanged = [this](const auto &note)
        {
            auto preview = project_;
            preview.staffPerformance->notes.push_back(note);
            recordStaffVisualEdit(preview, nullptr, note);
            view_->setStaffPerformance(&*preview.staffPerformance);
            view_->setStaffVisualEdits(preview.processing.value("staffVisualEdits").toArray());
        };
        if (dialog.exec() != QDialog::Accepted || !dialog.correctedProject() || !canEditOriginalStaff())
        {
            refreshStaffAnchorView();
            return;
        }
        auto corrected = *dialog.correctedProject();
        const auto &added = dialog.note();
        int selected = -1;
        // Equal-onset notes keep their stable order; the new note is the last matching event.
        for (std::size_t index = 0; index < corrected.staffPerformance->notes.size(); ++index)
        {
            const auto &note = corrected.staffPerformance->notes[index];
            if (note.midiPitch == added.midiPitch && note.startTick == added.startTick &&
                note.durationTicks == added.durationTicks && note.staff == added.staff &&
                note.voice == added.voice && note.pageIndex == added.pageIndex &&
                note.source.x == added.source.x && note.source.y == added.source.y)
                selected = int(index);
        }
        if (selected >= 0)
        {
            recordStaffVisualEdit(corrected, nullptr, corrected.staffPerformance->notes[std::size_t(selected)]);
            const bool manual = !timingAvailable || added.startTick != suggested.startTick ||
                                added.durationTicks != suggested.durationTicks;
            setStaffTimingSource(corrected, corrected.staffPerformance->notes[std::size_t(selected)],
                                 manual  ? "manual"
                                 : chord ? "chord"
                                         : "distance");
        }
        applyStaffNoteEdit(std::move(corrected), selected, "ui.original_playback.add_note");
        setStatus("ui.original_playback.note_added");
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}

void MainWindow::deleteStaffNote(int index)
{
    if (!canEditOriginalStaff() || index < 0 || index >= int(project_.staffPerformance->notes.size()) ||
        project_.staffPerformance->notes[std::size_t(index)].pageIndex != staffPageIndex_ ||
        project_.staffPerformance->notes.size() <= 1)
        return;
    try
    {
        const auto removed = project_.staffPerformance->notes[std::size_t(index)];
        auto corrected = deletedStaffNote(project_, index);
        QJsonArray visual;
        for (const auto &entry : corrected.processing.value("staffVisualEdits").toArray())
            if (!matchesStaffEdit(entry.toObject(), removed))
                visual.append(entry);
        corrected.processing.insert("staffVisualEdits", visual);
        applyStaffNoteEdit(std::move(corrected), -1, "ui.original_playback.delete_note");
        setStatus("ui.original_playback.note_deleted");
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}

void MainWindow::auditionStaffNote(const StaffPerformanceNote &note)
{
    if (!project_.staffPerformance || busy_ || audioLoading_)
        return;
    const auto &performance = *project_.staffPerformance;
    const int program =
        note.staff == performance.primaryStaff ? performance.primaryProgram : performance.otherProgram;
    const int pitch = note.midiPitch + player_.transpose() + project_.score.tonic - performance.sourceTonic;
    if (!player_.previewNote(pitch, program, note.velocity))
        setStatusMessage(player_.errorString());
}

void MainWindow::moveStaffNote(int index, const SourceRect &anchor, bool pitchEditing, bool timingEditing)
{
    if (!canEditOriginalStaff() || index < 0 || index >= int(project_.staffPerformance->notes.size()) ||
        project_.staffPerformance->notes[std::size_t(index)].pageIndex != staffPageIndex_)
        return;
    try
    {
        const auto before = project_.staffPerformance->notes[std::size_t(index)];
        QPointF center(anchor.x + anchor.width / 2, anchor.y + anchor.height / 2);
        auto edited = before;
        edited.source = anchor;
        if (timingEditing && std::abs(anchor.x - before.source.x) > 0.000001)
        {
            const auto timing = suggestStaffTimingMove(project_, index, center);
            if (!timing)
            {
                refreshStaffAnchorView();
                setStatus("ui.original_playback.timing_move_unknown");
                return;
            }
            edited.startTick = timing->startTick;
            edited.source.x = timing->alignedX - edited.source.width / 2;
            center.setX(timing->alignedX);
        }
        if (pitchEditing && std::abs(anchor.y - before.source.y) > 0.000001)
        {
            auto context = before;
            context.startTick = edited.startTick;
            const auto pitch = suggestStaffNotePosition(project_, context, center);
            if (!pitch)
            {
                refreshStaffAnchorView();
                setStatus("ui.original_playback.position_unknown");
                return;
            }
            edited.midiPitch = pitch->note.midiPitch;
            edited.staffSpelling = pitch->note.staffSpelling;
            edited.source = pitch->note.source;
        }
        auto corrected = updatedStaffNote(project_, index, edited);
        const int selected = findEditedStaffNote(corrected, edited);
        if (selected < 0)
            throw std::runtime_error(trText("ui.staff_correction.invalid_note").toStdString());
        recordStaffVisualEdit(corrected, &before, corrected.staffPerformance->notes[std::size_t(selected)]);
        if (edited.startTick != before.startTick)
            setStaffTimingSource(corrected, corrected.staffPerformance->notes[std::size_t(selected)], "distance");
        applyStaffNoteEdit(std::move(corrected), selected, "ui.original_playback.musical_drag");
        if (edited.startTick != before.startTick)
        {
            const auto measure =
                std::find_if(project_.score.writtenMeasures.begin(), project_.score.writtenMeasures.end(),
                             [&](const auto &entry)
                             {
                                 return edited.startTick >= entry.startTick &&
                                        edited.startTick < entry.startTick + entry.durationTicks;
                             });
            if (measure != project_.score.writtenMeasures.end())
                setStatus(
                    "ui.original_playback.timing_moved",
                    {QString::number(double(edited.startTick - measure->startTick) / TicksPerQuarter, 'g', 8)});
        }
        else
            setStatus("ui.original_playback.pitch_moved");
        auditionStaffNote(project_.staffPerformance->notes[std::size_t(selected)]);
    }
    catch (const std::exception &error)
    {
        refreshStaffAnchorView();
        setStatusMessage(QString::fromUtf8(error.what()));
    }
}

void MainWindow::refreshStaffNoteInspector()
{
    if (!noteInspectorStack_ || !staffNoteEditor_)
        return;
    const bool original = project_.staffImagePlayback && project_.staffPerformance.has_value();
    noteInspectorStack_->setCurrentIndex(original ? 1 : 0);
    const bool selection =
        original && selectedStaffNote_ >= 0 && selectedStaffNote_ < int(project_.staffPerformance->notes.size()) &&
        project_.staffPerformance->notes[std::size_t(selectedStaffNote_)].pageIndex == staffPageIndex_;
    if (!selection)
    {
        staffNoteEditor_->clearNote();
        staffInspectorNote_ = -1;
    }
    else
    {
        if (staffInspectorNote_ != selectedStaffNote_ || !staffNoteEditor_->hasDraft())
        {
            staffNoteEditor_->setNote(project_, project_.staffPerformance->notes[std::size_t(selectedStaffNote_)]);
            staffInspectorNote_ = selectedStaffNote_;
        }
        if (auto *title = staffNoteEditor_->findChild<QLabel *>("staffNoteTitle"))
            title->setText(trText("ui.original_playback.selected")
                               .arg(selectedStaffNote_ + 1)
                               .arg(project_.staffPerformance->notes.size())
                               .arg(project_.staffPerformance->notes[std::size_t(selectedStaffNote_)].midiPitch)
                               .arg(project_.staffPerformance->notes[std::size_t(selectedStaffNote_)].staff)
                               .arg(QString::fromStdString(
                                   project_.staffPerformance->notes[std::size_t(selectedStaffNote_)].voice)));
    }
    const bool editable = selection && canEditOriginalStaff(true);
    staffNoteEditor_->setEditingEnabled(editable);
    if (auto *remove = staffNoteEditor_->findChild<QPushButton *>("staffNoteDelete"))
        remove->setEnabled(editable && !staffNoteEditor_->hasDraft() &&
                           project_.staffPerformance->notes.size() > 1);
    inspectorScroll_->setVisible(correctionMode_ || (original && selection));
}

void MainWindow::previewStaffInspectorNote(const StaffPerformanceNote &before, const StaffPerformanceNote &note)
{
    auto preview = project_;
    preview.staffPerformance->notes[std::size_t(selectedStaffNote_)] = note;
    recordStaffVisualEdit(preview, &before, note);
    view_->setStaffPerformance(&*preview.staffPerformance);
    view_->setStaffVisualEdits(preview.processing.value("staffVisualEdits").toArray());
    view_->setSelectedStaffNote(selectedStaffNote_);
}

bool MainWindow::applyStaffInspectorNote()
{
    if (!staffNoteEditor_ || !canEditOriginalStaff(true) || selectedStaffNote_ < 0 ||
        selectedStaffNote_ >= int(project_.staffPerformance->notes.size()) ||
        project_.staffPerformance->notes[std::size_t(selectedStaffNote_)].pageIndex != staffPageIndex_)
        return false;
    if (!staffNoteEditor_->hasDraft())
        return true;
    try
    {
        const auto before = project_.staffPerformance->notes[std::size_t(selectedStaffNote_)];
        const auto note = staffNoteEditor_->note();
        auto corrected = updatedStaffNote(project_, selectedStaffNote_, note);
        const int selected = findEditedStaffNote(corrected, note);
        if (selected < 0)
            throw std::runtime_error(trText("ui.staff_correction.invalid_note").toStdString());
        recordStaffVisualEdit(corrected, &before, corrected.staffPerformance->notes[std::size_t(selected)]);
        staffNoteEditor_->discardDraft();
        applyStaffNoteEdit(std::move(corrected), selected, "ui.original_playback.note_properties");
        return true;
    }
    catch (const std::exception &error)
    {
        staffNoteEditor_->setError(QString::fromUtf8(error.what()));
        return false;
    }
}

void MainWindow::cancelStaffInspectorNote()
{
    if (!staffNoteEditor_)
        return;
    staffNoteEditor_->discardDraft();
    refreshStaffAnchorView();
}

void MainWindow::applyStaffNoteEdit(Project corrected, int selectedIndex, const char *undoKey)
{
    const auto before = materialState();
    const auto transform = view_->transform();
    const int horizontal = view_->horizontalScrollBar()->value();
    const int vertical = view_->verticalScrollBar()->value();
    playIntent_ = false;
    player_.stop();
    originalAudio_.pause();
    project_ = std::move(corrected);
    selectedStaffNote_ = selectedIndex;
    loadingNote_ = true;
    rebuild(false);
    selected_ = std::clamp(selected_, 0, int(project_.score.notes.size()) - 1);
    selectNote(selected_, false);
    loadingNote_ = false;
    refreshStaffAnchorView();
    view_->setTransform(transform);
    view_->horizontalScrollBar()->setValue(horizontal);
    view_->verticalScrollBar()->setValue(vertical);
    view_->setFocus(Qt::OtherFocusReason);
    if (selectedIndex < 0)
        noteTitle_->setText(trText("ui.original_playback.edit_help"));
    commitMaterialEdit(before, undoKey);
}
void MainWindow::refreshStaffAnchorView()
{
    const auto transform = view_->transform();
    const int horizontal = view_->horizontalScrollBar()->value();
    const int vertical = view_->verticalScrollBar()->value();
    const QImage &image = project_.staffPages.empty()
                              ? project_.image
                              : project_.staffPages[std::size_t(staffPageIndex_)].sourceImage;
    view_->setScore(image, project_.score, staffPageIndex_, true);
    view_->setStaffPerformance(project_.staffPerformance ? &*project_.staffPerformance : nullptr);
    refreshStaffAnchorEditing();
    view_->setTransform(transform);
    view_->horizontalScrollBar()->setValue(horizontal);
    view_->verticalScrollBar()->setValue(vertical);
    if (correctionMode_ && project_.staffPerformance && selectedStaffNote_ >= 0 &&
        std::size_t(selectedStaffNote_) < project_.staffPerformance->notes.size() &&
        project_.staffPerformance->notes[std::size_t(selectedStaffNote_)].pageIndex == staffPageIndex_)
        selectStaffNote(selectedStaffNote_);
}
void MainWindow::beginStaffAnchorEdit(int index)
{
    if (!canEditOriginalStaff())
        return;
    playIntent_ = false;
    player_.pause();
    originalAudio_.pause();
    selectStaffNote(index);
}
void MainWindow::moveStaffAnchor(int index, const SourceRect &anchor)
{
    if (!canEditOriginalStaff() || index < 0 || index >= int(project_.staffPerformance->notes.size()) ||
        project_.staffPerformance->notes[std::size_t(index)].pageIndex != staffPageIndex_)
        return;
    try
    {
        auto corrected = correctedStaffAnchor(project_, index, anchor);
        const auto before = materialState();
        QJsonArray visual;
        for (const auto &entry : corrected.processing.value("staffVisualEdits").toArray())
        {
            auto edit = entry.toObject();
            if (matchesStaffEdit(edit, project_.staffPerformance->notes[std::size_t(index)]))
                edit.insert("source", staffBoxJson(corrected.staffPerformance->notes[std::size_t(index)].source));
            visual.append(edit);
        }
        if (corrected.processing.contains("staffVisualEdits"))
            corrected.processing.insert("staffVisualEdits", visual);
        project_ = std::move(corrected);
        selectedStaffNote_ = index;
        refreshStaffAnchorView();
        commitMaterialEdit(before, "ui.original_playback.move_anchor", true);
        setStatus("ui.original_playback.moved");
    }
    catch (const std::exception &error)
    {
        refreshStaffAnchorView();
        setStatusMessage(QString::fromUtf8(error.what()));
    }
}
void MainWindow::openStaffCorrection()
{
    if (!project_.staffPerformance || !project_.processing.value("local").toBool() || busy_ || audioLoading_ ||
        notePreviewLoading_ || classroom_ || localStaffTask_.isRunning() ||
        localStaffTask_.state() == LocalStaffRecognitionTask::State::Ready || !resolveNoteDraft())
        return;
    player_.pause();
    try
    {
        StaffCorrectionDialog dialog(project_, this);
        if (dialog.exec() == QDialog::Accepted && dialog.correctedProject())
            applyStaffCorrection(*dialog.correctedProject());
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}
void MainWindow::applyStaffCorrection(Project corrected)
{
    if (!corrected.staffPerformance || !corrected.processing.value("local").toBool())
        throw std::runtime_error(trText("ui.staff.original_parts").toStdString());
    projectToJson(corrected);
    const auto before = materialState();
    project_ = std::move(corrected);
    player_.stop();
    selectedStaffNote_ = -1;
    rebuild(false);
    selected_ = project_.score.notes.empty() ? -1 : 0;
    selectNote(selected_, false);
    commitMaterialEdit(before, "ui.staff_correction.open");
    setStatus("ui.fidelity.corrected");
}
void MainWindow::confirmStaffTempo()
{
    if (!project_.staffPerformance || busy_ || audioLoading_ || notePreviewLoading_ || !resolveNoteDraft())
        return;
    bool confirmed = false;
    const auto bpm =
        QInputDialog::getDouble(this, trText("ui.fidelity.confirm_tempo"), trText("ui.fidelity.tempo_review"),
                                project_.score.bpm, MinimumScoreBpm, MaximumScoreBpm, 1, &confirmed);
    if (!confirmed)
        return;
    project_.score.bpm = bpm;
    project_.processing.insert("tempoNeedsConfirmation", false);
    project_.processing.insert("tempoSource", "user-confirmed");
    project_.processing.insert("confirmedQuarterBpm", bpm);
    markModified();
    rebuild(true);
    updatePlayback();
}
void MainWindow::refreshNotationControls()
{
    if (!notationStyle_)
        return;
    subtitle_->setText(project_.staffPerformance
                           ? trText("ui.staff.full_subtitle").arg(project_.staffPerformance->notes.size())
                           : trText("ui.subtitle.score").arg(project_.score.notes.size()));
    const QSignalBlocker blocker(notationStyle_);
    const bool fullScore = project_.staffPerformance.has_value();
    refreshStaffAnchorEditing();
    if (auto *action = findChild<QAction *>("menuStaffCorrection"))
        action->setEnabled(fullScore && project_.processing.value("local").toBool());
    if (auto *action = findChild<QAction *>("menuConfirmStaffTempo"))
        action->setEnabled(fullScore);
    if (playbackSource_)
    {
        const int synthesized = playbackSource_->findData(0);
        const char *sourceKey = fullScore ? "ui.staff.piano_source" : "ui.audio_import.synthesized";
        playbackSource_->setItemData(synthesized, QByteArray(sourceKey), Qt::UserRole + 1);
        playbackSource_->setItemText(synthesized, trText(sourceKey));
    }
    key_->setEnabled(!fullScore);
    meterTop_->setEnabled(!fullScore);
    meterBottom_->setEnabled(!fullScore);
    for (QWidget *field : std::initializer_list<QWidget *>{velocity_, accentBeats_})
        field->setEnabled(!fullScore);
    programA_->setEnabled(true);
    programB_->setEnabled(true);
    for (QWidget *field : std::initializer_list<QWidget *>{accentBeats_})
        field->setToolTip(fullScore ? trText("ui.staff.preserved_performance") : QString{});
    for (QWidget *field : std::initializer_list<QWidget *>{programA_, programB_})
        field->setToolTip(fullScore ? trText("ui.staff.instrument_tip") : QString{});
    if (auto *label = findChild<QLabel *>("programALabel"))
        bindText(label, fullScore ? "ui.staff.primary_instrument" : "ui.transport.program_a");
    if (auto *label = findChild<QLabel *>("programBLabel"))
        bindText(label, fullScore ? "ui.staff.other_instrument" : "ui.transport.program_b");
    velocity_->setToolTip(trText(fullScore ? "ui.staff.preserved_performance" : "ui.option.velocity_tip"));
    if (fullScore)
    {
        const QSignalBlocker programABlock(programA_), programBBlock(programB_);
        for (auto *combo : {programA_, programB_})
        {
            for (int program = 0; program < 128; ++program)
                if (combo->findData(program) < 0)
                    addTranslatedItem(combo, "ui.instrument.gm", program, {QString::number(program + 1)});
            const int program = combo == programA_ ? project_.staffPerformance->primaryProgram
                                                   : project_.staffPerformance->otherProgram;
            if (combo->findData(program) < 0)
                addTranslatedItem(combo, "ui.instrument.gm", program, {QString::number(program + 1)});
            combo->setCurrentIndex(combo->findData(program));
        }
    }
    else
    {
        for (auto *combo : {programA_, programB_})
        {
            const QSignalBlocker programBlock(combo);
            const int selectedProgram = programForVerse(project_.score, combo == programA_ ? 0 : 1);
            for (int index = combo->count() - 1; index >= 0; --index)
            {
                const int program = combo->itemData(index).toInt();
                if (program != selectedProgram && !instrumentNameKey(program))
                    combo->removeItem(index);
            }
        }
    }
    if (auto *repeats = findChild<QAction *>("menuRepeats"))
        repeats->setEnabled(!fullScore);
    notationStyle_->setEnabled(project_.generatedNotation);
    notationStyle_->setCurrentIndex(project_.generatedNotation
                                        ? notationStyle_->findData(int(project_.notationStyle))
                                        : notationStyle_->findData(-1));
    if (auto *model = qobject_cast<QStandardItemModel *>(notationStyle_->model()))
    {
        model->item(0)->setEnabled(!project_.generatedNotation);
        model->item(1)->setEnabled(!project_.staffPerformance);
    }
    if (auto *action = findChild<QAction *>("menuNumberedView"))
        action->setEnabled(project_.generatedNotation && !project_.staffPerformance);
    if (auto *action = findChild<QAction *>("menuStaffView"))
        action->setEnabled(project_.generatedNotation);
    notationStyle_->setToolTip(trText(project_.generatedNotation ? "ui.staff.view_tip" : "ui.staff.original_tip"));
}

void MainWindow::setNotationStyle(NotationStyle style)
{
    if (project_.staffImagePlayback || !project_.generatedNotation || busy_ || audioLoading_ ||
        notePreviewLoading_ || !resolveNoteDraft())
    {
        refreshNotationControls();
        return;
    }
    if (project_.notationStyle == style)
        return;
    if (project_.staffPerformance && style == NotationStyle::Numbered)
    {
        setStatus("ui.staff.original_parts");
        refreshNotationControls();
        return;
    }
    try
    {
        auto score = project_.score;
        QImage image =
            style == NotationStyle::Staff
                ? renderStaffScore(score, {project_.staffBassClef, project_.staffKeyFifths, project_.staffMinor})
                : renderNumberedScore(score);
        project_.score = std::move(score);
        project_.image = std::move(image);
        project_.notationStyle = style;
        markModified();
        rebuild(true);
        view_->fitWidth();
        refreshNotationControls();
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
        refreshNotationControls();
    }
}

void MainWindow::importMusicXml(const QString &path, int trackIndex)
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning() || !confirmDiscard())
        return;
    try
    {
        const auto imported = singlilt::importMusicXml(path);
        if (!imported.valid())
            throw std::runtime_error(imported.error.toStdString());
        if (trackIndex < 0 && imported.tracks.size() > 1)
        {
            QStringList choices;
            for (const auto &track : imported.tracks)
                choices.append(trText("ui.staff.track_label")
                                   .arg(track.partName + " [" + track.partId + ']')
                                   .arg(track.staff)
                                   .arg(track.voice));
            bool accepted = false;
            const QString selected =
                QInputDialog::getItem(this, trText("ui.staff.select_track"), trText("ui.staff.track_help"),
                                      choices, 0, false, &accepted);
            if (!accepted)
                return;
            trackIndex = choices.indexOf(selected);
        }
        if (trackIndex < 0)
            trackIndex = 0;
        const auto converted = musicXmlToPerformance(imported, static_cast<std::size_t>(trackIndex));
        if (!converted.valid())
            throw std::runtime_error(converted.error.toStdString());
        const auto &track = imported.tracks.at(static_cast<std::size_t>(trackIndex));
        Project candidate;
        candidate.score = *converted.selectedMelody.score;
        candidate.generatedNotation = true;
        candidate.notationStyle = NotationStyle::Staff;
        candidate.warnings = imported.warnings + converted.warnings;
        candidate.staffKeyFifths = converted.selectedMelody.keyFifths;
        candidate.staffMinor = converted.selectedMelody.minorKey;
        candidate.staffBassClef = converted.selectedMelody.clef.sign == "F";
        candidate.staffPerformance = converted.performance;
        candidate.practiceMix.accompanimentEnabled = candidate.staffPerformance->staffCount > 1;
        const bool multiplePages =
            std::any_of(candidate.score.writtenMeasures.begin(), candidate.score.writtenMeasures.end(),
                        [](const WrittenMeasure &measure) { return measure.pageIndex > 0; });
        if (multiplePages)
        {
            auto images =
                renderGrandStaffPages(candidate.score, *candidate.staffPerformance,
                                      {candidate.staffBassClef, candidate.staffKeyFifths, candidate.staffMinor});
            for (std::size_t page = 0; page < images.size(); ++page)
            {
                const auto first =
                    std::find_if(candidate.score.writtenMeasures.begin(), candidate.score.writtenMeasures.end(),
                                 [page](const WrittenMeasure &measure) { return measure.pageIndex == int(page); });
                const auto last =
                    std::find_if(candidate.score.writtenMeasures.rbegin(), candidate.score.writtenMeasures.rend(),
                                 [page](const WrittenMeasure &measure) { return measure.pageIndex == int(page); });
                if (first == candidate.score.writtenMeasures.end() ||
                    last == candidate.score.writtenMeasures.rend())
                    throw std::runtime_error(trText("messages.pages.invalid_project").toStdString());
                candidate.staffPages.push_back({trText("ui.pages.page_number").arg(page + 1).arg(images.size()),
                                                {},
                                                std::move(images[page]),
                                                first->startTick,
                                                last->startTick + last->durationTicks});
            }
            candidate.image = candidate.staffPages.front().renderedImage;
        }
        else
            candidate.image =
                renderGrandStaffScore(candidate.score, *candidate.staffPerformance,
                                      {candidate.staffBassClef, candidate.staffKeyFifths, candidate.staffMinor});
        candidate.processing.insert("musicXmlSourceName", QFileInfo(path).fileName());
        candidate.processing.insert("musicXmlSourceSHA256", QString::fromLatin1(imported.sourceSha256));
        candidate.processing.insert("musicXmlPart", track.partId);
        candidate.processing.insert("musicXmlStaff", track.staff);
        candidate.processing.insert("musicXmlVoice", track.voice);
        projectToJson(candidate);
        setProject(std::move(candidate), true);
        setStatus("ui.staff.imported", {QString::number(project_.staffPerformance->notes.size())});
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}

void MainWindow::importStaffImage(const QString &path)
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning() || cloudTask_.isRunning() ||
        cloudTask_.state() == CloudRecognitionTask::State::Ready || !resolveNoteDraft())
        return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (size.width() > 12000 || size.height() > 20000 || qint64(size.width()) * size.height() > 50000000)
    {
        showError("ui.error.image_large");
        return;
    }
    const auto image = reader.read();
    if (image.isNull())
    {
        showError(reader.errorString());
        return;
    }
    if (splitStaffPageInputs(image, QFileInfo(path).fileName(), 0).size() > 1)
        importStaffImages({path});
    else
        startLocalStaffRecognition(image, path);
}
} // namespace singlilt

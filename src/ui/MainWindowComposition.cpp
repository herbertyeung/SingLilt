// Authored voice transactions, selection, and notation-mark changes.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#include "MainWindow.h"
#include "NumberedVoicePanel.h"
#include "ScoreView.h"
#include "application/NumberedCompositionEditing.h"
#include <stdexcept>
#include "i18n/LanguageManager.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <algorithm>

namespace singlilt
{
NumberedComposition MainWindow::currentNumberedComposition() const
{
    if (project_.numberedComposition)
        return *project_.numberedComposition;
    return {{{1, trText("ui.editor.voice_name").arg(1).toStdString(), project_.score.notes}}, 2};
}
bool MainWindow::commitNumberedComposition(Score score, NumberedComposition composition, int active, int selection,
                                           const char *key)
{
    if ((!numberedEditorActive() && !project_.numberedComposition) || busy_ || audioLoading_ ||
        notePreviewLoading_)
        return false;
    try
    {
        if (project_.numberedComposition && score == project_.score &&
            composition == *project_.numberedComposition)
            return true;
        if (!composition.structure.measures.empty())
        {
            compositionTimeline(score, composition);
            compositionPlaybackPlan(score, composition);
        }
        NumberedScoreLayout layout;
        auto image = renderNumberedComposition(score, composition, active, layout);
        auto before = materialState();
        before.numberedScore = project_.score;
        project_.score = std::move(score);
        project_.numberedComposition = std::move(composition);
        project_.image = std::move(image);
        numberedLayout_ = std::move(layout);
        if (activeNumberedVoice_ != active)
        {
            numberedInputTick_ = 0;
            inputDynamic_ = 0;
            inputAccent_ = inputStaccato_ = false;
        }
        activeNumberedVoice_ = active;
        selected_ = selection;
        commitMaterialEdit(before, key);
        rebuild(true, false);
        loadingNote_ = true;
        selectNote(selected_, false);
        loadingNote_ = false;
        setStatus("ui.editor.voice_ready", {QString::number(project_.numberedComposition->voices.size())});
        return true;
    }
    catch (const std::exception &error)
    {
        setStatusMessage(QString::fromUtf8(error.what()));
        refreshNumberedEditor();
        return false;
    }
}
bool MainWindow::selectNumberedVoice(int id)
{
    if (!numberedEditorActive() || busy_ || audioLoading_ || notePreviewLoading_ || !resolveNoteDraft())
    {
        refreshNumberedEditor();
        return false;
    }
    if (!project_.numberedComposition || id == activeNumberedVoice_)
        return id == activeNumberedVoice_;
    try
    {
        auto guide = project_.score;
        guide.notes = numberedVoice(*project_.numberedComposition, id).notes;
        auto timeline = compositionTimeline(guide, *project_.numberedComposition);
        project_.score = std::move(guide);
        timeline_ = std::move(timeline);
        activeNumberedVoice_ = id;
        inputDynamic_ = 0;
        inputAccent_ = inputStaccato_ = false;
        selected_ = -1;
        numberedInputTick_ = 0;
        playingNote_ = playingVerse_ = -1;
        // Voice selection changes the guide and overlays, not the shared sounding plan or raster.
        view_->setScore(project_.image, project_.score);
        selectNumberedInput(0);
        setStatus("ui.editor.voice_ready", {QString::number(project_.numberedComposition->voices.size())});
        return true;
    }
    catch (const std::exception &error)
    {
        setStatusMessage(QString::fromUtf8(error.what()));
        refreshNumberedEditor();
        return false;
    }
}
void MainWindow::numberedVoiceAction(VoiceAction action)
{
    if (!numberedEditorActive() || !resolveNoteDraft())
        return;
    try
    {
        auto composition = currentNumberedComposition();
        int active = activeNumberedVoice_;
        if (action == VoiceAction::Add)
            active = addNumberedVoice(composition,
                                      trText("ui.editor.voice_name").arg(composition.nextId).toStdString());
        else if (action == VoiceAction::Rename)
        {
            auto &voice = numberedVoice(composition, active);
            bool accepted = false;
            const auto name =
                QInputDialog::getText(this, trText("ui.editor.voice_rename"), trText("ui.editor.voice_label"),
                                      QLineEdit::Normal, QString::fromStdString(voice.name), &accepted)
                    .trimmed();
            if (!accepted || name.isEmpty())
                return;
            if (name.size() > 64)
                throw std::invalid_argument("ui.editor.invalid_composition");
            voice.name = name.toStdString();
        }
        else if (action == VoiceAction::Remove)
        {
            if (!numberedVoice(composition, active).notes.empty() &&
                QMessageBox::question(
                    this, trText("ui.editor.voice_remove"), trText("ui.editor.voice_remove_confirm"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
                return;
            removeNumberedVoice(composition, active);
            active = composition.voices.front().id;
        }
        else
            moveNumberedVoice(composition, active, action == VoiceAction::Up ? -1 : 1);
        auto score = project_.score;
        score.notes = numberedVoice(composition, active).notes;
        const int selection = action == VoiceAction::Add || action == VoiceAction::Remove ? -1 : selected_;
        commitNumberedComposition(std::move(score), std::move(composition), active, selection, "ui.editor.voices");
    }
    catch (const std::exception &error)
    {
        setStatusMessage(QString::fromUtf8(error.what()));
    }
}
void MainWindow::setNumberedDynamic(int level)
{
    if (!numberedEditorActive() || !resolveNoteDraft())
    {
        refreshNumberedEditor();
        return;
    }
    inputDynamic_ = level;
    if (selected_ < 0)
        return;
    auto score = project_.score;
    score.notes[std::size_t(selected_)].dynamicLevel = level;
    commitNumberedScore(std::move(score), "ui.editor.marks", selected_, false);
}
} // namespace singlilt

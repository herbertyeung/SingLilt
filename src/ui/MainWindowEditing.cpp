// Score edits, draft handling, and undo/redo state.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AccompanimentPanel.h"
#include "MainWindow.h"
#include "MenuIcons.h"
#include "ScoreView.h"
#include "StaffNoteEditor.h"
#include "i18n/LanguageManager.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextBoundaryFinder>
#include <QTimer>
#include <QUndoCommand>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace singlilt
{
namespace
{
class MaterialEdit final : public QUndoCommand
{
  public:
    MaterialEdit(const QString &text, std::function<void()> undo, std::function<void()> redo)
        : QUndoCommand(text), undo_(std::move(undo)), redo_(std::move(redo))
    {
    }
    void undo() override
    {
        undo_();
    }
    void redo() override
    {
        // The material was already applied by the editor before push().
        if (firstRedo_)
            firstRedo_ = false;
        else
            redo_();
    }

  private:
    std::function<void()> undo_, redo_;
    bool firstRedo_ = true;
};
} // namespace

MainWindow::MaterialState MainWindow::materialState() const
{
    MaterialState state{project_.score.notes,
                        project_.score.repeats,
                        project_.warnings,
                        project_.audioSource ? project_.audioSource->lyricTimings
                                             : std::vector<AudioLyricTiming>{},
                        selected_,
                        loopStart_,
                        loopEnd_,
                        loopEditRevision_};
    if (project_.staffPerformance)
        state.staffProject = project_;
    state.selectedStaffNote = selectedStaffNote_;
    return state;
}
void MainWindow::commitMaterialEdit(const MaterialState &before, const char *key, bool anchorOnly)
{
    const auto after = materialState();
    materialUndo_.push(new MaterialEdit(
        trText(key),
        [this, before, anchorOnly]
        {
            if (anchorOnly)
                restoreStaffAnchorState(before);
            else
                restoreMaterialState(before);
        },
        [this, after, anchorOnly]
        {
            if (anchorOnly)
                restoreStaffAnchorState(after);
            else
                restoreMaterialState(after);
        }));
    refreshProjectIdentity();
}
void MainWindow::restoreStaffAnchorState(const MaterialState &state)
{
    if (!state.staffProject || !state.staffProject->staffPerformance || !project_.staffPerformance ||
        state.notes.size() != project_.score.notes.size() ||
        state.staffProject->staffPerformance->notes.size() != project_.staffPerformance->notes.size())
    {
        restoreMaterialState(state);
        return;
    }
    // Anchor undo leaves later instrument, tempo and playback choices intact.
    for (std::size_t index = 0; index < state.notes.size(); ++index)
    {
        project_.score.notes[index].source = state.notes[index].source;
        project_.score.notes[index].hasImageAnchor = state.notes[index].hasImageAnchor;
    }
    for (std::size_t index = 0; index < project_.staffPerformance->notes.size(); ++index)
    {
        const auto &stored = state.staffProject->staffPerformance->notes[index];
        project_.staffPerformance->notes[index].source = stored.source;
        project_.staffPerformance->notes[index].hasImageAnchor = stored.hasImageAnchor;
    }
    for (const auto *key : {"manualImageAnchors", "staffVisualEdits"})
        if (state.staffProject->processing.contains(key))
            project_.processing.insert(key, state.staffProject->processing.value(key));
        else
            project_.processing.remove(key);
    selectedStaffNote_ = state.selectedStaffNote;
    refreshStaffAnchorView();
    refreshProjectIdentity();
}
void MainWindow::restoreMaterialState(const MaterialState &state)
{
    if (accompanimentPanel_)
        accompanimentPanel_->reject();
    if (state.staffProject)
    {
        auto restored = *state.staffProject;
        // A later instrument/tempo/mix choice is not part of the note correction.
        restored.score.bpm = project_.score.bpm;
        restored.practiceMix = project_.practiceMix;
        restored.practiceSettings = project_.practiceSettings;
        if (restored.staffPerformance && project_.staffPerformance)
        {
            restored.staffPerformance->primaryProgram = project_.staffPerformance->primaryProgram;
            restored.staffPerformance->otherProgram = project_.staffPerformance->otherProgram;
        }
        for (const auto *key : {"tempoNeedsConfirmation", "tempoSource", "confirmedQuarterBpm"})
            if (project_.processing.contains(key))
                restored.processing.insert(key, project_.processing.value(key));
            else
                restored.processing.remove(key);
        project_ = std::move(restored);
    }
    project_.score.notes = state.notes;
    project_.score.repeats = state.repeats;
    project_.warnings = state.warnings;
    if (project_.audioSource)
        project_.audioSource->lyricTimings = state.lyricTimings;
    selected_ = state.selected;
    selectedStaffNote_ = state.selectedStaffNote;
    // Automatic clamping belongs to the material edit; later explicit loop edits do not.
    if (state.loopRevision == loopEditRevision_)
    {
        loopStart_ = state.loopStart;
        loopEnd_ = state.loopEnd;
    }
    loadingNote_ = true;
    rebuild(true);
    selectNote(selected_, false);
    loadingNote_ = false;
    refreshProjectIdentity();
}
void MainWindow::undoMaterialEdit()
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || !resolveNoteDraft())
        return;
    materialUndo_.undo();
}
void MainWindow::redoMaterialEdit()
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || !resolveNoteDraft())
        return;
    materialUndo_.redo();
}
bool MainWindow::hasNoteDraft() const
{
    if (project_.staffImagePlayback && project_.staffPerformance)
        return !loadingNote_ && staffNoteEditor_ && staffNoteEditor_->hasDraft();
    if (loadingNote_ || selected_ < 0 || selected_ >= int(project_.score.notes.size()))
        return false;
    const auto &note = project_.score.notes[size_t(selected_)];
    const auto verses = lyricVerses(note);
    const QString a = verses.empty() ? QString() : QString::fromStdString(verses[0]);
    const QString b = verses.size() > 1 ? QString::fromStdString(verses[1]) : QString();
    return degree_->currentData().toInt() != note.degree || octave_->value() != note.octave ||
           accidental_->value() != note.accidental ||
           int(std::lround(duration_->value() * 480)) != note.durationTicks ||
           noteKey_->currentData().toInt() != note.keyOverride || lyricEdit_->text() != a ||
           (!sharedLyric_->isChecked() && lyricBEdit_->text() != b) ||
           sharedLyric_->isChecked() != (verses.size() <= 1) || tie_->isChecked() != note.tieToNext;
}
bool MainWindow::resolveNoteDraft()
{
    if (!hasNoteDraft())
        return true;
    QMessageBox dialog(QMessageBox::Question, trText("ui.product.draft_title"), trText("ui.product.draft_message"),
                       QMessageBox::Apply | QMessageBox::Discard | QMessageBox::Cancel, this);
    dialog.setObjectName("noteDraftConfirmation");
    dialog.setDefaultButton(QMessageBox::Apply);
    bindText(dialog.button(QMessageBox::Apply), "ui.inspector.apply");
    bindText(dialog.button(QMessageBox::Discard), "ui.button.discard");
    bindText(dialog.button(QMessageBox::Cancel), "ui.button.cancel");
    const int choice = dialog.exec();
    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Apply)
    {
        if (project_.staffImagePlayback && project_.staffPerformance)
            return applyStaffInspectorNote();
        updateNote();
        return !hasNoteDraft();
    }
    else
    {
        if (project_.staffImagePlayback && project_.staffPerformance)
        {
            cancelStaffInspectorNote();
            return true;
        }
        loadingNote_ = true;
        selectNote(selected_, false);
        loadingNote_ = false;
    }
    return true;
}
void MainWindow::removeNote()
{
    if (project_.staffPerformance)
    {
        setStatus("ui.staff.guide_only");
        return;
    }
    if (selected_ < 0 || !resolveNoteDraft())
        return;
    if (project_.score.notes.size() == 1)
    {
        showError(QStringLiteral("ui.error.last_note"));
        return;
    }
    const auto before = materialState();
    const size_t removed = size_t(selected_);
    int affected = 0;
    auto &repeats = project_.score.repeats;
    for (auto it = repeats.begin(); it != repeats.end();)
    {
        const bool contains = removed >= it->firstNote && removed < it->endNote;
        affected += contains;
        if (removed < it->firstNote)
            --it->firstNote;
        if (removed < it->endNote)
            --it->endNote;
        if (it->firstEndingNote >= 0 && removed < size_t(it->firstEndingNote))
            --it->firstEndingNote;
        if (it->firstEndingNote >= 0 &&
            (size_t(it->firstEndingNote) <= it->firstNote || size_t(it->firstEndingNote) >= it->endNote))
            it->firstEndingNote = -1;
        if (it->firstNote >= it->endNote)
            it = repeats.erase(it);
        else
            ++it;
    }
    project_.score.notes.erase(project_.score.notes.begin() + selected_);
    selected_ = std::min(selected_, int(project_.score.notes.size()) - 1);
    commitMaterialEdit(before, "ui.product.remove_note");
    loadingNote_ = true;
    rebuild(true);
    selectNote(selected_, false);
    loadingNote_ = false;
    if (affected)
        setStatus("ui.product.repeat_adjusted", {QString::number(affected)});
}
void MainWindow::insertNote(QPointF position)
{
    if (project_.staffPerformance)
    {
        setStatus("ui.staff.guide_only");
        return;
    }
    if (!correctionMode_ || !resolveNoteDraft() || position.x() < 0 || position.y() < 0 ||
        position.x() + 18 > project_.image.width() || position.y() + 24 > project_.image.height())
        return;
    const auto before = materialState();
    Note note;
    note.source = {position.x(), position.y(), 18, 24};
    note.confidence = .5;
    const int index = selected_ < 0 ? int(project_.score.notes.size()) : selected_ + 1;
    note.id = int(project_.score.notes.size());
    if (selected_ >= 0)
    {
        note.line = project_.score.notes[size_t(selected_)].line;
        note.measure = project_.score.notes[size_t(selected_)].measure;
    }
    project_.score.notes.insert(project_.score.notes.begin() + index, note);
    for (auto &repeat : project_.score.repeats)
    {
        if (repeat.firstNote >= size_t(index))
            ++repeat.firstNote;
        if (repeat.endNote > size_t(index))
            ++repeat.endNote;
        if (repeat.firstEndingNote >= index)
            ++repeat.firstEndingNote;
    }
    selected_ = index;
    commitMaterialEdit(before, "ui.product.add_note");
    loadingNote_ = true;
    rebuild(true);
    selectNote(index, false);
    loadingNote_ = false;
}
void MainWindow::setCorrectionMode(bool correction)
{
    if (correctionMode_ && !correction && !resolveNoteDraft())
    {
        correctionModeButton_->setChecked(true);
        return;
    }
    correctionMode_ = correction;
    if (correction && project_.staffImagePlayback)
    {
        playIntent_ = false;
        player_.pause();
        originalAudio_.pause();
    }
    refreshStaffAnchorEditing();
    if (correction && project_.staffImagePlayback)
        setStatus("ui.original_playback.drag_help");
    inspectorScroll_->setVisible(correction || (project_.staffImagePlayback && selectedStaffNote_ >= 0));
    if (correction)
        correctionModeButton_->setChecked(true);
    else
        practiceModeButton_->setChecked(true);
    QTimer::singleShot(0, view_, [this] { view_->fitWidth(); });
}
void MainWindow::refreshReviewStatus()
{
    const auto count = std::count_if(project_.score.notes.begin(), project_.score.notes.end(),
                                     [](const Note &note) { return note.confidence < .75; });
    if (reviewStatus_)
        reviewStatus_->setText(trText("ui.product.review_count").arg(count));
    if (auto *next = findChild<QPushButton *>("nextUncertainNote"))
        next->setEnabled(count > 0);
}
void MainWindow::nextUncertainNote()
{
    if (!resolveNoteDraft())
        return;
    setCorrectionMode(true);
    const int count = int(project_.score.notes.size());
    for (int offset = 1; offset <= count; ++offset)
    {
        const int index = (std::max(-1, selected_) + offset) % count;
        if (project_.score.notes[size_t(index)].confidence < .75)
        {
            selectNote(index, false);
            view_->setCurrent(index, true);
            return;
        }
    }
}
void MainWindow::markModified()
{
    if (loading_)
        return;
    nonMaterialDirty_ = true;
    refreshProjectIdentity();
}
void MainWindow::refreshProjectIdentity()
{
    dirty_ = nonMaterialDirty_ || !materialUndo_.isClean();
    const QString name =
        projectPath_.isEmpty() ? trText("ui.product.unsaved_project") : QFileInfo(projectPath_).fileName();
    const QString identity = name + (dirty_ ? QStringLiteral(" *") : QString());
    if (projectIdentity_)
    {
        projectIdentity_->setText(identity);
        projectIdentity_->setToolTip(projectPath_.isEmpty() ? trText("ui.product.unsaved_project") : projectPath_);
    }
    setWindowTitle(trText("ui.window.title").arg(QApplication::applicationVersion()) + " — " + name + "[*]");
    setWindowModified(dirty_);
    if (undoAction_)
        undoAction_->setEnabled(materialUndo_.canUndo());
    if (redoAction_)
        redoAction_->setEnabled(materialUndo_.canRedo());
}
void MainWindow::rememberProject(const QString &path)
{
    QSettings settings;
    auto paths = settings.value("projects/recent").toStringList();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    paths.erase(std::remove_if(paths.begin(), paths.end(), [&absolute](const QString &item)
                               { return item.compare(absolute, Qt::CaseInsensitive) == 0; }),
                paths.end());
    paths.prepend(absolute);
    while (paths.size() > 8)
        paths.removeLast();
    settings.setValue("projects/recent", paths);
    refreshRecentProjects();
}
void MainWindow::refreshRecentProjects()
{
    if (!recentProjects_)
        return;
    recentProjects_->clear();
    const auto paths = QSettings().value("projects/recent").toStringList();
    for (const auto &path : paths)
    {
        auto *action = recentProjects_->addAction(menuIcon(MenuIcon::Score), QFileInfo(path).fileName());
        action->setIconVisibleInMenu(true);
        action->setData(path);
        action->setToolTip(path);
        connect(action, &QAction::triggered, this,
                [this, path]
                {
                    if (QFileInfo::exists(path))
                        openFile(path);
                    else
                    {
                        auto settings = QSettings();
                        auto paths = settings.value("projects/recent").toStringList();
                        paths.removeAll(path);
                        settings.setValue("projects/recent", paths);
                        refreshRecentProjects();
                        setStatus("ui.product.recent_missing", {path});
                    }
                });
    }
    if (paths.isEmpty())
    {
        auto *empty = recentProjects_->addAction(menuIcon(MenuIcon::Recent), trText("ui.product.recent_empty"));
        empty->setIconVisibleInMenu(true);
        empty->setEnabled(false);
    }
}

void MainWindow::editLyrics()
{
    if (project_.score.notes.empty() || busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning() ||
        !resolveNoteDraft())
        return;
    QDialog dialog(this);
    dialog.setObjectName("songLyricsDialog");
    dialog.setWindowTitle(trText("ui.audio_import.edit_lyrics"));
    dialog.resize(680, 500);
    QVBoxLayout layout(&dialog);
    auto *help = label("ui.audio_import.lyrics_help");
    help->setWordWrap(true);
    layout.addWidget(help);
    QPlainTextEdit text;
    text.setObjectName("songLyricsText");
    layout.addWidget(&text);
    auto *load = new QPushButton(trText("ui.audio_import.load_lyrics"));
    layout.addWidget(load);
    connect(load, &QPushButton::clicked, &dialog,
            [&]
            {
                const auto path = QFileDialog::getOpenFileName(&dialog, trText("ui.audio_import.load_lyrics"), {},
                                                               "Lyrics (*.txt *.lrc)");
                QFile file(path);
                if (file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024)
                    text.setPlainText(QString::fromUtf8(file.readAll()));
            });
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString input = text.toPlainText();
    if (input.trimmed().isEmpty())
        return;
    const auto before = materialState();
    QRegularExpression stamps(R"(\[(\d+):(\d+(?:\.\d+)?)\])");
    std::vector<std::pair<double, QString>> lines;
    for (const auto &line : input.split('\n'))
    {
        auto matches = stamps.globalMatch(line);
        while (matches.hasNext())
        {
            const auto match = matches.next();
            QString words = line;
            words.remove(stamps);
            lines.emplace_back(match.captured(1).toDouble() * 60 + match.captured(2).toDouble(), words.trimmed());
        }
    }
    std::sort(lines.begin(), lines.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (auto &note : project_.score.notes)
    {
        note.lyric.clear();
        note.verseLyrics = {""};
    }
    auto fill = [this](const QString &words, const std::vector<std::size_t> &notes)
    {
        QStringList syllables;
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, words);
        int previous = 0;
        while (finder.toNextBoundary() != -1)
        {
            const auto word = words.mid(previous, finder.position() - previous);
            previous = finder.position();
            if (word == "_" || word == "~")
                syllables.append(QString());
            else if (!word.trimmed().isEmpty() && !word.front().isPunct())
                syllables.append(word);
        }
        for (int i = 0; i < syllables.size() && i < int(notes.size()); ++i)
        {
            auto &note = project_.score.notes[notes[size_t(i)]];
            note.lyric = syllables[i].toStdString();
            note.verseLyrics = {note.lyric};
        }
        if (syllables.size() > int(notes.size()))
            project_.warnings.append(trText("ui.audio_import.lyrics_overflow"));
    };
    if (!lines.empty() && project_.audioSource)
    {
        for (size_t i = 0; i < lines.size(); ++i)
        {
            std::vector<std::size_t> notes;
            const double end =
                i + 1 < lines.size() ? lines[i + 1].first : project_.audioSource->selectedEndSeconds;
            for (const auto &timing : project_.audioSource->timings)
                if (timing.startSeconds >= lines[i].first && timing.startSeconds < end &&
                    timing.sourceNoteIndex >= 0 && timing.sourceNoteIndex < int(project_.score.notes.size()) &&
                    project_.score.notes[size_t(timing.sourceNoteIndex)].degree != 0)
                    notes.push_back(size_t(timing.sourceNoteIndex));
            fill(lines[i].second, notes);
        }
    }
    else
    {
        std::vector<std::size_t> notes;
        for (size_t i = 0; i < project_.score.notes.size(); ++i)
            if (project_.score.notes[i].degree != 0 && !(i > 0 && project_.score.notes[i - 1].tieToNext))
                notes.push_back(i);
        QString words = input;
        words.remove(stamps);
        fill(words, notes);
        project_.warnings.append(trText("ui.audio_import.lyrics_provisional"));
    }
    if (project_.audioSource)
        project_.audioSource->lyricTimings.clear();
    commitMaterialEdit(before, "ui.product.edit_lyrics");
    rebuild(true);
    selectNote(selected_, false);
}
} // namespace singlilt

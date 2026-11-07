// Numbered-score authoring actions and workspace coordination.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MainWindow.h"
#include "WorkspaceHeader.h"
#include "NumberedEditorToolbar.h"
#include "NumberedVoicePanel.h"
#include "application/NumberedCompositionEditing.h"
#include "PianoKeyboard.h"
#include "ScorePropertiesPanel.h"
#include "ScoreView.h"
#include "domain/NumberedEditing.h"
#include "i18n/LanguageManager.h"
#include <QCheckBox>
#include <QButtonGroup>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBoundaryFinder>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace singlilt
{
bool MainWindow::numberedEditorActive() const
{
    return correctionMode_ && project_.generatedNotation && project_.score.editorMinimumBars > 0 &&
           !project_.staffPerformance && !project_.audioSource &&
           project_.notationStyle == NotationStyle::Numbered;
}

void MainWindow::applyAuthoredMetadataControls()
{
    const int key = key_->currentData().toInt();
    const int beats = meterTop_->value();
    const int unit = meterBottom_->currentData().toInt();
    const double tempo = tempo_->value();
    if (!resolveNoteDraft())
    {
        refreshNumberedEditor();
        return;
    }
    auto score = project_.score;
    score.tonic = key;
    score.beatsPerBar = beats;
    score.beatUnit = unit;
    score.bpm = tempo;
    commitNumberedScore(std::move(score), "ui.editor.properties", selected_, false);
}

void MainWindow::createNumberedEditor(QVBoxLayout *layout)
{
    editorToolbar_ = new NumberedEditorToolbar;
    layout->addWidget(editorToolbar_);
    editorToolbar_->hide();
    editorToolbar_->voicePanel()->selected = [this](int id) { selectNumberedVoice(id); };
    editorToolbar_->voicePanel()->requested = [this](VoiceAction action) { numberedVoiceAction(action); };
    editorToolbar_->dynamicRequested = [this](int level) { setNumberedDynamic(level); };
    view_->numberedVoiceNoteSelected = [this](int voice, int note)
    {
        if (selectNumberedVoice(voice))
            selectNote(note, false);
    };
    view_->numberedVoiceInputSelected = [this](int voice, std::int64_t tick)
    {
        if (selectNumberedVoice(voice))
            selectNumberedInput(tick);
    };
    editorToolbar_->actionRequested = [this](NumberedEditAction action) { numberedAction(action); };
    editorToolbar_->keyboardToggled = [this](bool checked) { keyboardHost_->setVisible(checked); };
    editorToolbar_->propertiesToggled = [this](bool checked) { inspectorScroll_->setVisible(checked); };
    editorToolbar_->durationRequested = [this](int duration)
    {
        if (!numberedEditorActive() || !resolveNoteDraft())
        {
            refreshNumberedEditor();
            return;
        }
        inputDuration_ = duration;
        if (selected_ >= 0)
        {
            auto score = project_.score;
            score.notes[std::size_t(selected_)].durationTicks = duration;
            commitNumberedScore(std::move(score), "ui.editor.notes", selected_, false);
        }
        refreshNumberedEditor();
    };
    view_->numberedInputSelected = [this](std::int64_t tick) { selectNumberedInput(tick); };
    view_->numberedBarsRequested = [this] { numberedAction(NumberedEditAction::AddBars); };
    view_->numberedKeyPressed = [this](int key)
    {
        if (key >= Qt::Key_0 && key <= Qt::Key_7)
            inputNumberedPitch(key - Qt::Key_0, inputOctave_, inputAccidental_);
        else if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
            numberedAction(NumberedEditAction::DeleteNote);
        else if (key == Qt::Key_Escape || key == Qt::Key_End)
            selectNumberedInput(numberedDuration(project_.score));
        else
            return false;
        return true;
    };
}

void MainWindow::refreshNumberedEditor()
{
    if (!editorToolbar_ || !scoreProperties_ || !keyboardHost_)
        return;
    const bool authored = project_.generatedNotation && project_.score.editorMinimumBars > 0 &&
                          !project_.staffPerformance && !project_.audioSource &&
                          project_.notationStyle == NotationStyle::Numbered;
    workspaceHeader_->setAuthoring(authored);
    scoreContextToolbar_->setVisible(!authored);
    const bool active = numberedEditorActive();
    const NumberedComposition single{{{1, trText("ui.editor.voice_name").arg(1).toStdString(), {}}}, 2};
    editorToolbar_->voicePanel()->setComposition(
        project_.numberedComposition ? *project_.numberedComposition : single, activeNumberedVoice_);
    const bool entering = active && editorToolbar_->isHidden();
    editorToolbar_->setVisible(active);
    if (auto *mix = findChild<QWidget *>("practiceMixControls"))
        mix->setVisible(!active && !project_.numberedComposition);
    keyboardHost_->setVisible(active && findChild<QPushButton *>("editorKeyboardToggle")->isChecked());
    editorInspectorTabs_->setTabVisible(0, active);
    if (entering)
        editorInspectorTabs_->setCurrentIndex(0);
    if (authored)
    {
        QSignalBlocker keyBlock(key_), tempoBlock(tempo_), topBlock(meterTop_), bottomBlock(meterBottom_);
        key_->setCurrentIndex(key_->findData(project_.score.tonic));
        tempo_->setValue(project_.score.bpm);
        meterTop_->setValue(project_.score.beatsPerBar);
        meterBottom_->setCurrentIndex(meterBottom_->findData(project_.score.beatUnit));
    }
    if (active)
    {
        inspectorScroll_->setVisible(findChild<QPushButton *>("editorPropertiesToggle")->isChecked());
        scoreProperties_->setScore(project_.score);
        editorToolbar_->retranslate();
        const auto end = numberedDuration(project_.score);
        const auto measures = project_.numberedComposition
                                  ? numberedMeasures(project_.score, *project_.numberedComposition)
                                  : numberedMeasures(project_.score, {}, end);
        const auto requested = std::max(end, numberedInputTick_);
        numberedInputTick_ = end;
        for (const auto &slot : numberedLayout_.slots)
            if ((slot.voiceId == 0 || slot.voiceId == activeNumberedVoice_) && slot.tick >= end &&
                slot.tick <= requested)
                numberedInputTick_ = std::max(numberedInputTick_, slot.tick);
        const int bar = selected_ >= 0 && selected_ < int(project_.score.notes.size())
                            ? project_.score.notes[std::size_t(selected_)].measure
                            : int(numberedMeasureAt(measures, numberedInputTick_));
        const auto &measure = measures[std::size_t(bar)];
        const auto barTicks = measure.end - measure.start;
        const auto used = std::clamp<std::int64_t>(end - measure.start, 0, barTicks);
        editorToolbar_->setPosition(bar, double(used) / TicksPerQuarter, double(barTicks) / TicksPerQuarter,
                                    selected_ < 0 && end >= measures.back().end);
        const Note *selected = selected_ >= 0 && selected_ < int(project_.score.notes.size())
                                   ? &project_.score.notes[std::size_t(selected_)]
                                   : nullptr;
        const int duration = selected ? selected->durationTicks : inputDuration_;
        const bool dotted = duration == 1440 || duration == 720 || duration == 360 || duration == 180 ||
                            duration == 90 || duration == 45;
        const int baseDuration = dotted ? duration * 2 / 3 : duration;
        auto *durations = findChild<QPushButton *>("editorDuration480")->group();
        durations->setExclusive(false);
        for (int ticks : {960, 480, 240, 120, 60, 30})
            if (auto *button = findChild<QPushButton *>(qPrintable(QString("editorDuration%1").arg(ticks))))
                button->setChecked(ticks == baseDuration);
        durations->setExclusive(true);
        auto *dot = findChild<QPushButton *>("editorDotted");
        dot->setCheckable(true);
        dot->setChecked(dotted);
        findChild<QPushButton *>("editorLyrics")->setEnabled(selected != nullptr);
        findChild<QPushButton *>("editorDeleteNote")->setEnabled(selected != nullptr);
        auto *tie = findChild<QPushButton *>("editorTie");
        tie->setCheckable(true);
        tie->setChecked(selected && selected->tieToNext);
        tie->setEnabled(selected != nullptr);
        for (const auto *name : {"editorAccent", "editorStaccato"})
        {
            auto *button = findChild<QPushButton *>(name);
            button->setCheckable(true);
            const bool accent = QByteArray(name) == "editorAccent";
            button->setChecked(selected ? (accent ? selected->accent : selected->staccato)
                                        : (accent ? inputAccent_ : inputStaccato_));
        }
        auto *dynamic = findChild<QComboBox *>("numberedDynamics");
        const QSignalBlocker dynamicBlock(dynamic);
        dynamic->setCurrentIndex(selected ? selected->dynamicLevel : inputDynamic_);
    }
    view_->setNumberedEditor(
        numberedLayout_, selected_ < 0 ? numberedInputTick_ : -1, active, activeNumberedVoice_,
        selected_ < 0 && numberedDuration(project_.score) > 0 &&
            std::none_of(numberedLayout_.slots.begin(), numberedLayout_.slots.end(),
                         [this](const auto &slot)
                         {
                             return (slot.voiceId == 0 || slot.voiceId == activeNumberedVoice_) &&
                                    slot.tick == numberedInputTick_;
                         }));
}

bool MainWindow::commitNumberedScore(Score score, const char *key, int selection, bool resolveDraft)
{
    if ((!numberedEditorActive() && !project_.numberedComposition) || busy_ || audioLoading_ ||
        notePreviewLoading_ || (resolveDraft && !resolveNoteDraft()))
        return false;
    const bool marked = std::any_of(score.notes.begin(), score.notes.end(), [](const Note &note)
                                    { return note.dynamicLevel || note.accent || note.staccato; });
    if (project_.numberedComposition || marked)
    {
        auto composition = currentNumberedComposition();
        numberedVoice(composition, activeNumberedVoice_).notes = score.notes;
        return commitNumberedComposition(std::move(score), std::move(composition), activeNumberedVoice_, selection,
                                         key);
    }
    if (score == project_.score)
        return true;
    NumberedScoreLayout layout;
    QImage image;
    // Validate and lay out a candidate before changing the live project or undo stack.
    try
    {
        if (numberedBarCount(score) > MaximumEditorBars)
            throw std::runtime_error(trText("ui.editor.bar_limit").toStdString());
        image = renderNumberedScore(score, &layout);
    }
    catch (const std::exception &error)
    {
        setStatusMessage(QString::fromUtf8(error.what()));
        scoreProperties_->setScore(project_.score);
        refreshNumberedEditor();
        return false;
    }
    auto before = materialState();
    before.numberedScore = project_.score;
    project_.score = std::move(score);
    project_.image = std::move(image);
    numberedLayout_ = std::move(layout);
    selected_ = selection >= 0 && selection < int(project_.score.notes.size()) ? selection : -1;
    commitMaterialEdit(before, key);
    rebuild(true, false);
    loadingNote_ = true;
    selectNote(selected_, false);
    loadingNote_ = false;
    return true;
}

void MainWindow::selectNumberedInput(std::int64_t tick)
{
    if (!numberedEditorActive() || !resolveNoteDraft())
        return;
    numberedInputTick_ = tick;
    selected_ = -1;
    view_->setCurrent(-1, false);
    refreshNumberedEditor();
    view_->setFocus();
}

void MainWindow::inputNumberedPitch(int degree, int octave, int accidental)
{
    if (!numberedEditorActive() || !resolveNoteDraft())
        return;
    auto score = project_.score;
    const bool inserting = selected_ < 0;
    if (inserting)
    {
        Note note;
        note.degree = degree;
        note.octave = octave;
        note.accidental = accidental;
        note.durationTicks = inputDuration_;
        note.dynamicLevel = inputDynamic_;
        note.accent = inputAccent_;
        note.staccato = inputStaccato_;
        const auto limit =
            project_.numberedComposition && !project_.numberedComposition->structure.measures.empty()
                ? std::int64_t(MaximumEditorBars) * MaximumNoteDurationTicks
                : 0;
        if (!appendNumberedNote(score, numberedInputTick_, note, limit))
        {
            setStatus("ui.editor.structure_unavailable");
            return;
        }
    }
    else
    {
        auto &note = score.notes[std::size_t(selected_)];
        note.degree = degree;
        note.octave = octave;
        note.accidental = accidental;
        note.confidence = 1;
    }
    if (commitNumberedScore(std::move(score), "ui.editor.notes", selected_) && inserting)
        selectNumberedInput(numberedDuration(project_.score));
    view_->setFocus();
}

void MainWindow::numberedAction(NumberedEditAction action)
{
    using Action = NumberedEditAction;

    if (!numberedEditorActive() || !resolveNoteDraft())
    {
        refreshNumberedEditor();
        return;
    }
    if (action == Action::MeasureProperties || action == Action::MeasureRepeat)
    {
        editNumberedStructure(action == Action::MeasureRepeat);
        return;
    }
    if (action == Action::Append)
    {
        selectNumberedInput(numberedDuration(project_.score));
        return;
    }
    if (action == Action::Lyrics)
    {
        editNumberedLyrics();
        return;
    }
    if (action == Action::Rest)
    {
        inputNumberedPitch(0, 0, 0);
        return;
    }
    if (action == Action::DeleteNote)
    {
        removeNote();
        refreshNumberedEditor();
        return;
    }
    auto score = project_.score;
    int selection = selected_;
    const auto measures = project_.numberedComposition ? numberedMeasures(score, *project_.numberedComposition)
                                                       : numberedMeasures(score, {}, numberedDuration(score));
    const int currentBar = selected_ >= 0 ? score.notes[std::size_t(selected_)].measure
                                          : int(numberedMeasureAt(measures, numberedInputTick_));
    if (action == Action::MeasureRest)
    {
        const int count = int(measures.size());
        QInputDialog target(this);
        target.setObjectName("measureRestDialog");
        target.setWindowTitle(trText("ui.editor.measure_rest"));
        target.setLabelText(trText("ui.editor.measure_rest_target").arg(count));
        target.setInputMode(QInputDialog::IntInput);
        target.setIntRange(1, count);
        target.setIntValue(currentBar + 1);
        target.setOkButtonText(trText("ui.inspector.apply"));
        target.setCancelButtonText(trText("ui.button.cancel"));
        if (target.exec() != QDialog::Accepted)
            return;
        const int bar = target.intValue() - 1;
        const auto &span = measures[std::size_t(bar)];
        if (!replaceNumberedTimeWithRest(score, span.start, int(span.end - span.start)))
        {
            setStatus("ui.editor.structure_unavailable");
            return;
        }
        std::int64_t tick = 0;
        int restIndex = -1;
        for (std::size_t i = 0; i < score.notes.size(); ++i)
        {
            if (tick == span.start)
            {
                restIndex = int(i);
                break;
            }
            tick += score.notes[i].durationTicks;
        }
        if (commitNumberedScore(std::move(score), "ui.editor.measure_rest", restIndex, false))
            selectNote(restIndex, false);
        return;
    }

    if (action == Action::AddBars)
    {
        score.editorMinimumBars =
            (project_.numberedComposition ? compositionBars(score, *project_.numberedComposition)
                                          : numberedBarCount(score)) +
            4;
        if (score.editorMinimumBars > MaximumEditorBars)
        {
            setStatus("ui.editor.bar_limit");
            return;
        }
    }
    else if (action == Action::InsertBarBefore || action == Action::InsertBarAfter || action == Action::DeleteBar)
    {
        int bar = currentBar;
        if (action == Action::InsertBarAfter)
            ++bar;
        if (project_.numberedComposition)
        {
            try
            {
                auto composition = *project_.numberedComposition;
                editCompositionMeasure(score, composition, bar, action == Action::DeleteBar);
                score.notes = numberedVoice(composition, activeNumberedVoice_).notes;
                commitNumberedComposition(std::move(score), std::move(composition), activeNumberedVoice_, -1,
                                          "ui.editor.bars");
            }
            catch (const std::exception &error)
            {
                setStatusMessage(QString::fromUtf8(error.what()));
            }
            return;
        }
        const bool changed =
            action == Action::DeleteBar ? removeNumberedBar(score, bar) : insertNumberedBar(score, bar);
        if (!changed)
        {
            setStatus("ui.editor.structure_unavailable");
            return;
        }
        selection = -1;
    }
    else
    {
        Note input;
        input.durationTicks = inputDuration_;
        input.octave = inputOctave_;
        input.accidental = inputAccidental_;
        input.accent = inputAccent_;
        input.staccato = inputStaccato_;
        auto &note = selected_ >= 0 ? score.notes[std::size_t(selected_)] : input;
        switch (action)
        {
        case Action::Accent:
            note.accent = !note.accent;
            break;
        case Action::Staccato:
            note.staccato = !note.staccato;
            break;
        case Action::Dotted:
            if (note.durationTicks == 1440 || note.durationTicks == 720 || note.durationTicks == 360 ||
                note.durationTicks == 180 || note.durationTicks == 90 || note.durationTicks == 45)
                note.durationTicks = note.durationTicks * 2 / 3;
            else
                note.durationTicks = std::min(MaximumNoteDurationTicks, note.durationTicks * 3 / 2);
            break;
        case Action::Sharp:
            note.accidental = 1;
            break;
        case Action::Flat:
            note.accidental = -1;
            break;
        case Action::Natural:
            note.accidental = 0;
            break;
        case Action::OctaveUp:
            note.octave = std::min(4, note.octave + 1);
            break;
        case Action::OctaveDown:
            note.octave = std::max(-4, note.octave - 1);
            break;
        case Action::Tie:
            note.tieToNext = !note.tieToNext;
            break;
        default:
            return;
        }
        if (selected_ < 0)
        {
            inputDuration_ = std::min(input.durationTicks, MaximumNoteDurationTicks);
            inputOctave_ = input.octave;
            inputAccidental_ = input.accidental;
            inputAccent_ = input.accent;
            inputStaccato_ = input.staccato;
            refreshNumberedEditor();
            return;
        }
    }
    commitNumberedScore(std::move(score), "ui.editor.bars", selection);
}

void MainWindow::editNumberedLyrics()
{
    if (selected_ < 0)
    {
        setStatus("ui.editor.select_note");
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName("numberedLyricsDialog");
    dialog.setWindowTitle(trText("ui.editor.lyrics"));
    dialog.resize(460, 310);
    QVBoxLayout layout(&dialog);
    auto *hint = new QLabel(trText("ui.editor.lyrics_hint").arg(selected_ + 1));
    hint->setWordWrap(true);
    layout.addWidget(hint);
    QPlainTextEdit text;
    text.setObjectName("numberedLyricsText");
    layout.addWidget(&text);
    QLabel count;
    layout.addWidget(&count);
    QDialogButtonBox buttons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Apply)->setText(trText("ui.inspector.apply"));
    buttons.button(QDialogButtonBox::Cancel)->setText(trText("ui.button.cancel"));
    layout.addWidget(&buttons);
    auto parse = [&text]
    {
        QStringList tokens;
        QString group;
        bool grouping = false;
        const auto input = text.toPlainText();
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, input);
        int previous = 0;
        while (finder.toNextBoundary() != -1)
        {
            const auto token = input.mid(previous, finder.position() - previous);
            previous = finder.position();
            if (token == "(" && !grouping)
            {
                grouping = true;
                continue;
            }
            if (token == ")" && grouping)
            {
                tokens << group;
                group.clear();
                grouping = false;
                continue;
            }
            if (grouping)
            {
                group += token;
                continue;
            }
            if (token.trimmed().isEmpty())
                continue;
            tokens << (token == "%" ? QString() : token == "_" ? QStringLiteral("—") : token);
        }
        if (grouping)
            tokens << group;
        return tokens;
    };
    std::vector<std::size_t> targets;
    for (std::size_t i = std::size_t(selected_); i < project_.score.notes.size(); ++i)
        if (project_.score.notes[i].degree != 0)
            targets.push_back(i);
    auto update = [&]
    {
        const auto size = parse().size();
        count.setText(trText("ui.editor.lyrics_count").arg(size).arg(targets.size()));
        buttons.button(QDialogButtonBox::Apply)->setEnabled(size > 0 && size <= qsizetype(targets.size()));
    };
    connect(&text, &QPlainTextEdit::textChanged, &dialog, update);
    connect(buttons.button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto tokens = parse();
    auto score = project_.score;
    for (qsizetype i = 0; i < tokens.size(); ++i)
    {
        auto &note = score.notes[targets[std::size_t(i)]];
        auto verses = lyricVerses(note);
        if (verses.empty())
            verses.emplace_back();
        verses[0] = tokens[i].toStdString();
        note.lyric = verses[0];
        note.verseLyrics = std::move(verses);
    }
    commitNumberedScore(std::move(score), "ui.editor.lyrics", selected_);
}
} // namespace singlilt

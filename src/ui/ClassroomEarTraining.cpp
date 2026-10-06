// Ear-training interactions in the classroom.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ClassroomDialog.h"
#include "NotationRenderer.h"
#include "PitchCurve.h"
#include "ScoreView.h"
#include "i18n/LanguageManager.h"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>

namespace singlilt
{
Score ClassroomDialog::questionScore(bool includeReference) const
{
    Score score;
    score.title = trText("ui.classroom.example").toStdString();
    score.bpm = 80;
    score.versePrograms = {0};
    score.accentBeats = false;
    constexpr std::array<int, 7> major{0, 2, 4, 5, 7, 9, 11};
    int tick = 0;
    const auto append = [&](const EarTone &tone)
    {
        Note note;
        note.id = static_cast<int>(score.notes.size());
        note.durationTicks = tone.durationTicks;
        if (tone.midiPitch < 0)
            note.degree = 0;
        else
        {
            note.octave = static_cast<int>(std::floor((tone.midiPitch - 60) / 12.0));
            const int chroma = tone.midiPitch - 60 - note.octave * 12;
            int degree = 0;
            while (degree + 1 < 7 && major[degree + 1] <= chroma)
                ++degree;
            note.degree = degree + 1;
            note.accidental = chroma - major[degree];
        }
        note.measure = tick / ticksPerBar(score);
        tick += note.durationTicks;
        score.notes.push_back(note);
    };
    if (includeReference && !question_.reference.empty())
    {
        for (const auto &tone : question_.reference)
            append(tone);
        append({-1, 960});
    }
    for (const auto &tone : question_.prompt)
        append(tone);
    return score;
}
void ClassroomDialog::newQuestion(bool weakOnly)
{
    if (lessonIndex_ < 0 || exerciseType_->count() == 0)
        return;
    stopActivity();
    if (weakOnly)
    {
        std::array<int, 7> errors{}, answered{};
        const auto &attempts = history_.attempts();
        for (int i = std::max(0, static_cast<int>(attempts.size()) - 50); i < attempts.size(); ++i)
        {
            const auto entry = attempts[i].toObject();
            const int type = entry.value("exercise").toInt(-1);
            if (entry.value("kind").toString() == "ear" && entry.value("reliable").toBool() && type >= 0 &&
                type < 7)
            {
                ++answered[static_cast<std::size_t>(type)];
                errors[static_cast<std::size_t>(type)] += !entry.value("correct").toBool();
            }
        }
        int selected = exerciseType_->currentIndex();
        double highest = -1.0;
        for (int i = 0; i < exerciseType_->count(); ++i)
        {
            const int type = exerciseType_->itemData(i).toInt();
            const double rate =
                answered[static_cast<std::size_t>(type)] > 0
                    ? double(errors[static_cast<std::size_t>(type)]) / answered[static_cast<std::size_t>(type)]
                    : 0.0;
            if (rate > highest)
            {
                highest = rate;
                selected = i;
            }
        }
        QSignalBlocker blocker(exerciseType_);
        exerciseType_->setCurrentIndex(selected);
        int level = difficulty_->currentIndex();
        if (highest >= 0.5)
            level = std::max(0, level - 1);
        const QSignalBlocker difficultyBlocker(difficulty_);
        difficulty_->setCurrentIndex(level);
    }
    const auto type = static_cast<EarExercise>(exerciseType_->currentData().toInt());
    question_ = makeEarQuestion(type, difficulty_->currentIndex(), random_, 60 + transpose_->value());
    heardCount_ = 0;
    heard_ = false;
    answered_ = false;
    earFeedback_->clear();
    answerScore_->hide();
    results_->setRowCount(0);
    lastResult_.reset();
    summary_->clear();
    earPrompt_->setText(
        trText(question_.requiresMicrophone() ? "ui.ear.echo_instruction" : "ui.ear.choice_instruction")
            .arg(trText(earExerciseKey(type))));
    renderOptions();
    if (earMode())
        curve_->setFrames({}, {}, false);
    setBusy(false);
}
void ClassroomDialog::renderOptions()
{
    const int selected = options_->currentRow();
    options_->setRowCount(static_cast<int>(question_.options.size()));
    for (int row = 0; row < static_cast<int>(question_.options.size()); ++row)
    {
        const int option = question_.options[static_cast<std::size_t>(row)];
        QString label;
        switch (question_.type)
        {
        case EarExercise::PitchDirection:
            label = trText(option < 0 ? "ui.ear.lower" : option > 0 ? "ui.ear.higher" : "ui.ear.equal");
            break;
        case EarExercise::SameMelody:
            label = trText(option == 1 ? "ui.ear.same_answer" : "ui.ear.different");
            break;
        case EarExercise::ScaleDegree:
            label = trText("ui.ear.degree_answer")
                        .arg(option)
                        .arg(trText(qPrintable(QString("ui.note.syllable.%1").arg(option))));
            break;
        case EarExercise::Interval:
            label = trText("ui.ear.interval_answer").arg(option);
            break;
        case EarExercise::Rhythm:
        {
            QStringList beats;
            for (const auto &tone : question_.rhythmOptions[static_cast<std::size_t>(row)])
                beats.append(QString::number(tone.durationTicks / 480.0, 'g', 3));
            label = trText("ui.ear.rhythm_answer").arg(row + 1).arg(beats.join(" / "));
            break;
        }
        default:
            break;
        }
        auto *item = new QTableWidgetItem(label);
        // Correctness is not attached to a visible item until the student submits.
        if (answered_ && row == question_.correctIndex)
        {
            auto color = palette().color(QPalette::Link);
            color.setAlpha(48);
            item->setBackground(color);
        }
        options_->setItem(row, 0, item);
    }
    if (selected >= 0 && selected < options_->rowCount())
        options_->selectRow(selected);
    if (question_.requiresMicrophone())
        options_->hide();
    else
        options_->show();
}
void ClassroomDialog::answerQuestion()
{
    if (!heard_ || answered_ || question_.requiresMicrophone() || options_->currentRow() < 0)
        return;
    const int selected = options_->currentRow();
    const bool correct = selected == question_.correctIndex;
    answered_ = true;
    renderOptions();
    earFeedback_->setText(trText("ui.ear.feedback")
                              .arg(trText(correct ? "ui.ear.correct" : "ui.ear.retry"))
                              .arg(options_->item(question_.correctIndex, 0)->text())
                              .arg(heardCount_));
    revealQuestion();
    appendHistory({{"kind", "ear"},
                   {"lessonId", lessons_[static_cast<std::size_t>(lessonIndex_)].id},
                   {"exercise", static_cast<int>(question_.type)},
                   {"correct", correct},
                   {"reliable", true},
                   {"replays", heardCount_},
                   {"difficulty", difficulty_->currentIndex()}});
    setBusy(false);
}
void ClassroomDialog::revealQuestion()
{
    auto score = questionScore(true);
    answerScore_->setScore(renderNumberedScore(score), score);
    answerScore_->show();
    QTimer::singleShot(0, answerScore_,
                       [this]
                       {
                           answerScore_->fitWidth();
                           answerScore_->setCurrent(0, true);
                       });
    if (!question_.requiresMicrophone())
        curve_->setFrames({}, {}, true);
}
} // namespace singlilt

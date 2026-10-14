// Lesson practice, recording, and singing-feedback window.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ClassroomDialog.h"
#include "NotationRenderer.h"
#include "PitchCurve.h"
#include "ScoreView.h"
#include "domain/Accompaniment.h"
#include "i18n/LanguageManager.h"
#include "storage/LessonStore.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
QString text(const std::string &value)
{
    return QString::fromStdString(value);
}
} // namespace

ClassroomDialog::ClassroomDialog(LanguageManager &languages, AudioBackend backend, QString lessonDirectory,
                                 QString historyPath, QWidget *parent)
    : QDialog(parent), languages_(languages), lessonDirectory_(std::move(lessonDirectory)),
      history_(std::move(historyPath))
{
    preferences_ = loadAppSettings();
    createUi();
    player_.setAudioBackend(backend);
    player_.setOutputBoost(preferences_.outputBoost);
    try
    {
        history_.load();
    }
    catch (const std::exception &error)
    {
        historyWritable_ = false;
        historyError_ = QString::fromUtf8(error.what());
        lastError_ = trText("messages.classroom.history_error").arg(QString::fromUtf8(error.what()));
    }
    reloadLessons(lessonDirectory_);
    refreshDevices();
    if (!preferences_.microphoneId.isEmpty())
    {
        if (devices_->findData(preferences_.microphoneId) < 0)
            devices_->addItem(trText("ui.options.missing_device"), preferences_.microphoneId);
        devices_->setCurrentIndex(devices_->findData(preferences_.microphoneId));
    }
    speed_->setValue(preferences_.practiceSpeed);
    latency_->setValue(preferences_.latencyMilliseconds);
    difficulty_->setCurrentIndex(preferences_.difficulty);
    guide_->setCurrentIndex(preferences_.guide);
    tolerance_->setValue(preferences_.centsTolerance);
    refreshHistory();
    refreshHistorySaveUi();
    if (!lastError_.isEmpty())
        status_->setText(lastError_);
    connect(&audioLoader_, &QFutureWatcher<bool>::finished, this,
            [this]
            {
                bool played = false;
                try
                {
                    played = audioLoader_.result();
                }
                catch (const std::exception &error)
                {
                    showError(QString::fromUtf8(error.what()));
                }
                if (!played)
                {
                    demonstrating_ = false;
                    showError(player_.errorString());
                    setBusy(false);
                    return;
                }
                audioReady_ = true;
                if (!demonstrating_)
                    return;
                status_->setText(trText("ui.classroom.listening"));
            });
    timer_ = new QTimer(this);
    timer_->setInterval(25);
    connect(timer_, &QTimer::timeout, this, [this] { tick(); });
    timer_->start();
}
ClassroomDialog::~ClassroomDialog()
{
    timer_->stop();
    audioLoader_.disconnect(this);
    audioLoader_.waitForFinished();
    session_.stop();
    player_.stop();
    replay_.close();
}
void ClassroomDialog::closeEvent(QCloseEvent *event)
{
    if (!confirmHistoryDiscard())
    {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}
bool ClassroomDialog::confirmHistoryDiscard()
{
    if (pendingHistory_.isEmpty())
        return true;
    QMessageBox confirmation(QMessageBox::Question, trText("ui.history.unsaved_title"),
                             trText("ui.history.close_pending").arg(pendingHistory_.size()),
                             QMessageBox::Discard | QMessageBox::Cancel, this);
    confirmation.setObjectName("historyCloseConfirmation");
    confirmation.setDefaultButton(QMessageBox::Cancel);
    if (confirmation.exec() != QMessageBox::Discard)
        return false;
    pendingHistory_ = {};
    return true;
}
void ClassroomDialog::reject()
{
    done(QDialog::Rejected);
}
void ClassroomDialog::done(int result)
{
    if (!confirmHistoryDiscard())
        return;
    stopActivity();
    QDialog::done(result);
}
void ClassroomDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && tabs_)
    {
        try
        {
            // Changing display language preserves the live exercise and recording clock.
            auto localized = loadSingingLessons(lessonDirectory_, languages_.language());
            if (localized.size() == lessons_.size())
                for (std::size_t i = 0; i < localized.size(); ++i)
                    if (localized[i].id == lessons_[i].id)
                    {
                        lessons_[i].title = localized[i].title;
                        lessons_[i].goal = localized[i].goal;
                        lessons_[i].steps = localized[i].steps;
                        lessons_[i].selfCheck = localized[i].selfCheck;
                    }
        }
        catch (const std::exception &error)
        {
            showError(QString::fromUtf8(error.what()));
        }
        retranslate();
    }
}
void ClassroomDialog::retranslate()
{
    setWindowTitle(trText("ui.classroom.title"));
    for (auto *object : findChildren<QObject *>())
    {
        for (const auto &property : object->dynamicPropertyNames())
            if (property.startsWith("_ui_"))
                object->setProperty(property.mid(4).constData(),
                                    trText(object->property(property.constData()).toByteArray().constData()));
    }
    tabs_->setTabText(0, trText("ui.classroom.course_tab"));
    tabs_->setTabText(1, trText("ui.classroom.ear_tab"));
    tabs_->setTabText(2, trText("ui.classroom.history_tab"));
    const auto fill = [](QComboBox *combo, const QStringList &items)
    {
        QSignalBlocker blocker(combo);
        const int index = std::max(0, combo->currentIndex());
        combo->clear();
        combo->addItems(items);
        combo->setCurrentIndex(std::min(index, combo->count() - 1));
    };
    fill(range_, {trText("ui.classroom.whole"), trText("ui.classroom.phrase")});
    fill(difficulty_, {trText("ui.ear.easy"), trText("ui.ear.medium"), trText("ui.ear.hard")});
    fill(guide_, {trText("ui.classroom.independent"), trText("ui.classroom.follow"),
                  trText("ui.classroom.accompaniment")});
    for (int i = 0; i < exerciseType_->count(); ++i)
        exerciseType_->setItemText(
            i, trText(earExerciseKey(static_cast<EarExercise>(exerciseType_->itemData(i).toInt()))));
    {
        QSignalBlocker blocker(lessonSelector_);
        for (int i = 0; i < static_cast<int>(lessons_.size()); ++i)
            lessonSelector_->setItemText(i, text(lessons_[static_cast<std::size_t>(i)].title));
    }
    results_->setHorizontalHeaderLabels({trText("ui.classroom.note"), trText("ui.classroom.verdict"),
                                         trText("ui.classroom.cents"), trText("ui.classroom.coverage"),
                                         trText("ui.classroom.timing")});
    options_->setHorizontalHeaderLabels({trText("ui.ear.answer")});
    historyTable_->setHorizontalHeaderLabels({trText("ui.classroom.date"), trText("ui.classroom.lesson"),
                                              trText("ui.classroom.exercise"), trText("ui.classroom.result"),
                                              trText("ui.classroom.replays")});
    updateLessonText();
    if (folder_)
        folder_->setText(trText("ui.classroom.folder").arg(lessonDirectory_));
    if (lessonIndex_ >= 0)
        earPrompt_->setText(
            trText(question_.requiresMicrophone() ? "ui.ear.echo_instruction" : "ui.ear.choice_instruction")
                .arg(trText(earExerciseKey(question_.type))));
    for (auto *combo : findChildren<QComboBox *>())
    {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->updateGeometry();
    }
    layout()->invalidate();
    layout()->activate();
    renderOptions();
    refreshHistory();
    refreshHistorySaveUi();
    curve_->update();
    if (lastResult_)
        displayResult(*lastResult_);
    if (answered_ && !question_.requiresMicrophone())
        earFeedback_->setText(
            trText("ui.ear.feedback")
                .arg(trText(options_->currentRow() == question_.correctIndex ? "ui.ear.correct" : "ui.ear.retry"))
                .arg(options_->item(question_.correctIndex, 0)->text())
                .arg(heardCount_));
    if (status_ && lastError_.isEmpty())
        status_->setText(trText(recording_       ? "ui.classroom.recording"
                                : calibrating_   ? "ui.classroom.quiet"
                                : demonstrating_ ? "ui.classroom.listening"
                                : heard_         ? "ui.classroom.heard"
                                                 : "ui.classroom.first_listen"));
}
void ClassroomDialog::reloadLessons(const QString &directory)
{
    auto catalog = loadSingingLessons(directory, languages_.language());
    stopActivity();
    lessons_ = std::move(catalog);
    lessonDirectory_ = directory;
    folder_->setText(trText("ui.classroom.folder").arg(directory));
    {
        QSignalBlocker blocker(lessonSelector_);
        lessonSelector_->clear();
        for (const auto &lesson : lessons_)
            lessonSelector_->addItem(text(lesson.title), lesson.id);
        lessonSelector_->setCurrentIndex(0);
    }
    selectLesson(0);
    status_->setText(trText("ui.classroom.loaded").arg(lessons_.size()));
}
void ClassroomDialog::updateLessonText()
{
    if (lessonIndex_ < 0 || lessonIndex_ >= static_cast<int>(lessons_.size()))
        return;
    const auto &lesson = lessons_[static_cast<std::size_t>(lessonIndex_)];
    instructions_->setPlainText(
        trText("ui.classroom.instructions").arg(text(lesson.goal), text(lesson.steps), text(lesson.selfCheck)));
}
void ClassroomDialog::selectLesson(int index)
{
    if (index < 0 || index >= static_cast<int>(lessons_.size()))
        return;
    stopActivity();
    lessonIndex_ = index;
    const auto &lesson = lessons_[static_cast<std::size_t>(index)];
    phraseFirst_ = 0;
    phraseEnd_ = static_cast<int>(lesson.score.notes.size());
    tolerance_->setValue(preferences_.centsTolerance);
    updateLessonText();
    auto score = lesson.score;
    scoreView_->setScore(renderNumberedScore(score), score);
    QTimer::singleShot(0, scoreView_,
                       [this]
                       {
                           scoreView_->fitWidth();
                           scoreView_->setCurrent(0, true);
                       });
    {
        QSignalBlocker blocker(exerciseType_);
        exerciseType_->clear();
        for (auto type : lesson.earExercises)
            exerciseType_->addItem(trText(earExerciseKey(type)), static_cast<int>(type));
    }
    heard_ = false;
    newQuestion();
    targets_.clear();
    curve_->setFrames({}, {}, true);
    summary_->setText(trText("ui.classroom.first_listen"));
    results_->setRowCount(0);
    setBusy(false);
}
void ClassroomDialog::updatePhrase(int sourceNoteIndex)
{
    if (lessonIndex_ < 0)
        return;
    const auto &score = lessons_[static_cast<std::size_t>(lessonIndex_)].score;
    if (sourceNoteIndex < 0 || sourceNoteIndex >= static_cast<int>(score.notes.size()))
        return;
    phraseFirst_ = sourceNoteIndex;
    while (phraseFirst_ > 0 && score.notes[static_cast<std::size_t>(phraseFirst_ - 1)].degree != 0)
        --phraseFirst_;
    // Scales without rests are split by written four-beat bars.
    if (phraseFirst_ == 0 &&
        std::none_of(score.notes.begin(), score.notes.end(), [](const auto &note) { return note.degree == 0; }))
    {
        phraseFirst_ = sourceNoteIndex;
        while (phraseFirst_ > 0 && score.notes[static_cast<std::size_t>(phraseFirst_ - 1)].measure ==
                                       score.notes[static_cast<std::size_t>(sourceNoteIndex)].measure)
            --phraseFirst_;
    }
    phraseEnd_ = phraseFirst_;
    do
    {
        ++phraseEnd_;
    } while (
        phraseEnd_ < static_cast<int>(score.notes.size()) &&
        score.notes[static_cast<std::size_t>(phraseEnd_)].degree != 0 &&
        (score.notes[static_cast<std::size_t>(phraseEnd_)].measure ==
             score.notes[static_cast<std::size_t>(phraseFirst_)].measure ||
         std::any_of(score.notes.begin(), score.notes.end(), [](const auto &note) { return note.degree == 0; })));
    scoreView_->setCurrent(sourceNoteIndex, false);
    heard_ = false;
    setBusy(false);
}
Score ClassroomDialog::currentLessonScore() const
{
    auto score = lessons_.at(static_cast<std::size_t>(lessonIndex_)).score;
    if (range_->currentIndex() == 1)
    {
        score.notes = std::vector<Note>(score.notes.begin() + phraseFirst_, score.notes.begin() + phraseEnd_);
        score.repeats.clear();
        if (!score.notes.empty())
            score.notes.back().tieToNext = false;
        int tick = 0;
        for (auto &note : score.notes)
        {
            note.measure = tick / ticksPerBar(score);
            tick += note.durationTicks;
        }
    }
    return score;
}

void ClassroomDialog::auditionNote(const Score &score, int index, int transpose)
{
    if (index < 0 || index >= static_cast<int>(score.notes.size()) || calibrating_ || previewStarting_)
        return;
    replay_.pause();
    const auto timeline = buildTimeline(score, transpose);
    const auto event = std::find_if(timeline.events.begin(), timeline.events.end(), [index](const auto &item)
                                    { return item.sourceNoteIndex == static_cast<std::size_t>(index); });
    if (!timeline.valid() || event == timeline.events.end())
        return;
    if (!player_.previewNote(event->midiPitch, event->program, score.baseVelocity))
        return;
    previewStarting_ = true;
    setBusy(true);
    status_->setText(trText("ui.classroom.loading_audio"));
}
bool ClassroomDialog::earMode() const
{
    return tabs_->currentIndex() == 1;
}
QString ClassroomDialog::selectedDevice() const
{
    return devices_->currentData().toString();
}
void ClassroomDialog::refreshDevices()
{
    const QString selected = selectedDevice();
    devices_->clear();
    for (const auto &device : session_.devices())
        devices_->addItem(device.name, device.id);
    if (devices_->findData(selected) >= 0)
        devices_->setCurrentIndex(devices_->findData(selected));
    if (devices_->count() == 0)
        status_->setText(trText("messages.mic.no_device"));
    setBusy(false);
}
void ClassroomDialog::setBusy(bool busy)
{
    for (QWidget *widget :
         std::initializer_list<QWidget *>{lessonSelector_, chooseFolder_, reload_, openScore_, transpose_, speed_,
                                          tolerance_, latency_, devices_, refreshMic_, calibrate_, exerciseType_,
                                          difficulty_, range_, guide_, loop_, next_, weak_, settingsButton_})
        widget->setEnabled(!busy);
    // The history tab cannot interrupt a recording by changing the exercise context.
    for (int i = 0; i < tabs_->count(); ++i)
        tabs_->setTabEnabled(i, !busy || i == tabs_->currentIndex());
    listen_->setEnabled(!busy && tabs_->currentIndex() != 2 && lessonIndex_ >= 0);
    record_->setEnabled(!busy && heard_ && devices_->count() > 0 && tabs_->currentIndex() != 2 &&
                        pendingHistory_.size() < 500 &&
                        (!earMode() || (question_.requiresMicrophone() && !answered_)));
    calibrate_->setEnabled(!busy && devices_->count() > 0);
    submit_->setEnabled(!busy && heard_ && !answered_ && !question_.requiresMicrophone() &&
                        pendingHistory_.size() < 500);
    stop_->setEnabled(busy || replay_.isPlaying());
    replayButton_->setEnabled(!busy && session_.hasRecording());
    exportButton_->setEnabled(!busy && session_.hasRecording());
    options_->setEnabled(!busy && !answered_);
    retryHistory_->setEnabled(!busy);
}
void ClassroomDialog::showError(const QString &message)
{
    lastError_ = message;
    status_->setText(message);
}
void ClassroomDialog::stopActivity()
{
    const bool interrupted = recording_ || calibrating_ || demonstrating_;
    demonstrating_ = false;
    recording_ = false;
    calibrating_ = false;
    previewStarting_ = false;
    audioLoader_.waitForFinished();
    player_.stop();
    replay_.pause();
    session_.stop();
    if (interrupted)
        status_->setText(trText("ui.classroom.cancelled"));
    setBusy(false);
}
void ClassroomDialog::playExample()
{
    if (lessonIndex_ < 0)
        return;
    try
    {
        stopActivity();
        exerciseScore_ = earMode() ? questionScore(true) : currentLessonScore();
        exerciseTimeline_ = buildTimeline(exerciseScore_);
        if (!exerciseTimeline_.valid() || !player_.load(exerciseScore_, exerciseTimeline_))
            throw std::runtime_error(player_.errorString().toStdString());
        if (!player_.setTranspose(earMode() ? 0 : transpose_->value()))
            throw std::runtime_error(player_.errorString().toStdString());
        player_.setSpeed(speed_->value());
        player_.setPracticeMix({true, false, 0.9, 0.55});
        player_.setMetronome(!earMode());
        player_.setMetronomeVolume(0.25);
        audioReady_ = false;
        demonstrating_ = true;
        if (earMode() && !answered_)
            heardCount_ = std::min(100, heardCount_ + 1);
        setBusy(true);
        status_->setText(trText("ui.classroom.loading_audio"));
        audioLoader_.setFuture(QtConcurrent::run([this] { return player_.play(); }));
    }
    catch (const std::exception &error)
    {
        demonstrating_ = false;
        showError(QString::fromUtf8(error.what()));
        setBusy(false);
    }
}
void ClassroomDialog::startRecording()
{
    if (lessonIndex_ < 0 || selectedDevice().isEmpty() || !heard_)
        return;
    try
    {
        stopActivity();
        exerciseScore_ = earMode() ? questionScore(false) : currentLessonScore();
        exerciseTimeline_ = buildTimeline(exerciseScore_);
        targets_ = expectedSingingTones(buildTimeline(exerciseScore_, earMode() ? 0 : transpose_->value()),
                                        speed_->value());
        const double duration = exerciseTimeline_.durationSeconds() / speed_->value();
        if (targets_.empty() || duration > 110.0)
            throw std::runtime_error(trText("messages.mic.limit").toStdString());
        sourceMap_.clear();
        for (const auto &note : exerciseScore_.notes)
            sourceMap_.push_back(static_cast<std::size_t>(note.id));
        if (!earMode())
        {
            auto arrangement = generateAccompaniment(exerciseScore_);
            const auto plan = buildAccompanimentPlan(exerciseScore_, exerciseTimeline_, arrangement);
            if (!player_.load(exerciseScore_, exerciseTimeline_, plan, arrangement.settings))
                throw std::runtime_error(player_.errorString().toStdString());
            if (!player_.setTranspose(earMode() ? 0 : transpose_->value()))
                throw std::runtime_error(player_.errorString().toStdString());
            player_.setSpeed(speed_->value());
            player_.setMetronome(true);
            player_.setPracticeMix({guide_->currentIndex() == 1, guide_->currentIndex() == 2, 0.5, 0.55});
        }
        recording_ = true;
        startedGuide_ = false;
        recordStart_ = MicrophoneCapture::clockSeconds() + 4.0 * 60.0 / exerciseScore_.bpm / speed_->value();
        // Retain a release tail so late endings are measurable rather than truncated.
        session_.record(selectedDevice(), recordStart_, duration + 1.0, latency_->value() / 1000.0);
        results_->setRowCount(0);
        lastResult_.reset();
        summary_->clear();
        curve_->setFrames(targets_, {}, !earMode());
        setBusy(true);
        status_->setText(trText("ui.classroom.countdown").arg(4));
    }
    catch (const std::exception &error)
    {
        recording_ = false;
        showError(QString::fromUtf8(error.what()));
        setBusy(false);
    }
}
void ClassroomDialog::tick()
{
    if (player_.isPreviewLoading())
        return;
    if (previewStarting_)
    {
        previewStarting_ = false;
        setBusy(false);
        if (!player_.errorString().isEmpty())
            showError(player_.errorString());
        else
            status_->setText(trText("ui.classroom.note_preview"));
    }
    if (demonstrating_ && !audioLoader_.isRunning() && audioReady_)
    {
        if (!player_.isPlaying())
        {
            if (!earMode() && loop_->isChecked())
            {
                player_.seek(0);
                if (!player_.play())
                    showError(player_.errorString());
            }
            else
            {
                demonstrating_ = false;
                heard_ = true;
                setBusy(false);
                status_->setText(trText("ui.classroom.heard"));
            }
        }
        else if (!earMode())
        {
            const auto event = exerciseTimeline_.eventIndexAtTick(player_.positionTicks());
            if (event)
            {
                const auto index = exerciseTimeline_.events[*event].sourceNoteIndex;
                if (index < exerciseScore_.notes.size())
                    scoreView_->setCurrent(exerciseScore_.notes[index].id, true);
            }
        }
    }
    if (!recording_ && !calibrating_)
    {
        if (!replay_.isPlaying())
            stop_->setEnabled(false);
        return;
    }
    const auto snapshot = session_.snapshot();
    level_->setValue(static_cast<int>(std::clamp(snapshot.latest.rms * 400.0, 0.0, 100.0)));
    if (snapshot.state == PracticeSession::State::Failed)
    {
        stopActivity();
        showError(snapshot.error);
        return;
    }
    if (calibrating_)
    {
        if (snapshot.state == PracticeSession::State::Finished)
        {
            calibrating_ = false;
            setBusy(false);
            status_->setText(trText("ui.classroom.calibrated").arg(snapshot.noiseGate, 0, 'f', 4));
        }
        return;
    }
    const double elapsed = snapshot.clockSeconds - recordStart_;
    if (elapsed < 0.0)
    {
        const double beat = 60.0 / exerciseScore_.bpm / speed_->value();
        status_->setText(
            trText("ui.classroom.countdown").arg(std::max(1, static_cast<int>(std::ceil(-elapsed / beat)))));
        return;
    }
    if (!startedGuide_)
    {
        startedGuide_ = true;
        if (!earMode() && !player_.play())
        {
            stopActivity();
            showError(player_.errorString());
            return;
        }
        status_->setText(trText("ui.classroom.recording"));
    }
    curve_->setFrames(targets_, snapshot.observations, !earMode() || answered_);
    const auto target = std::find_if(targets_.begin(), targets_.end(), [&](const auto &tone)
                                     { return elapsed >= tone.startSeconds && elapsed < tone.endSeconds; });
    if (!earMode() && target != targets_.end())
    {
        if (target->sourceNoteIndex < sourceMap_.size())
            scoreView_->setCurrent(static_cast<int>(sourceMap_[target->sourceNoteIndex]), true);
        if (snapshot.latest.confidence >= 0.85 && snapshot.latest.midiPitch >= 0 && !snapshot.latest.clipped)
        {
            const double cents = (snapshot.latest.midiPitch - target->midiPitch) * 100.0;
            const char *key = std::abs(cents) <= tolerance_->value() ? "ui.singing.accurate"
                              : cents > 0                            ? "ui.singing.high"
                                                                     : "ui.singing.low";
            live_->setText(trText("ui.classroom.live").arg(trText(key)).arg(cents, 0, 'f', 0));
        }
        else
            live_->setText(trText("ui.singing.uncertain"));
    }
    else
        live_->setText(trText(earMode() ? "ui.classroom.hidden_live" : "ui.transport.rest"));
    if (snapshot.state == PracticeSession::State::Finished)
        finishRecording();
}
void ClassroomDialog::finishRecording()
{
    recording_ = false;
    player_.stop();
    const auto &lesson = lessons_[static_cast<std::size_t>(lessonIndex_)];
    try
    {
        const auto result = assessSinging(targets_, session_.observations(), tolerance_->value(), 0.85,
                                          lesson.timingToleranceSeconds);
        lastResult_ = result;
        displayResult(result);
        live_->setText(trText("ui.classroom.feedback_ready"));
        if (earMode())
        {
            answered_ = true;
            const bool correct = result.reliable && result.coveragePercent >= 70.0 && result.pitchPercent >= 75.0;
            earFeedback_->setText(trText(correct ? "ui.ear.correct" : "ui.ear.retry"));
            revealQuestion();
            appendHistory({{"kind", "ear"},
                           {"lessonId", lesson.id},
                           {"exercise", static_cast<int>(question_.type)},
                           {"correct", correct},
                           {"reliable", result.reliable},
                           {"replays", heardCount_},
                           {"difficulty", difficulty_->currentIndex()}});
        }
        else
            appendHistory({{"kind", "singing"},
                           {"lessonId", lesson.id},
                           {"reliable", result.reliable},
                           {"pitch", result.pitchPercent},
                           {"rhythm", result.rhythmPercent},
                           {"coverage", result.coveragePercent}});
        status_->setText(trText(pendingHistory_.isEmpty() && historyWritable_ ? "ui.classroom.completed"
                                                                              : "ui.history.result_unsaved"));
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
    setBusy(false);
}
void ClassroomDialog::displayResult(const SingingResult &result)
{
    curve_->setFrames(targets_, session_.observations(), true);
    summary_->setText(result.reliable ? trText("ui.classroom.summary")
                                            .arg(result.pitchPercent, 0, 'f', 0)
                                            .arg(result.rhythmPercent, 0, 'f', 0)
                                            .arg(result.coveragePercent, 0, 'f', 0)
                                      : trText("ui.classroom.unreliable").arg(result.coveragePercent, 0, 'f', 0));
    results_->setRowCount(static_cast<int>(result.tones.size()));
    for (int row = 0; row < static_cast<int>(result.tones.size()); ++row)
    {
        const auto &tone = result.tones[static_cast<std::size_t>(row)];
        const auto source = tone.target.sourceNoteIndex < sourceMap_.size()
                                ? sourceMap_[tone.target.sourceNoteIndex]
                                : tone.target.sourceNoteIndex;
        auto *number = new QTableWidgetItem(QString::number(source + 1));
        number->setData(Qt::UserRole, static_cast<int>(source));
        results_->setItem(row, 0, number);
        results_->setItem(row, 1, new QTableWidgetItem(trText(singingVerdictKey(tone.verdict))));
        results_->setItem(row, 2,
                          new QTableWidgetItem(tone.coverage > 0.0 ? QString::number(tone.cents, 'f', 0) : "—"));
        results_->setItem(row, 3, new QTableWidgetItem(QString::number(tone.coverage * 100.0, 'f', 0) + "%"));
        results_->setItem(row, 4,
                          new QTableWidgetItem(tone.timingDetected
                                                   ? trText("ui.classroom.timing_values")
                                                         .arg(tone.onsetErrorSeconds * 1000.0, 0, 'f', 0)
                                                         .arg(tone.releaseErrorSeconds * 1000.0, 0, 'f', 0)
                                                   : trText("ui.classroom.no_timing")));
    }
}
bool ClassroomDialog::appendHistory(QJsonObject attempt)
{
    if (pendingHistory_.size() >= 500)
    {
        refreshHistorySaveUi();
        return false;
    }
    attempt.insert("attemptId", QUuid::createUuid().toString(QUuid::WithoutBraces));
    attempt.insert("time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    pendingHistory_.append(attempt);
    if (!historyWritable_)
    {
        refreshHistorySaveUi();
        return false;
    }
    return retryHistory();
}
bool ClassroomDialog::retryHistory()
{
    try
    {
        history_.load();
        while (!pendingHistory_.isEmpty())
        {
            history_.append(pendingHistory_.first().toObject());
            pendingHistory_.removeFirst();
        }
        historyWritable_ = true;
        historyError_.clear();
        refreshHistory();
        refreshHistorySaveUi();
        return true;
    }
    catch (const std::exception &error)
    {
        historyWritable_ = false;
        historyError_ = QString::fromUtf8(error.what());
        refreshHistorySaveUi();
        return false;
    }
}
void ClassroomDialog::refreshHistorySaveUi()
{
    const bool pending = !pendingHistory_.isEmpty() || !historyWritable_;
    historySaveStatus_->setText(
        pending ? trText("ui.history.pending").arg(pendingHistory_.size()).arg(historyError_) : QString());
    historySaveStatus_->setVisible(pending);
    retryHistory_->setVisible(pending);
    retryHistory_->setEnabled(!recording_ && !calibrating_ && !demonstrating_);
}
void ClassroomDialog::refreshHistory()
{
    const auto &attempts = history_.attempts();
    historyTable_->setRowCount(attempts.size());
    for (int row = 0; row < attempts.size(); ++row)
    {
        const auto attempt = attempts[attempts.size() - row - 1].toObject();
        historyTable_->setItem(
            row, 0,
            new QTableWidgetItem(QDateTime::fromString(attempt.value("time").toString(), Qt::ISODate)
                                     .toLocalTime()
                                     .toString("yyyy-MM-dd HH:mm")));
        historyTable_->setItem(row, 1, new QTableWidgetItem(QString::number(attempt.value("lessonId").toInt())));
        const bool ear = attempt.value("kind").toString() == "ear";
        historyTable_->setItem(row, 2,
                               new QTableWidgetItem(trText(ear ? earExerciseKey(static_cast<EarExercise>(
                                                                     attempt.value("exercise").toInt()))
                                                               : "ui.classroom.course_tab")));
        QString outcome = trText("ui.singing.uncertain");
        if (attempt.value("reliable").toBool())
            outcome = ear ? trText(attempt.value("correct").toBool() ? "ui.ear.correct" : "ui.ear.retry")
                          : trText("ui.classroom.history_score")
                                .arg(attempt.value("pitch").toDouble(), 0, 'f', 0)
                                .arg(attempt.value("rhythm").toDouble(), 0, 'f', 0);
        historyTable_->setItem(row, 3, new QTableWidgetItem(outcome));
        historyTable_->setItem(
            row, 4, new QTableWidgetItem(ear ? QString::number(attempt.value("replays").toInt()) : "—"));
    }
}
} // namespace singlilt

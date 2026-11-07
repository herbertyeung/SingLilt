// Complete rhythm-entry workflows, not just toolbar presence.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT
#include "NumberedRhythmCheck.h"
#include "cli/JsonReport.h"
#include "domain/NumberedEditing.h"
#include "storage/NumberedCompositionStore.h"
#include "ui/MainWindow.h"
#include "ui/NotationRenderer.h"
#include "ui/ScoreView.h"
#include <QAbstractButton>
#include <QApplication>
#include <QSlider>
#include <QListWidget>
#include <QMouseEvent>
#include <QProcess>
#include <QFile>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QCheckBox>
#include <QAction>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QInputDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QKeyEvent>
#include <QPushButton>
#include <QTimer>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTabWidget>
#include <QScrollBar>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace singlilt
{
namespace
{
template <typename Widget> Widget *requiredControl(QObject *parent, const char *name)
{
    if (auto *widget = parent->findChild<Widget *>(name))
        return widget;
    throw std::runtime_error(std::string("Missing diagnostic control: ") + name);
}
} // namespace

void runNumberedRhythmCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        100, &window,
        [&]
        {
            QJsonArray checks;
            bool passed = true;
            const auto folder = QFileInfo(args.value("report")).absolutePath();
            QDir().mkpath(folder);
            auto check = [&](const char *name, bool condition)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", condition}});
                passed &= condition;
            };
            auto click = [&](const char *name)
            {
                auto *button = window.findChild<QAbstractButton *>(name);
                if (!button)
                    throw std::runtime_error(std::string("Missing diagnostic button: ") + name);
                button->click();
            };
            const auto runDialog = [&window](QAbstractButton *button, const auto &handle)
            {
                QTimer timer;
                timer.setSingleShot(true);
                QString failure;
                bool opened = false;
                QObject::connect(&timer, &QTimer::timeout, &window,
                                 [&]
                                 {
                                     auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                                     if (!dialog)
                                         return;
                                     opened = true;
                                     try
                                     {
                                         handle(dialog);
                                     }
                                     catch (const std::exception &error)
                                     {
                                         failure = QString::fromUtf8(error.what());
                                         dialog->reject();
                                     }
                                 });
                timer.start(0);
                button->click();
                if (!opened || !failure.isEmpty())
                    throw std::runtime_error(opened ? failure.toStdString()
                                                    : "Expected diagnostic dialog did not open");
            };
            try
            {
                Project project;
                project.generatedNotation = true;
                project.score.editorMinimumBars = 4;
                project.score.bpm = 120;
                project.score.title = "节奏输入验证";
                Note shortNote, dotted;
                shortNote.durationTicks = 30;
                dotted.durationTicks = 45;
                dotted.degree = 3;
                project.score.notes = {shortNote, dotted};
                window.setProject(project);
                window.setCorrectionMode(true);
                window.selectNote(1, false);
                auto *sixtyFourth = window.findChild<QPushButton *>("editorDuration30");
                auto *dottedButton = window.findChild<QPushButton *>("editorDotted");
                if (!sixtyFourth || !dottedButton)
                    throw std::runtime_error("Missing numbered duration or dotted toolbar control");
                check("64th duration is reachable from the toolbar", true);
                check("Dotted 64th selection reflects its duration", dottedButton->isChecked());
                const auto hasFourLines = [](const QImage &image, const Note &note)
                {
                    const int center = int(std::lround(note.source.x + note.source.width / 2));
                    const int bottom = int(std::floor(note.source.y + note.source.height));
                    int lines = 0;
                    bool previousInk = false;
                    // Antialiasing and font metrics can move a stroke to an adjacent scan row.
                    for (int y = bottom + 1; y <= bottom + 21; ++y)
                    {
                        int ink = 0;
                        for (int x = center - 10; x <= center + 10; ++x)
                            if (image.valid(x, y) && image.pixelColor(x, y).lightness() < 224)
                                ++ink;
                        const bool hasInk = ink >= 12;
                        if (hasInk && !previousInk)
                            ++lines;
                        previousInk = hasInk;
                    }
                    return lines == 4;
                };
                check("Plain 64th renders four duration lines",
                      hasFourLines(window.project_.image, window.project_.score.notes[0]));
                check("Dotted 64th renders four lines rather than raw ticks",
                      hasFourLines(window.project_.image, window.project_.score.notes[1]));
                click("editorDotted");
                check("Dotted 64th toggles back to 30 ticks", window.project_.score.notes[1].durationTicks == 30);
                click("editorUndo");
                check("Undo restores dotted 64th duration", window.project_.score.notes[1].durationTicks == 45);
                for (int index : {0, 1})
                {
                    window.selectNote(index, false);
                    const auto before = window.project_.score;
                    const int history = window.materialUndo_.index();
                    const int preset = window.inputDuration_;
                    window.lyricEdit_->setText("keep rhythm draft");
                    QTimer::singleShot(0, &window,
                                       []
                                       {
                                           if (auto *message =
                                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                                               message->button(QMessageBox::Cancel)->click();
                                       });
                    click("editorDotted");
                    check("Cancel dotted change preserves model, preset, draft and checked state",
                          window.project_.score == before && window.inputDuration_ == preset &&
                              window.materialUndo_.index() == history &&
                              window.lyricEdit_->text() == "keep rhythm draft" &&
                              dottedButton->isChecked() == (index == 1));
                    window.lyricEdit_->setText("");
                }
                if (sixtyFourth)
                {
                    window.selectNumberedInput(75);
                    sixtyFourth->click();
                    QKeyEvent key(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier);
                    QApplication::sendEvent(window.view_, &key);
                    check("64th numeric input writes and advances exact ticks",
                          window.project_.score.notes.back().durationTicks == 30 &&
                              numberedDuration(window.project_.score) == 105);
                    click("editorDotted");
                    check("Dotted entry preset is 45 ticks",
                          window.inputDuration_ == 45 && dottedButton->isChecked());
                    click("editorDotted");
                    check("Dotted entry preset toggles back exactly",
                          window.inputDuration_ == 30 && !dottedButton->isChecked());
                }
                saveProject(folder + "/sixty-fourth.jpp", window.project_);
                const auto restored = loadProject(folder + "/sixty-fourth.jpp");
                check("64th notes survive project reopen",
                      restored.score.notes[0].durationTicks == 30 && restored.score.notes[1].durationTicks == 45);
                check("Playback time retains sub-beat precision",
                      std::abs(window.timeline_.secondsAtTick(75) - 0.078125) < 1e-8);
                check("Rhythm score image saved", window.project_.image.save(folder + "/rhythm-score.png"));
                window.resize(1320, 940);
                QApplication::processEvents();
                window.view_->fitWidth();
                window.view_->verticalScrollBar()->setValue(window.view_->verticalScrollBar()->minimum());
                check("Native rhythm editor screenshot", window.grab().save(folder + "/rhythm-editor.png"));

                auto *measureRest = window.findChild<QAbstractButton *>("editorBarRest");
                check("Whole-measure rest has an executable command", measureRest != nullptr);
                if (measureRest)
                {
                    const auto applyRest = [&](int measure, bool accept = true)
                    {
                        int dialogs = 0;
                        QTimer timer;
                        QObject::connect(
                            &timer, &QTimer::timeout, &window,
                            [&]
                            {
                                if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget()))
                                {
                                    ++dialogs;
                                    dialog->setIntValue(measure);
                                    if (accept)
                                        dialog->accept();
                                    else
                                        dialog->reject();
                                }
                            });
                        timer.start(1);
                        measureRest->click();
                        return dialogs;
                    };

                    project.score.notes = {Note{}, Note{}};
                    project.score.notes[1].degree = 3;
                    project.score.notes[1].durationTicks = 1920;
                    Note lower;
                    lower.degree = 5;
                    lower.durationTicks = 2400;
                    project.numberedComposition =
                        NumberedComposition{{{1, "Upper", project.score.notes}, {2, "Lower", {lower}}}, 3};
                    window.setProject(project);
                    window.setCorrectionMode(true);
                    window.selectNote(0, false);
                    const auto before =
                        numberedCompositionToJson(window.project_.score, *window.project_.numberedComposition);
                    applyRest(1);
                    bool durationChecked = false;
                    for (int ticks : {960, 480, 240, 120, 60, 30})
                        durationChecked |= requiredControl<QPushButton>(
                                               &window, qPrintable(QString("editorDuration%1").arg(ticks)))
                                               ->isChecked();
                    check("Whole-measure rest does not retain a shorter duration highlight", !durationChecked);
                    const auto &notes = numberedVoice(*window.project_.numberedComposition, 1).notes;
                    check("Measure rest replaces time without shifting the suffix",
                          notes.size() == 2 && notes[0].degree == 0 && notes[0].durationTicks == 1920 &&
                              notes[1].degree == 3 && notes[1].durationTicks == 480);
                    const auto &other = numberedVoice(*window.project_.numberedComposition, 2).notes;
                    check("Other voices retain their written music",
                          other.size() == 1 && other[0].degree == 5 && other[0].durationTicks == 2400);
                    const auto sound = window.activeAccompanimentPlan();
                    check("Measure rest is silent but keeps later onset",
                          sound.events.size() == 2 && sound.events[0].midiPitch == 67 &&
                              sound.events[0].startTick == 0 && sound.events[1].midiPitch == 64 &&
                              sound.events[1].startTick == 1920 && sound.durationTicks == 2400);
                    click("editorUndo");
                    check("Undo restores the entire composition",
                          numberedCompositionToJson(window.project_.score, *window.project_.numberedComposition) ==
                              before);
                    click("editorRedo");
                    saveProject(folder + "/measure-rest.jpp", window.project_);
                    const auto saved = loadProject(folder + "/measure-rest.jpp");
                    check("Measure rest reopens with other voices intact",
                          saved.numberedComposition && saved.numberedComposition->voices[0].notes[0].degree == 0 &&
                              saved.numberedComposition->voices[1].notes[0].durationTicks == 2400);
                    requiredControl<QTabWidget>(&window, "editorToolTabs")->setCurrentIndex(1);
                    QApplication::processEvents();
                    window.view_->fitWidth();
                    window.view_->verticalScrollBar()->setValue(window.view_->verticalScrollBar()->minimum());
                    check("Native measure-rest editor screenshot",
                          window.grab().save(folder + "/measure-rest-editor.png"));
                    Project crossing = project;
                    Note held;
                    held.durationTicks = 4800;
                    held.tieToNext = true;
                    held.lyric = "held";
                    crossing.score.notes = {held, Note{}};
                    lower.durationTicks = 5280;
                    crossing.numberedComposition =
                        NumberedComposition{{{1, "Upper", crossing.score.notes}, {2, "Lower", {lower}}}, 3};
                    window.setProject(crossing);
                    window.setCorrectionMode(true);
                    window.selectNote(0, false);
                    const auto unchanged = projectToJson(window.project_);
                    const int beforeCancel = window.materialUndo_.index();
                    const int cancelCount = applyRest(2, false);
                    check("Target-measure cancellation is atomic",
                          cancelCount == 1 && projectToJson(window.project_) == unchanged &&
                              window.materialUndo_.index() == beforeCancel);
                    const int targetCount = applyRest(2);
                    const auto &split = window.project_.score.notes;
                    check("UI can replace an internal measure of a sustained note",
                          targetCount == 1 && split.size() == 4 && split[0].durationTicks == 1920 &&
                              split[1].degree == 0 && split[1].durationTicks == 1920 &&
                              split[2].durationTicks == 960 && split[2].tieToNext &&
                              split[3].durationTicks == 480);
                    const auto afterRest = window.project_.score;
                    const auto afterHistory = window.materialUndo_.index();
                    applyRest(2);
                    check("Repeated target rest keeps selection without adding history",
                          window.project_.score == afterRest && window.materialUndo_.index() == afterHistory &&
                              window.selected_ == 1);

                    Project wide;
                    wide.generatedNotation = true;
                    wide.score.editorMinimumBars = 1;
                    wide.score.beatsPerBar = 64;
                    wide.score.beatUnit = 1;
                    window.setProject(wide);
                    window.setCorrectionMode(true);
                    const auto original = window.project_.score;
                    const auto historyIndex = window.materialUndo_.index();
                    applyRest(1);
                    check("Oversized rest layout rejects without editing score or history",
                          window.project_.score == original && window.materialUndo_.index() == historyIndex);
                    Project dense;
                    dense.generatedNotation = true;
                    dense.score.editorMinimumBars = 1;
                    dense.score.notes.assign(20, shortNote);
                    window.setProject(dense);
                    window.setCorrectionMode(true);
                    requiredControl<QTabWidget>(&window, "editorToolTabs")->setCurrentIndex(0);
                    window.selectNumberedInput(numberedDuration(window.project_.score));
                    click("editorDuration30");
                    bool rejectedAtomically = false;
                    for (int i = 0; i < 64; ++i)
                    {
                        const auto beforeScore = window.project_.score;
                        const auto beforeImage = window.project_.image;
                        const auto beforeTick = window.numberedInputTick_;
                        const int beforeHistory = window.materialUndo_.index();
                        QKeyEvent noteKey(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier);
                        QApplication::sendEvent(window.view_, &noteKey);
                        if (window.project_.score == beforeScore)
                        {
                            rejectedAtomically =
                                window.project_.image == beforeImage && window.numberedInputTick_ == beforeTick &&
                                window.selected_ == -1 && window.materialUndo_.index() == beforeHistory;
                            break;
                        }
                    }
                    check("Overfull 64th measure preserves score, pixels, cursor and undo", rejectedAtomically);
                }
                Project timed;
                timed.generatedNotation = true;
                timed.score.title = "Structured performance";
                timed.score.editorMinimumBars = 4;
                timed.score.bpm = 120;
                Note held;
                held.durationTicks = 1920;
                timed.score.notes = {held, held, held};
                timed.score.notes.back().durationTicks = 2400;
                Note bass;
                bass.degree = 4;
                bass.durationTicks = 6240;
                timed.numberedComposition =
                    NumberedComposition{{{1, "Melody", timed.score.notes}, {2, "Bass", {bass}}}, 3};
                window.setProject(timed);
                window.setCorrectionMode(true);
                auto *measure = window.findChild<QAbstractButton *>("editorMeasureProperties");
                auto *repeat = window.findChild<QAbstractButton *>("editorMeasureRepeat");
                check("Measure and repeat commands exist", measure && repeat);
                if (measure && repeat)
                {
                    runDialog(measure,
                              [](QDialog *dialog)
                              {
                                  requiredControl<QSpinBox>(dialog, "structureMeasure")->setValue(2);
                                  requiredControl<QSpinBox>(dialog, "structureBeats")->setValue(3);
                                  requiredControl<QDoubleSpinBox>(dialog, "structureTempo")->setValue(60);
                                  auto *key = requiredControl<QComboBox>(dialog, "structureTonic");
                                  key->setCurrentIndex(key->findData(2));
                                  dialog->accept();
                              });
                    if (!window.project_.numberedComposition ||
                        window.project_.numberedComposition->structure.measures.size() < 4)
                        throw std::runtime_error("Measure dialog did not commit the structured fixture");
                    check("Local tempo integrates from measure boundary",
                          std::abs(window.timeline_.secondsAtTick(3360) - 5) < 1e-8);
                    runDialog(repeat,
                              [](QDialog *dialog)
                              {
                                  requiredControl<QSpinBox>(dialog, "repeatFirst")->setValue(1);
                                  requiredControl<QSpinBox>(dialog, "repeatLast")->setValue(3);
                                  auto *ending = requiredControl<QComboBox>(dialog, "repeatEnding");
                                  ending->setCurrentIndex(ending->findData(3));
                                  dialog->accept();
                              });
                    check("Repeat skips first ending on second pass",
                          window.timeline_.durationTicks == 9600 &&
                              std::abs(window.timeline_.durationSeconds() - 16) < 1e-8);
                    const auto sound = window.activeAccompanimentPlan();
                    check("Both voices follow the same repeat traversal",
                          sound.durationTicks == 9600 &&
                              std::any_of(sound.events.begin(), sound.events.end(),
                                          [](const auto &event)
                                          {
                                              return event.midiPitch == 65 && event.startTick == 0 &&
                                                     event.durationTicks == 4800;
                                          }) &&
                              std::any_of(sound.events.begin(), sound.events.end(), [](const auto &event)
                                          { return event.midiPitch == 62 && event.startTick == 1920; }));
                    check("Transport shows the edited performed duration immediately",
                          window.time_->text().endsWith("00:16"));
                    window.resize(1320, 940);
                    requiredControl<QTabWidget>(&window, "editorToolTabs")->setCurrentIndex(1);
                    QApplication::processEvents();
                    window.view_->fitWidth();
                    window.view_->verticalScrollBar()->setValue(window.view_->verticalScrollBar()->minimum());
                    check("Structured score and controls render",
                          window.grab().save(folder + "/structured-editor.png"));
                    saveProject(folder + "/structured.jpp", window.project_);
                    const auto arrangementReport = folder + "/structured-arrangement.json";
                    QProcess cli;
                    cli.start(QApplication::applicationFilePath(),
                              {folder + "/structured.jpp", "--generate-accompaniment", "--language", "en_US",
                               "--report", arrangementReport});
                    if (!cli.waitForFinished(15000) || cli.exitStatus() != QProcess::NormalExit ||
                        cli.exitCode() != 0)
                        throw std::runtime_error("Structured accompaniment CLI probe failed");
                    QFile arrangementFile(arrangementReport);
                    if (!arrangementFile.open(QIODevice::ReadOnly))
                        throw std::runtime_error("Structured accompaniment CLI report is missing");
                    const auto arrangement = QJsonDocument::fromJson(arrangementFile.readAll()).object();
                    const auto practice = arrangement.value("practice").toObject();
                    check("CLI uses a complete written guide for legacy arrangement previews",
                          arrangement.value("durationTicks").toInt() == 9600 && practice.value("valid").toBool() &&
                              practice.value("writtenPreviewOnly").toBool() &&
                              !practice.value("events").toArray().isEmpty());
                    auto reopened = loadProject(folder + "/structured.jpp");
                    check("Structure roundtrips in version 8",
                          projectToJson(reopened).value("schemaVersion").toInt() == 8);
                    window.setProject(reopened);
                    check("Reopened score keeps performed clock",
                          std::abs(window.timeline_.durationSeconds() - 16) < 1e-8);
                    const auto waitFor = [](auto predicate, int timeout)
                    {
                        QEventLoop loop;
                        QElapsedTimer elapsed;
                        QTimer poll;
                        elapsed.start();
                        QObject::connect(&poll, &QTimer::timeout, &loop,
                                         [&]
                                         {
                                             if (predicate() || elapsed.elapsed() >= timeout)
                                                 loop.quit();
                                         });
                        poll.start(10);
                        loop.exec();
                        return predicate();
                    };
                    if (!args.isSet("diagnostic-no-audio"))
                    {
                        window.player_.setPracticeMix({true, true, 0.18, 0.18});
                        window.seekPracticeTick(1800);
                        window.togglePlayback();
                        check("Audio backend starts and crosses the local tempo boundary",
                              waitFor(
                                  [&]
                                  {
                                      return !window.audioLoading_ && window.player_.isPlaying() &&
                                             window.player_.positionTicks() >= 2040;
                                  },
                                  3500));
                        checks.append(QJsonObject{{"name", "Audio output device"},
                                                  {"device", window.player_.deviceName()},
                                                  {"error", window.player_.errorString()},
                                                  {"passed", window.player_.isPlaying()}});
                        if (window.player_.isPlaying())
                        {
                            window.togglePlayback();
                            const auto paused = window.player_.positionTicks();
                            waitFor([] { return false; }, 120);
                            check("Pause freezes position after a tempo change",
                                  !window.player_.isPlaying() && window.player_.positionTicks() == paused);
                            window.position_->setValue(4920);
                            window.position_->sliderReleased();
                            window.updatePlayback();
                            check("Seek uses performed time on the second repeat pass",
                                  window.player_.positionTicks() == 4920 &&
                                      window.time_->text().startsWith("00:08") && window.playingVerse_ == 1);
                            window.seekPracticeTick(1920);
                            click("practiceLoopStart");
                            window.seekPracticeTick(2160);
                            click("practiceLoopEnd");
                            window.loop_->setChecked(true);
                            window.seekPracticeTick(2100);
                            window.togglePlayback();
                            check("Live loop returns to the specified performed range and keeps playing",
                                  waitFor(
                                      [&]
                                      {
                                          return !window.audioLoading_ && window.player_.isPlaying() &&
                                                 window.player_.positionTicks() >= 1920 &&
                                                 window.player_.positionTicks() < 2080;
                                      },
                                      2500));
                            window.player_.stop();
                            window.playIntent_ = false;
                            window.loop_->setChecked(false);
                        }
                    }
                    bool rangePicker = false;
                    int rangeStart = -1;
                    int rangeEnd = -1;
                    runDialog(
                        repeat,
                        [&](QDialog *dialog)
                        {
                            auto *range = requiredControl<QListWidget>(dialog, "repeatRange");
                            rangePicker = range != nullptr;
                            if (range)
                            {
                                const auto clickMeasure = [range](int index, Qt::KeyboardModifiers modifiers)
                                {
                                    auto *viewport = range->viewport();
                                    const auto point = range->visualItemRect(range->item(index)).center();
                                    const auto global = viewport->mapToGlobal(point);
                                    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton,
                                                      Qt::LeftButton, modifiers);
                                    QApplication::sendEvent(viewport, &press);
                                    QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton,
                                                        Qt::NoButton, modifiers);
                                    QApplication::sendEvent(viewport, &release);
                                };
                                clickMeasure(1, Qt::NoModifier);
                                clickMeasure(3, Qt::ShiftModifier);
                                rangeStart = requiredControl<QSpinBox>(dialog, "repeatFirst")->value();
                                rangeEnd = requiredControl<QSpinBox>(dialog, "repeatLast")->value();
                                auto *ending = requiredControl<QComboBox>(dialog, "repeatEnding");
                                rangePicker = dialog->grab().save(folder + "/repeat-range.png") &&
                                              rangeStart == 2 && rangeEnd == 4 && ending->count() == 3 &&
                                              ending->findData(0) >= 0 && ending->findData(2) < 0 &&
                                              ending->findData(3) >= 0 && ending->findData(4) >= 0;
                            }
                            dialog->reject();
                        });
                    check("Visual range selection updates both endpoints and cancelling preserves playback",
                          rangePicker && window.timeline_.durationSeconds() == 16);
                    if (!rangePicker)
                    {
                        auto failure = checks.last().toObject();
                        failure.insert("start", rangeStart);
                        failure.insert("end", rangeEnd);
                        checks[checks.size() - 1] = failure;
                    }
                    window.selectNote(1, false);
                    runDialog(repeat,
                              [](QDialog *dialog)
                              {
                                  requiredControl<QSpinBox>(dialog, "repeatFirst")->setValue(2);
                                  requiredControl<QSpinBox>(dialog, "repeatLast")->setValue(3);
                                  auto *ending = requiredControl<QComboBox>(dialog, "repeatEnding");
                                  ending->setCurrentIndex(ending->findData(3));
                                  dialog->accept();
                              });
                    const auto &editedRepeats = window.project_.numberedComposition->structure.repeats;
                    const bool moved =
                        editedRepeats.size() == 1 && std::abs(window.timeline_.durationSeconds() - 14) < 1e-8;
                    check("Changing an existing repeat start replaces the range rather than adding an overlap",
                          moved);
                    if (moved)
                        click("editorUndo");
                    bool removed = false;
                    int removeFirst = -1;
                    int removeLast = -1;
                    window.selectNote(1, false);
                    runDialog(repeat,
                              [&](QDialog *dialog)
                              {
                                  removeFirst = requiredControl<QSpinBox>(dialog, "repeatFirst")->value();
                                  removeLast = requiredControl<QSpinBox>(dialog, "repeatLast")->value();
                                  auto *remove = requiredControl<QPushButton>(dialog, "repeatRemove");
                                  if (remove && remove->isEnabled())
                                  {
                                      removed = true;
                                      remove->click();
                                  }
                                  else
                                      dialog->reject();
                              });
                    check("Opening inside an existing repeat offers explicit removal",
                          removed && window.project_.numberedComposition->structure.repeats.empty() &&
                              window.timeline_.durationSeconds() == 11);
                    if (!removed)
                    {
                        auto result = checks.last().toObject();
                        result.insert("start", removeFirst);
                        result.insert("end", removeLast);
                        result.insert("inputTick", double(window.numberedInputTick_));
                        result.insert("selectedNote", window.selected_);
                        checks[checks.size() - 1] = result;
                    }
                    if (removed)
                    {
                        click("editorUndo");
                        check("Undo restores the removed repeat and performed duration",
                              window.project_.numberedComposition->structure.repeats.size() == 1 &&
                                  window.timeline_.durationSeconds() == 16);
                    }
                    for (int bpm : {90, 0})
                    {
                        runDialog(measure,
                                  [bpm](QDialog *dialog)
                                  {
                                      requiredControl<QSpinBox>(dialog, "structureMeasure")->setValue(3);
                                      requiredControl<QDoubleSpinBox>(dialog, "structureTempo")->setValue(bpm);
                                      dialog->accept();
                                  });
                        const auto &spec = window.project_.numberedComposition->structure.measures[2];
                        check("Tempo-only edit leaves meter and key inherited",
                              spec.beats == 0 && spec.unit == 0 && spec.tonic == -1 && spec.bpm == bpm);
                    }
                    check("Clearing a tempo override restores inherited clock",
                          std::abs(window.timeline_.durationSeconds() - 16) < 1e-8);
                    auto high = window.project_;
                    high.score.tonic = 11;
                    auto &composition = *high.numberedComposition;
                    composition.structure.measures.front().tonic = 0;
                    auto &firstNote = composition.voices.front().notes.front();
                    firstNote.degree = 7;
                    firstNote.octave = 4;
                    auto &lowerNote = composition.voices[1].notes.front();
                    lowerNote.degree = 7;
                    lowerNote.octave = 4;
                    lowerNote.accidental = 2;
                    NumberedScoreLayout highLayout;
                    high.image = renderNumberedComposition(high.score, composition, composition.voices.front().id,
                                                           highLayout);
                    high.practiceSettings = ProjectPracticeSettings{};
                    saveProject(folder + "/local-key.jpp", high);
                    const auto highReloaded = loadProject(folder + "/local-key.jpp");
                    check("A legal local pitch survives layout and schema 8 reload with practice settings",
                          compositionTimeline(highReloaded.score, *highReloaded.numberedComposition)
                                      .events.front()
                                      .midiPitch == 119 &&
                              highReloaded.score.notes.front().keyOverride == -1);
                    window.setProject(highReloaded);
                    OptionsContext options;
                    options.score = window.project_.score;
                    options.mix = window.project_.practiceMix;
                    options.speed = 1.25;
                    options.transpose = 2;
                    auto settings = window.settings_;
                    settings.audioBackend = 1;
                    check("Settings accept the local key and a legal transpose",
                          window.applyOptions(settings, options) && window.player_.speed() == 1.25 &&
                              window.player_.transpose() == 2);
                    options.speed = 1.5;
                    options.transpose = 8;
                    bool rejected = false;
                    try
                    {
                        window.applyOptions(settings, options);
                    }
                    catch (const std::exception &)
                    {
                        rejected = true;
                    }
                    check("Transpose overflow in the inactive voice rejects the full settings edit",
                          rejected && window.player_.speed() == 1.25 && window.player_.transpose() == 2);
                    auto *recover = window.findChild<QAction *>("menuRecoverNumberedDraft");
                    check("File menu exposes authored numbered draft recovery", recover != nullptr);
                    if (recover)
                    {
                        window.setProject(reopened);
                        QTemporaryDir draftFixture(folder + "/draft-XXXXXX");
                        if (!draftFixture.isValid())
                            throw std::runtime_error("Could not create an isolated draft fixture");
                        const auto draftDirectory = draftFixture.path();
                        const auto oldDraftDirectory = qgetenv("SINGLILT_NUMBERED_DRAFT_DIRECTORY");
                        const auto restoreDraftDirectory = qScopeGuard(
                            [&]
                            {
                                if (oldDraftDirectory.isNull())
                                    qunsetenv("SINGLILT_NUMBERED_DRAFT_DIRECTORY");
                                else
                                    qputenv("SINGLILT_NUMBERED_DRAFT_DIRECTORY", oldDraftDirectory);
                            });
                        qputenv("SINGLILT_NUMBERED_DRAFT_DIRECTORY", draftDirectory.toUtf8());
                        const auto source = folder + "/structured.jpp";
                        window.projectPath_ = source;
                        QFile saved(source);
                        if (!saved.open(QIODevice::ReadOnly))
                            throw std::runtime_error("Draft source fixture could not be read");
                        const auto originalBytes = saved.readAll();
                        saved.close();
                        window.project_.score.title = "Recovered numbered draft";
                        window.markModified();
                        const auto files = [&]
                        { return QDir(draftDirectory).entryList({"numbered-draft-*.jpp"}, QDir::Files); };
                        check("Committed editor changes create a deferred recovery package",
                              waitFor([&] { return files().size() == 1; }, 2500));
                        if (!saved.open(QIODevice::ReadOnly))
                            throw std::runtime_error("Draft source fixture could not be reopened");
                        check("Recovery does not change the manually saved project",
                              saved.readAll() == originalBytes);
                        saved.close();
                        {
                            MainWindow other(window.languageManager_, window.themes_);
                            other.setProject(Project{});
                            requiredControl<QAction>(&other, "menuRecoverNumberedDraft")->trigger();
                            check("Another window cannot claim an active draft",
                                  !other.hasUnsavedChanges() && !other.project_.numberedComposition);
                        }
                        const auto lockPath = window.numberedDraftPath_ + ".lock";
                        QFile lockFile(lockPath);
                        if (!lockFile.open(QIODevice::ReadOnly))
                            throw std::runtime_error("Active draft lock fixture could not be read");
                        auto lockBytes = lockFile.readAll();
                        lockFile.close();
                        window.setProject(Project{});
                        // Recreate the lock a terminated owner would leave, using a real exited process ID.
                        QProcess exitedOwner;
                        exitedOwner.start(QApplication::applicationFilePath(), {"--version"});
                        if (!exitedOwner.waitForStarted())
                            throw std::runtime_error("Stale-lock fixture process did not start");
                        const auto pid = exitedOwner.processId();
                        if (!pid || !exitedOwner.waitForFinished() || exitedOwner.exitCode() != 0 ||
                            lockBytes.indexOf('\n') <= 0)
                            throw std::runtime_error("Stale-lock fixture process did not exit normally");
                        lockBytes.replace(0, lockBytes.indexOf('\n'), QByteArray::number(pid));
                        if (!lockFile.open(QIODevice::WriteOnly) || lockFile.write(lockBytes) != lockBytes.size())
                            throw std::runtime_error("Stale draft lock fixture could not be written");
                        lockFile.close();
                        const auto invalidPath = draftDirectory + "/numbered-draft-invalid.jpp";
                        QFile invalid(invalidPath);
                        if (!invalid.open(QIODevice::WriteOnly) || invalid.write("invalid") != 7)
                            throw std::runtime_error("Corrupt draft fixture could not be created");
                        invalid.close();
                        recover->trigger();
                        if (!invalid.remove())
                            throw std::runtime_error("Corrupt draft fixture could not be removed");
                        check("Recovery skips a corrupt candidate and reclaims a dead owner lock with both voices "
                              "and repeat timing",
                              window.project_.score.title == "Recovered numbered draft" &&
                                  !window.project_.processing.contains("numberedDraft") &&
                                  window.projectPath_.isEmpty() && window.hasUnsavedChanges() &&
                                  window.project_.numberedComposition &&
                                  window.project_.numberedComposition->voices.size() == 2 &&
                                  window.timeline_.durationSeconds() == 16);
                        window.projectPath_ = folder + "/draft-saved.jpp";
                        window.save();
                        check("Successful explicit save retires the recovered snapshot",
                              !window.hasUnsavedChanges() && files().empty() && !QFileInfo::exists(lockPath) &&
                                  QFileInfo::exists(window.projectPath_));
                        window.setProject(reopened);
                        window.project_.score.title = "Discarded numbered draft";
                        window.markModified();
                        window.saveNumberedDraft();
                        const bool draftWasPresent = files().size() == 1;
                        QTimer::singleShot(
                            0, &window,
                            []
                            {
                                if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                                    message->button(QMessageBox::Discard)->click();
                            });
                        const bool discarded = window.confirmDiscard();
                        window.setProject(Project{});
                        check("Explicit discard retires the numbered draft without recreating it",
                              draftWasPresent && discarded && files().empty());
                        {
                            MainWindow teardown(window.languageManager_, window.themes_);
                            teardown.setProject(reopened);
                            teardown.project_.score.title = "Draft on teardown";
                            teardown.markModified();
                        }
                        const auto pendingDrafts = files();
                        check("Destruction flushes a pending committed draft without a close event",
                              pendingDrafts.size() == 1 &&
                                  loadProject(QDir(draftDirectory).filePath(pendingDrafts.front())).score.title ==
                                      "Draft on teardown");
                    }
                }
            }
            catch (const std::exception &error)
            {
                passed = false;
                checks.append(QJsonObject{{"error", QString::fromUtf8(error.what())}});
            }
            writeJsonReport(args.value("report"), {{"passed", passed}, {"checks", checks}});
            QTextStream(stdout) << "NUMBERED_RHYTHM passed=" << passed << " checks=" << checks.size() << Qt::endl;
            app.exit(passed ? 0 : 3);
        });
}
} // namespace singlilt

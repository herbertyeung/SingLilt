// Numbered editor creation, input, history and persistence regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "NumberedEditorCheck.h"
#include "cli/JsonReport.h"
#include "domain/NumberedEditing.h"
#include "storage/ProjectStore.h"
#include "storage/NumberedCompositionStore.h"
#include <QComboBox>
#include <QInputDialog>
#include <QEventLoop>
#include <QMouseEvent>
#include "ui/MainWindow.h"
#include "ui/WorkspaceHeader.h"
#include "ui/ThemeManager.h"
#include "ui/NumberedEditorToolbar.h"
#include "ui/NewScoreDialog.h"
#include "ui/ScoreView.h"
#include <QAction>
#include <QAbstractButton>
#include <QLabel>
#include <QToolButton>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QWheelEvent>
#include <QScrollBar>
#include <QLineF>
#include <limits>
#include <cmath>
#include <QApplication>
#include <QCommandLineParser>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>
#include <algorithm>

namespace singlilt
{
void runNumberedEditorCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        100, &window,
        [&window, &args, &app]
        {
            QJsonArray checks;
            const auto folder = QFileInfo(args.value("report")).absolutePath();
            QDir().mkpath(folder);
            auto check = [&checks](const char *name, bool passed)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", passed}});
                if (!passed)
                    throw std::runtime_error(name);
            };
            auto click = [&window](const char *name)
            {
                auto *button = window.findChild<QAbstractButton *>(name);
                if (!button)
                    throw std::runtime_error(name);
                button->click();
            };
            auto key = [&window](int code)
            {
                QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
                QApplication::sendEvent(window.view_, &event);
            };
            bool passed = true;
            try
            {
                const auto original = scoreToJson(window.project_.score);
                QTimer::singleShot(0, &window,
                                   []
                                   {
                                       if (auto *dialog =
                                               qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                                           dialog->reject();
                                   });
                window.findChild<QAction *>("menuNew")->trigger();
                check("Cancel new leaves current score intact", original == scoreToJson(window.project_.score));
                QTimer::singleShot(
                    0, &window,
                    [&folder]
                    {
                        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                        if (!dialog)
                            return;
                        dialog->findChild<QLineEdit *>("editorTitle")->setText(QStringLiteral("春日练习"));
                        dialog->findChild<QLineEdit *>("editorSubtitle")
                            ->setText(QStringLiteral("简谱创作 · 四小节起步"));
                        dialog->findChild<QLineEdit *>("editorComposer")->setText(QStringLiteral("示例作者"));
                        dialog->grab().save(folder + "/new-score.png");
                        dialog->accept();
                    });
                window.findChild<QAction *>("menuNew")->trigger();
                check("New creates four empty bars at 120 BPM", window.project_.score.notes.empty() &&
                                                                    window.project_.score.editorMinimumBars == 4 &&
                                                                    window.project_.score.bpm == 120);
                check("Empty score has 16 input slots", window.numberedLayout_.slots.size() == 16);
                check("New activates authoring workspace", window.numberedEditorActive());
                check("Header owns shared undo and redo actions",
                      window.findChild<QToolButton *>("editorUndo")->defaultAction() == window.undoAction_ &&
                          window.findChild<QToolButton *>("editorRedo")->defaultAction() == window.redoAction_);
                check("Authoring hides legacy context row and navigation buttons",
                      window.scoreContextToolbar_->isHidden() &&
                          !window.findChild<QPushButton *>("songPracticeTask"));
                auto *toolTabs = window.findChild<QTabWidget *>("editorToolTabs");
                check("Panel controls share the category tab row",
                      toolTabs->cornerWidget(Qt::TopRightCorner) ==
                          window.findChild<QWidget *>("editorPanelToggles"));

                window.editorInspectorTabs_->setCurrentIndex(0);
                window.resize(1320, 940);
                QApplication::processEvents();
                window.view_->fitWidth();
                check("Empty workspace screenshot", window.grab().save(folder + "/empty-editor.png"));
                key(Qt::Key_1);
                key(Qt::Key_2);
                key(Qt::Key_3);
                check("Numeric entry advances without overwriting",
                      window.project_.score.notes.size() == 3 && window.project_.score.notes.back().degree == 3);
                click("editorDuration240");
                key(Qt::Key_4);
                check("Duration applies to new notes", window.project_.score.notes.back().durationTicks == 240);
                window.selectNote(0, false);
                click("piano_1_5_1");
                check("Piano changes selected pitch", window.project_.score.notes[0].degree == 5 &&
                                                          window.project_.score.notes[0].octave == 1 &&
                                                          window.project_.score.notes[0].accidental == 1);
                click("editorUndo");
                check("Undo restores pitch", window.project_.score.notes[0].degree == 1);
                click("editorRedo");
                check("Redo restores pitch", window.project_.score.notes[0].degree == 5);
                QTimer::singleShot(
                    0, &window,
                    [&folder]
                    {
                        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                        if (!dialog)
                            return;
                        dialog->findChild<QPlainTextEdit *>("numberedLyricsText")
                            ->setPlainText(QStringLiteral("(春风)%_来"));
                        dialog->grab().save(folder + "/lyrics.png");
                        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
                    });
                click("editorLyrics");
                check("Lyrics grouping blank and extension", window.project_.score.notes[0].lyric == "春风" &&
                                                                 window.project_.score.notes[1].lyric.empty() &&
                                                                 window.project_.score.notes[2].lyric == "—" &&
                                                                 window.project_.score.notes[3].lyric == "来");
                auto *title = window.findChild<QLineEdit *>("editorTitle");
                title->setText(QStringLiteral("春日新作"));
                QMetaObject::invokeMethod(title, "editingFinished");
                check("Properties edit reaches score", window.project_.score.title == "春日新作");
                const auto countBefore = window.project_.score.notes.size();
                QKeyEvent textKey(QEvent::KeyPress, Qt::Key_7, Qt::NoModifier, "7");
                QApplication::sendEvent(title, &textKey);
                check("Text input does not enter a note", window.project_.score.notes.size() == countBefore);
                title->setText(QStringLiteral("春日新作"));
                click("editorAddBars");
                check("Add four measures", numberedBarCount(window.project_.score) == 8);
                click("editorUndo");
                check("Undo restores blank layout", numberedBarCount(window.project_.score) == 4);
                window.selectNumberedInput(1920);
                key(Qt::Key_6);
                check("Skipped beats become rests", numberedDuration(window.project_.score) == 2160 &&
                                                        window.project_.score.notes[4].degree == 0);
                window.selectNote(0, false);
                click("editorBarBefore");
                check("Measure insertion shifts melody", window.project_.score.notes[0].degree == 0 &&
                                                             window.project_.score.notes[0].durationTicks == 1920);
                click("editorUndo");
                const auto path = folder + "/roundtrip.jpp";
                saveProject(path, window.project_);
                const auto loaded = loadProject(path);
                check("Save reopen preserves metadata and notes",
                      scoreToJson(loaded.score) == scoreToJson(window.project_.score));
                auto invalid = scoreToJson(loaded.score);
                invalid.insert("editorMinimumBars", -1);
                bool rejected = false;
                try
                {
                    scoreFromJson(invalid);
                }
                catch (const std::exception &)
                {
                    rejected = true;
                }
                check("Malformed layout rejected", rejected);
                QApplication::processEvents();
                window.view_->fitWidth();
                check("Edited screenshot", window.grab().save(folder + "/editor.png"));
                check("Shared document shortcuts retained",
                      window.undoAction_->shortcut() == QKeySequence::Undo &&
                          window.findChild<QAction *>("menuNew")->shortcut() == QKeySequence::New &&
                          window.findChild<QAction *>("menuSave")->shortcut() == QKeySequence::Save);
                window.editorInspectorTabs_->setCurrentIndex(1);
                const QString committedLyric = window.lyricEdit_->text();
                window.lyricEdit_->setText("Unapplied header test lyric");
                QTimer::singleShot(0, &window,
                                   []
                                   {
                                       auto *message =
                                           qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                                       if (message && message->button(QMessageBox::Cancel))
                                           message->button(QMessageBox::Cancel)->click();
                                   });
                click("practiceModeButton");
                check("Cancelling mode switch preserves note draft and edit selection",
                      window.numberedEditorActive() && window.correctionModeButton_->isChecked() &&
                          !window.practiceModeButton_->isChecked() &&
                          window.lyricEdit_->text() == "Unapplied header test lyric");
                window.lyricEdit_->setText(committedLyric);
                window.editorInspectorTabs_->setCurrentIndex(0);
                click("editorKeyboardToggle");

                click("practiceModeButton");
                check("Preview keeps document commands without old button rows",
                      window.workspaceHeader_->isVisible() && window.editorToolbar_->isHidden() &&
                          window.scoreContextToolbar_->isHidden() && window.keyboardHost_->isHidden() &&
                          window.findChild<QToolButton *>("editorSave")->isVisible());
                const auto previewScore = scoreToJson(window.project_.score);
                key(Qt::Key_5);
                check("Preview blocks score entry", scoreToJson(window.project_.score) == previewScore);
                check("Preview screenshot", window.grab().save(folder + "/preview.png"));
                click("correctionModeButton");
                check("Return to editor restores tools",
                      window.numberedEditorActive() && !window.editorToolbar_->isHidden());
                check("Panel preference survives preview roundtrip", window.keyboardHost_->isHidden());
                click("editorKeyboardToggle");
                const auto theme = window.themes_.mode();
                window.themes_.setMode(ThemeMode::Dark);
                QApplication::processEvents();
                check("Dark editor screenshot", window.grab().save(folder + "/editor-dark.png"));
                const auto materialBeforeZoom = scoreToJson(window.project_.score);
                const bool dirtyBeforeZoom = window.hasUnsavedChanges();
                auto *zoom = window.findChild<QDoubleSpinBox *>("scoreZoomPercent");
                zoom->setValue(200);
                QApplication::processEvents();
                check("Percentage control sets manual zoom",
                      std::abs(window.view_->zoom() - 2) < 1e-6 && !window.view_->isFittingWidth());
                click("scoreZoomIn");
                check("Zoom-in control and displayed percentage agree",
                      std::abs(window.view_->zoom() - 2.3) < 1e-6 && std::abs(zoom->value() - 230) < .1);
                click("scoreZoomOut");
                check("Zoom-out reverses zoom-in", std::abs(window.view_->zoom() - 2) < 1e-6);
                const auto wheel = [&window](QPoint pixelDelta, QPoint angleDelta)
                {
                    const QPoint anchor = window.view_->viewport()->rect().center() + QPoint(40, 25);
                    QWheelEvent event(QPointF(anchor), QPointF(window.view_->viewport()->mapToGlobal(anchor)),
                                      pixelDelta, angleDelta, Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase,
                                      false);
                    QApplication::sendEvent(window.view_->viewport(), &event);
                };
                const QPoint zoomAnchor = window.view_->viewport()->rect().center() + QPoint(40, 25);
                const auto anchorBefore = window.view_->mapToScene(zoomAnchor);
                wheel({}, QPoint(0, 120));
                check("Ctrl-wheel keeps the pointer's score position",
                      std::abs(window.view_->zoom() - 2.3) < 1e-6 &&
                          QLineF(anchorBefore, window.view_->mapToScene(zoomAnchor)).length() < 2);
                const auto wheelZoom = window.view_->zoom();
                wheel({}, {});
                check("Zero wheel delta does not shrink score", window.view_->zoom() == wheelZoom);
                wheel(QPoint(0, 30), {});
                check("Pixel-delta trackpad zoom works", window.view_->zoom() > wheelZoom);
                window.view_->setZoom(50);
                check("Maximum zoom clamps and disables plus",
                      window.view_->zoom() == ScoreView::MaximumZoom &&
                          !window.findChild<QPushButton *>("scoreZoomIn")->isEnabled());
                window.view_->setZoom(.01);
                check("Minimum zoom clamps and disables minus",
                      window.view_->zoom() == ScoreView::MinimumZoom &&
                          !window.findChild<QPushButton *>("scoreZoomOut")->isEnabled());
                window.view_->setZoom(std::numeric_limits<double>::quiet_NaN());
                check("Nonfinite zoom is ignored", window.view_->zoom() == ScoreView::MinimumZoom);
                click("scoreZoomReset");
                check("Actual-size button resets scale", window.view_->zoom() == 1 && zoom->value() == 100);
                QKeyEvent zoomKey(QEvent::KeyPress, Qt::Key_Plus, Qt::ControlModifier);
                QApplication::sendEvent(window.view_, &zoomKey);
                check("Focused Ctrl-plus zooms", std::abs(window.view_->zoom() - 1.15) < 1e-6);
                QKeyEvent resetKey(QEvent::KeyPress, Qt::Key_0, Qt::ControlModifier);
                QApplication::sendEvent(window.view_, &resetKey);
                check("Focused Ctrl-zero resets scale without entering rest",
                      window.view_->zoom() == 1 && scoreToJson(window.project_.score) == materialBeforeZoom);
                zoom->setValue(175);
                click("practiceModeButton");
                QApplication::processEvents();
                click("correctionModeButton");
                QApplication::processEvents();
                check("Manual zoom survives preview roundtrip", std::abs(window.view_->zoom() - 1.75) < 1e-6);
                click("editorPropertiesToggle");
                QApplication::processEvents();
                click("editorPropertiesToggle");
                QApplication::processEvents();
                check("Manual zoom survives inspector resize", std::abs(window.view_->zoom() - 1.75) < 1e-6);
                QImage originalImage(720, 500, QImage::Format_RGB32);
                originalImage.fill(Qt::white);
                window.staffSourceView_->setScore(originalImage, Score{});
                window.staffScoreTabs_->setTabVisible(1, true);
                window.staffScoreTabs_->setCurrentIndex(1);
                QApplication::processEvents();
                zoom->setValue(150);
                check("Zoom controls target the displayed original view",
                      window.staffSourceView_->zoom() == 1.5 && window.view_->zoom() == 1.75);
                window.staffScoreTabs_->setCurrentIndex(0);
                window.staffScoreTabs_->setTabVisible(1, false);
                QApplication::processEvents();
                check("Returning to score restores its independent ratio", zoom->value() == 175);
                window.resize(1100, 800);
                QApplication::processEvents();
                check("Manual zoom survives window resize", window.view_->zoom() == 1.75);
                window.resize(1320, 940);
                QApplication::processEvents();
                check("Zoom does not dirty or mutate score",
                      scoreToJson(window.project_.score) == materialBeforeZoom &&
                          window.hasUnsavedChanges() == dirtyBeforeZoom);
                zoom->setValue(100);
                window.view_->horizontalScrollBar()->setValue(window.view_->horizontalScrollBar()->minimum());
                window.view_->verticalScrollBar()->setValue(window.view_->verticalScrollBar()->minimum());
                check("Zoomed screenshot", window.grab().save(folder + "/zoom-100.png"));
                click("scoreZoomFit");
                QApplication::processEvents();
                check("Fit-width resumes automatic fitting",
                      window.view_->isFittingWidth() && window.view_->horizontalScrollBar()->maximum() == 0);
                check("Zoom controls screenshot", window.grab().save(folder + "/zoom-controls.png"));

                window.themes_.setMode(theme);

                window.resize(980, 700);
                QApplication::processEvents();
                window.view_->fitWidth();
                check("Small window retains requested geometry", window.size() == QSize(980, 700));
                window.view_->verticalScrollBar()->setValue(window.view_->verticalScrollBar()->minimum());
                check("Small screenshot", window.grab().save(folder + "/editor-small.png"));
                auto *header = window.workspaceHeader_;
                check("Document bar remains compact", header->height() <= 60);
                const auto titleBounds =
                    QRect(header->titleLabel()->mapTo(header, QPoint()), header->titleLabel()->size());
                const auto modeBounds =
                    QRect(header->previewButton()->mapTo(header, QPoint()), header->previewButton()->size());
                check("Title and mode controls do not overlap", !titleBounds.intersects(modeBounds));
                check("Save command visible at minimum window width",
                      window.findChild<QToolButton *>("editorSave")->isVisible());
                const QString savedTitle = title->text();
                const QString longTitle(200, QChar(0x6625));
                title->setText(longTitle);
                QMetaObject::invokeMethod(title, "editingFinished");
                QApplication::processEvents();
                check("Long title is elided with full tooltip",
                      header->titleLabel()->text().endsWith(QChar(0x2026)) &&
                          header->titleLabel()->toolTip() == longTitle);
                title->setText(savedTitle);
                QMetaObject::invokeMethod(title, "editingFinished");

                click("editorKeyboardToggle");
                check("Keyboard collapses", window.keyboardHost_->isHidden());
                click("editorKeyboardToggle");
                Project empty;
                empty.generatedNotation = true;
                empty.score.editorMinimumBars = 4;
                empty.score.notes.push_back(Note{});
                window.setProject(empty);
                window.view_->setZoom(1.25);
                QApplication::processEvents();
                check("Deferred document layout does not override a newer manual zoom",
                      window.view_->zoom() == 1.25);
                window.setCorrectionMode(true);
                window.selectNote(0, false);
                click("editorDeleteNote");
                check("Last note deletion restores empty slots",
                      window.project_.score.notes.empty() && window.numberedLayout_.slots.size() == 16);
                saveProject(folder + "/empty.jpp", window.project_);
                check("Empty project roundtrip", loadProject(folder + "/empty.jpp").score.notes.empty());
                Project poly;
                poly.generatedNotation = true;
                poly.score.title = "多声部练习";
                poly.score.editorMinimumBars = 4;
                poly.score.notes = {Note{}, Note{}};
                poly.score.notes[1].degree = 3;
                window.setProject(poly);
                window.setCorrectionMode(true);
                window.view_->setZoom(.6);
                click("voiceAdd");
                check("Add voice creates authoritative composition",
                      window.project_.numberedComposition &&
                          window.project_.numberedComposition->voices.size() == 2 &&
                          window.activeNumberedVoice_ == 2 && window.project_.score.notes.empty());
                click("editorUndo");
                check("Undo first voice addition restores legacy document",
                      !window.project_.numberedComposition && window.project_.score.notes.size() == 2);
                click("editorRedo");
                check("Redo voice addition restores empty input origin", window.project_.numberedComposition &&
                                                                             window.activeNumberedVoice_ == 2 &&
                                                                             window.numberedInputTick_ == 0);
                key(Qt::Key_5);
                key(Qt::Key_6);
                check("Entry targets only the selected voice",
                      numberedVoice(*window.project_.numberedComposition, 1).notes[0].degree == 1 &&
                          numberedVoice(*window.project_.numberedComposition, 2).notes[0].degree == 5);
                window.selectNote(0, false);
                window.findChild<QComboBox *>("numberedDynamics")->setCurrentIndex(2);
                click("editorAccent");
                click("editorStaccato");
                const auto marked = numberedVoice(*window.project_.numberedComposition, 2).notes[0];
                check("Mark tools update selected note",
                      marked.dynamicLevel == 2 && marked.accent && marked.staccato);
                const auto plan = window.activeAccompanimentPlan();
                const auto found = std::find_if(plan.events.begin(), plan.events.end(), [](const auto &event)
                                                { return event.midiPitch == 67 && event.startTick == 0; });
                check("UI marks reach actual playback plan", found != plan.events.end() && found->velocity == 64 &&
                                                                 found->durationTicks == 240 &&
                                                                 plan.events.size() == 4);
                auto *voices = window.findChild<QComboBox *>("numberedVoiceSelector");
                voices->setCurrentIndex(voices->findData(1));
                check("Voice switch restores its own notes and preserves zoom",
                      window.project_.score.notes[0].degree == 1 && std::abs(window.view_->zoom() - .6) < 1e-6);
                const auto &composition = *window.project_.numberedComposition;
                check("Shared measure starts align across voices",
                      std::abs(numberedVoice(composition, 1).notes[0].source.x -
                               numberedVoice(composition, 2).notes[0].source.x) < .1 &&
                          numberedVoice(composition, 2).notes[0].source.y >
                              numberedVoice(composition, 1).notes[0].source.y);
                const auto hit =
                    std::find_if(window.numberedLayout_.notes.begin(), window.numberedLayout_.notes.end(),
                                 [](const auto &note) { return note.voiceId == 2 && note.noteIndex == 0; });
                check("All voice notes have hit identities", hit != window.numberedLayout_.notes.end());
                const auto point = window.view_->mapFromScene(hit->bounds.center());
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(point),
                                  QPointF(window.view_->viewport()->mapToGlobal(point)), Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(window.view_->viewport(), &press);
                check("Canvas hit selects voice and note",
                      window.activeNumberedVoice_ == 2 && window.selected_ == 0);
                QTimer::singleShot(0, &window,
                                   []
                                   {
                                       if (auto *dialog =
                                               qobject_cast<QInputDialog *>(QApplication::activeModalWidget()))
                                       {
                                           dialog->setTextValue("低声部");
                                           dialog->accept();
                                       }
                                   });
                click("voiceRename");
                click("voiceUp");
                check("Rename and reorder preserve stable identity",
                      window.project_.numberedComposition->voices.front().id == 2 &&
                          window.project_.numberedComposition->voices.front().name == "低声部");
                window.selectNote(0, false);
                QTimer::singleShot(
                    0, &window,
                    []
                    {
                        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                        if (!dialog)
                            return;
                        dialog->findChild<QPlainTextEdit *>("numberedLyricsText")->setPlainText("低音");
                        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
                    });
                click("editorLyrics");
                check("Lyrics are independent per voice",
                      numberedVoice(*window.project_.numberedComposition, 2).notes[0].lyric == "低" &&
                          numberedVoice(*window.project_.numberedComposition, 1).notes[0].lyric.empty());
                check("Only one shared instrument control is shown",
                      !window.programA_->isHidden() && window.programB_->isHidden());
                const auto beforeDelete =
                    numberedCompositionToJson(window.project_.score, *window.project_.numberedComposition);
                QTimer::singleShot(0, &window,
                                   []
                                   {
                                       if (auto *message =
                                               qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                                           message->button(QMessageBox::Cancel)->click();
                                   });
                click("voiceRemove");
                check("Voice deletion cancel preserves material",
                      numberedCompositionToJson(window.project_.score, *window.project_.numberedComposition) ==
                          beforeDelete);
                QTimer::singleShot(0, &window,
                                   []
                                   {
                                       if (auto *message =
                                               qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                                           message->button(QMessageBox::Yes)->click();
                                   });
                click("voiceRemove");
                check("Voice deletion removes only selected part",
                      window.project_.numberedComposition->voices.size() == 1 &&
                          window.project_.numberedComposition->voices[0].id == 1);
                click("editorUndo");
                check("Undo restores full voice and marks",
                      window.activeNumberedVoice_ == 2 &&
                          numberedCompositionToJson(window.project_.score, *window.project_.numberedComposition) ==
                              beforeDelete);
                window.selectNote(0, false);
                click("editorBarBefore");
                check("Measure operation shifts every voice equally",
                      numberedVoice(*window.project_.numberedComposition, 1).notes[0].degree == 0 &&
                          numberedVoice(*window.project_.numberedComposition, 2).notes[0].degree == 0);
                click("editorUndo");
                const auto multiPath = folder + "/composition.jpp";
                saveProject(multiPath, window.project_);
                auto reopened = loadProject(multiPath);
                check("Versioned save reopens all voices and marks",
                      projectToJson(reopened).value("schemaVersion").toInt() == 7 &&
                          numberedCompositionToJson(reopened.score, *reopened.numberedComposition) ==
                              beforeDelete);
                auto malformed = projectToJson(reopened);
                auto payload = malformed.value("numberedComposition").toObject();
                auto entries = payload.value("voices").toArray();
                auto duplicate = entries[1].toObject();
                duplicate.insert("id", entries[0].toObject().value("id"));
                entries[1] = duplicate;
                payload.insert("voices", entries);
                malformed.insert("numberedComposition", payload);
                bool invalidRejected = false;
                try
                {
                    projectFromJson(malformed, reopened.image);
                }
                catch (const std::exception &)
                {
                    invalidRejected = true;
                }
                check("Duplicate persisted voice identity is rejected", invalidRejected);
                window.setProject(reopened);
                window.setCorrectionMode(true);
                voices->setCurrentIndex(voices->findData(1));
                check("Selection is not a document edit", !window.hasUnsavedChanges());
                window.resize(1320, 940);
                window.view_->fitWidth();
                QApplication::processEvents();
                window.view_->verticalScrollBar()->setValue(window.view_->verticalScrollBar()->minimum());
                check("Multi-voice screenshot", window.grab().save(folder + "/voices.png"));
                voices->setCurrentIndex(voices->findData(2));
                window.findChild<QTabWidget *>("editorToolTabs")->setCurrentIndex(4);
                window.selectNote(0, false);
                QApplication::processEvents();
                check("Mark toolbar screenshot", window.grab().save(folder + "/marks.png"));
                window.findChild<QTabWidget *>("editorToolTabs")->setCurrentIndex(2);
                check("Voice toolbar screenshot", window.grab().save(folder + "/voice-tools.png"));
                const auto voicesTheme = window.themes_.mode();
                const auto scorePixels = window.project_.image;
                window.themes_.setMode(ThemeMode::Dark);
                QEventLoop settleTheme;
                QTimer::singleShot(80, &settleTheme, &QEventLoop::quit);
                settleTheme.exec();
                check("Theme changes preserve score pixels", window.project_.image == scorePixels);
                check("Rendered voice score image", window.project_.image.save(folder + "/voices-score.png"));
                check("Dark multi-voice screenshot", window.grab().save(folder + "/voices-dark.png"));
                window.themes_.setMode(voicesTheme);
                window.resize(980, 700);
                QApplication::processEvents();
                check("Multi-voice narrow layout",
                      window.size() == QSize(980, 700) && window.grab().save(folder + "/voices-small.png"));

                click("voiceAdd");
                check("Empty active voice still retains full playback duration",
                      window.project_.score.notes.empty() && window.timeline_.durationTicks == 960 &&
                          window.play_->isEnabled());
            }
            catch (const std::exception &error)
            {
                passed = false;
                checks.append(QJsonObject{{"error", QString::fromUtf8(error.what())}});
            }
            writeJsonReport(args.value("report"), {{"passed", passed}, {"checks", checks}});
            QTextStream(stdout) << "NUMBERED_EDITOR passed=" << passed << " checks=" << checks.size() << Qt::endl;
            app.exit(passed ? 0 : 3);
        });
}
} // namespace singlilt

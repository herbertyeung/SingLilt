// Staff-note source-anchor correction regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffAnchorCorrectionCheck.h"

#include "application/StaffAnchorCorrection.h"
#include "application/StaffScoreCorrection.h"
#include "domain/Timeline.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ScoreView.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
bool sameBox(const SourceRect &left, const SourceRect &right)
{
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}

bool samePixels(const Project &left, const Project &right)
{
    if (left.image != right.image || left.staffPages.size() != right.staffPages.size())
        return false;
    for (std::size_t page = 0; page < left.staffPages.size(); ++page)
        if (left.staffPages[page].sourceImage != right.staffPages[page].sourceImage ||
            left.staffPages[page].renderedImage != right.staffPages[page].renderedImage)
            return false;
    return true;
}

QJsonObject musicWithoutAnchors(const Project &project)
{
    auto score = scoreToJson(project.score);
    auto performance = staffPerformanceToJson(*project.staffPerformance);
    for (auto *object : {&score, &performance})
    {
        QJsonArray notes;
        for (const auto &entry : object->value("notes").toArray())
        {
            auto note = entry.toObject();
            note.remove("bbox");
            note.remove("hasImageAnchor");
            notes.append(note);
        }
        object->insert("notes", notes);
    }
    return {{"score", score}, {"performance", performance}};
}

QJsonObject anchors(const Project &project)
{
    QJsonObject result;
    for (const auto &[name, notes] :
         {std::make_pair("guide", scoreToJson(project.score).value("notes").toArray()),
          std::make_pair("performance",
                         staffPerformanceToJson(*project.staffPerformance).value("notes").toArray())})
    {
        QJsonArray boxes;
        for (const auto &entry : notes)
        {
            const auto note = entry.toObject();
            boxes.append(QJsonObject{{"bbox", note.value("bbox")},
                                     {"hasImageAnchor", note.value("hasImageAnchor")},
                                     {"pageIndex", note.value("pageIndex")}});
        }
        result.insert(name, boxes);
    }
    return result;
}

QJsonObject soundPlan(const Project &project)
{
    const auto plan =
        buildStaffPerformancePlan(project.score, buildTimeline(project.score), *project.staffPerformance);
    if (!plan.valid())
        throw std::runtime_error("The anchor fixture has an invalid complete sounding plan");
    QJsonArray events;
    for (const auto &event : plan.events)
        events.append(QJsonArray{double(event.startTick), double(event.durationTicks), event.midiPitch,
                                 event.velocity, event.role == AccompanimentRole::Chord ? "primary" : "other"});
    QJsonArray diagnostics;
    for (const auto &diagnostic : plan.diagnostics)
        diagnostics.append(QJsonObject{{"message", QString::fromStdString(diagnostic.message)},
                                       {"noteIndex", diagnostic.noteIndex},
                                       {"error", diagnostic.severity == DiagnosticSeverity::Error}});
    return {{"durationTicks", double(plan.durationTicks)}, {"events", events}, {"diagnostics", diagnostics}};
}

bool rejects(const std::function<void()> &operation)
{
    try
    {
        operation();
    }
    catch (const std::exception &)
    {
        return true;
    }
    return false;
}

bool waitUntil(const std::function<bool()> &ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    do
    {
        QApplication::processEvents();
        if (ready())
            return true;
        QThread::msleep(5);
    } while (elapsed.elapsed() < 3000);
    return ready();
}

void mouseAt(ScoreView &view, QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const auto global = view.viewport()->mapToGlobal(position);
    QMouseEvent event(type, QPointF(position), QPointF(global), button, buttons, modifiers);
    QApplication::sendEvent(view.viewport(), &event);
}

Project fixtureProject()
{
    Project project;
    project.staffImagePlayback = true;
    project.notationStyle = NotationStyle::Staff;
    project.processing = {{"local", true},
                          {"recognitionAccuracyMeasured", false},
                          {"sourceAnchorProvenance", "explicit-synthetic-test-bboxes"}};
    project.practiceMix.accompanimentEnabled = true;
    project.score.title = "Anchor-only correction fixture";
    project.score.bpm = 62;
    project.score.writtenMeasures = {{0, 1920, 0, 4, 4, 0}, {1920, 1920, 1, 4, 4, 1}};
    const SourceRect primary{140, 160, 24, 16};
    const SourceRect upper{300, 140, 24, 16};
    for (int index = 0; index < 4; ++index)
    {
        Note note;
        note.id = index;
        note.degree = index == 1 ? 3 : 1;
        note.durationTicks = index < 2 ? 480 : index == 2 ? 960 : 1920;
        note.measure = index == 3 ? 1 : 0;
        note.pageIndex = index == 3 ? 1 : 0;
        note.source = index == 1 ? upper : primary;
        project.score.notes.push_back(note);
    }
    project.score.repeats.push_back({0, 3, 2, -1});
    StaffPerformance performance;
    performance.durationTicks = 3840;
    performance.primaryProgram = 40;
    performance.otherProgram = 35;
    performance.timingFingerprint = staffTimingFingerprint(project.score);
    performance.clefChanges = {{0, 1, false}, {0, 2, true}};
    for (int index = 0; index < 6; ++index)
    {
        StaffPerformanceNote note;
        note.durationTicks = index == 1 ? 480 : 1920;
        note.startTick = index == 1 ? 480 : index == 5 ? 1920 : 0;
        note.midiPitch = index == 1 ? 64 : index == 2 ? 45 : index == 3 ? 52 : index == 4 ? 43 : 60;
        note.staff = index >= 2 && index <= 4 ? 2 : 1;
        note.voice = note.staff == 1 ? "right" : "left";
        note.sourceNoteIndex = index == 0 ? 0 : index == 1 ? 1 : index == 5 ? 3 : -1;
        note.pageIndex = index == 5 ? 1 : 0;
        note.source = index == 1   ? upper
                      : index == 2 ? SourceRect{140, 295, 24, 16}
                      : index == 3 ? SourceRect{140, 270, 24, 16}
                      : index == 4 ? SourceRect{140, 320, 24, 16}
                                   : primary;
        performance.notes.push_back(note);
    }
    project.staffPerformance = performance;
    for (int page = 0; page < 2; ++page)
    {
        QImage image(900, 1200, QImage::Format_RGB32);
        image.fill(page == 0 ? QColor("#FFFEF7") : QColor("#F6FAFF"));
        project.staffPages.push_back(
            {QString("Synthetic original %1").arg(page + 1), image, image, page * 1920, (page + 1) * 1920});
    }
    project.image = project.staffPages.front().sourceImage;
    return project;
}
} // namespace

void runStaffAnchorCorrectionCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        0, &window,
        [&window, &args, &app]
        {
            QJsonArray checks;
            QJsonArray artifacts;
            QJsonObject metrics;
            QString error;
            bool passed = false;
            const auto check = [&](const QString &name, bool valid)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", valid}});
                if (!valid)
                    throw std::runtime_error(name.toStdString());
            };
            try
            {
                const auto folder = QFileInfo(args.value("report")).absolutePath();
                check("Anchor report folder exists", QDir().mkpath(folder));
                const auto original = fixtureProject();
                const auto originalJson = projectToJson(original);
                const auto originalMusic = musicWithoutAnchors(original);
                const auto originalPlan = soundPlan(original);
                const SourceRect moved{230, 190, 24, 16};
                const auto corrected = correctedStaffAnchor(original, 0, moved);
                check("Anchor edit does not mutate the supplied project", projectToJson(original) == originalJson);
                check("Independent primary performance box moves",
                      sameBox(corrected.staffPerformance->notes[0].source, moved));
                check("All split guide aliases move without changing the higher overlapping guide or other page",
                      sameBox(corrected.score.notes[0].source, moved) &&
                          sameBox(corrected.score.notes[2].source, moved) &&
                          sameBox(corrected.score.notes[1].source, original.score.notes[1].source) &&
                          sameBox(corrected.score.notes[3].source, original.score.notes[3].source));
                check("Pitch, ticks, voices, repeats, ties, programs and complete sounding plan remain exact",
                      musicWithoutAnchors(corrected) == originalMusic && soundPlan(corrected) == originalPlan);
                check("Source and displayed page pixels remain exact", samePixels(corrected, original));
                const auto left = correctedStaffAnchor(original, 3, {220, 260, 24, 16});
                check("Left-hand chord member moves independently without moving its siblings or guide",
                      sameBox(left.staffPerformance->notes[3].source, {220, 260, 24, 16}) &&
                          sameBox(left.staffPerformance->notes[2].source,
                                  original.staffPerformance->notes[2].source) &&
                          sameBox(left.staffPerformance->notes[4].source,
                                  original.staffPerformance->notes[4].source) &&
                          scoreToJson(left.score) == scoreToJson(original.score) &&
                          soundPlan(left) == originalPlan);
                auto unanchored = original;
                unanchored.score.notes[0].hasImageAnchor = false;
                unanchored.score.notes[0].source = {};
                const auto partial = correctedStaffAnchor(unanchored, 0, moved);
                check("Missing guide anchor is not guessed while a known split alias is synchronized",
                      !partial.score.notes[0].hasImageAnchor && sameBox(partial.score.notes[0].source, {}) &&
                          sameBox(partial.score.notes[2].source, moved));
                auto shared = original;
                auto duplicate = original.staffPerformance->notes[0];
                duplicate.sourceNoteIndex = -1;
                shared.staffPerformance->notes.push_back(duplicate);
                const auto ambiguous = correctedStaffAnchor(shared, 0, moved);
                check(
                    "Shared-box unison ambiguity changes only the selected full note, not the guide or duplicate",
                    sameBox(ambiguous.staffPerformance->notes[0].source, moved) &&
                        sameBox(ambiguous.staffPerformance->notes.back().source, duplicate.source) &&
                        scoreToJson(ambiguous.score) == scoreToJson(shared.score) &&
                        soundPlan(ambiguous) == soundPlan(shared));
                duplicate.source = {500, 160, 24, 16};
                shared.staffPerformance->notes.back() = duplicate;
                const auto independentUnison = correctedStaffAnchor(shared, 0, moved);
                check("Distinct printed unison boxes do not falsely block a proven existing guide alias",
                      sameBox(independentUnison.score.notes[0].source, moved) &&
                          sameBox(independentUnison.staffPerformance->notes.back().source, duplicate.source));
                const auto repeated = correctedStaffAnchor(corrected, 0, {260, 210, 24, 16});
                const auto records = repeated.processing.value("manualImageAnchors").toArray();
                check("Repeated moves replace the same provenance entry rather than growing an event log",
                      records.size() == 1 && records[0].toObject().value("performanceIndex").toInt() == 0 &&
                          records[0].toObject().value("source").toArray()[0].toDouble() == 260 &&
                          repeated.processing.value("sourceAnchorProvenance") ==
                              original.processing.value("sourceAnchorProvenance"));
                const auto fullCorrection = correctedStaffProject(corrected, corrected.staffPerformance->notes,
                                                                  corrected.score.writtenMeasures);
                const auto retainedAnchor =
                    std::find_if(fullCorrection.staffPerformance->notes.begin(),
                                 fullCorrection.staffPerformance->notes.end(), [](const auto &note)
                                 { return note.pageIndex == 0 && note.staff == 1 && note.midiPitch == 60; });
                check(
                    "Full-note reindexing clears index-based anchor provenance but retains actual original boxes",
                    !fullCorrection.processing.contains("manualImageAnchors") &&
                        retainedAnchor != fullCorrection.staffPerformance->notes.end() &&
                        retainedAnchor->hasImageAnchor && sameBox(retainedAnchor->source, moved) &&
                        samePixels(fullCorrection, original) && soundPlan(fullCorrection) == originalPlan);
                const std::vector<SourceRect> invalidBoxes{{-1, 0, 24, 16},
                                                           {0, 0, 0, 16},
                                                           {890, 0, 24, 16},
                                                           {0, 1190, 24, 16},
                                                           {std::numeric_limits<double>::quiet_NaN(), 0, 24, 16},
                                                           {0, 0, std::numeric_limits<double>::infinity(), 16}};
                for (std::size_t index = 0; index < invalidBoxes.size(); ++index)
                    check(QString("Invalid finite, positive or page-bound box %1 is rejected").arg(index + 1),
                          rejects([&] { correctedStaffAnchor(original, 0, invalidBoxes[index]); }));
                check("Invalid independent performance indices are rejected",
                      rejects([&] { correctedStaffAnchor(original, -1, moved); }) &&
                          rejects(
                              [&]
                              {
                                  correctedStaffAnchor(original, int(original.staffPerformance->notes.size()),
                                                       moved);
                              }));
                auto generated = original;
                generated.staffImagePlayback = false;
                generated.generatedNotation = true;
                check("Generated-score coordinate mode is not accepted as original positioning",
                      rejects([&] { correctedStaffAnchor(generated, 0, moved); }));
                const auto corePath = QDir(folder).filePath("anchor-core-roundtrip.jpp");
                saveProject(corePath, corrected);
                const auto reopened = loadProject(corePath);
                artifacts.append(corePath);
                check("Schema6 save and reopen retain exact anchors, full sound, music and original pixels",
                      anchors(reopened) == anchors(corrected) && musicWithoutAnchors(reopened) == originalMusic &&
                          soundPlan(reopened) == originalPlan && samePixels(reopened, original) &&
                          reopened.processing == corrected.processing);

                const auto providedPath = args.value("staff-anchor-project");
                const auto uiSource = providedPath.isEmpty() ? original : loadProject(providedPath);
                check("UI source uses original-image full-score mode",
                      uiSource.staffImagePlayback && uiSource.staffPerformance);
                metrics.insert("uiSourceKind", providedPath.isEmpty() ? "synthetic" : "provided-project");
                if (!providedPath.isEmpty())
                    metrics.insert("providedProject", QFileInfo(providedPath).absoluteFilePath());
                const auto uiPath = QDir(folder).filePath("anchor-ui-copy.jpp");
                saveProject(uiPath, uiSource);
                artifacts.append(uiPath);
                window.show();
                window.setProject(uiSource, false);
                window.openFile(uiPath);
                check("Production project is ready without recognition",
                      waitUntil([&] { return !window.isAudioLoading() && !window.isRecognizing(); }));
                auto *graphics = window.findChild<QGraphicsView *>("scoreView");
                auto *practiceMode = window.findChild<QPushButton *>("practiceModeButton");
                auto *correctionMode = window.findChild<QPushButton *>("correctionModeButton");
                auto *undo = window.findChild<QAction *>("menuUndo");
                auto *redo = window.findChild<QAction *>("menuRedo");
                auto *save = window.findChild<QAction *>("menuSave");
                auto *programA = window.findChild<QComboBox *>("programA");
                auto *programB = window.findChild<QComboBox *>("programB");
                auto *tempo = window.findChild<QDoubleSpinBox *>("tempo");
                auto *play = window.findChild<QPushButton *>("play");
                auto *stop = window.findChild<QPushButton *>("stop");
                check("Production original-image correction controls exist",
                      graphics && practiceMode && correctionMode && undo && redo && save && programA && programB &&
                          tempo && play && stop);
                auto *view = static_cast<ScoreView *>(graphics);
                practiceMode->click();
                if (auto *pitchDrag = window.findChild<QCheckBox *>("staffPitchDrag"))
                    pitchDrag->setChecked(false);
                if (auto *timingDrag = window.findChild<QCheckBox *>("staffTimingDrag"))
                    timingDrag->setChecked(false);
                correctionMode->click();
                QApplication::processEvents();
                const auto &fullNotes = window.project().staffPerformance->notes;
                const auto selected = std::find_if(fullNotes.begin(), fullNotes.end(),
                                                   [](const auto &note) { return note.hasImageAnchor; });
                check("UI project has an actual original note box", selected != fullNotes.end());
                const int selectedIndex = int(std::distance(fullNotes.begin(), selected));
                const auto originalBox = selected->source;
                window.setStaffPage(selected->pageIndex);
                view->scale(1.25, 1.25);
                const QPointF center(originalBox.x + originalBox.width / 2,
                                     originalBox.y + originalBox.height / 2);
                view->centerOn(center);
                QApplication::processEvents();
                window.player().seek(std::min<std::int64_t>(480, window.timeline().durationTicks / 2));
                const auto beforeGesture = window.project();
                const auto beforePosition = window.player().positionTicks();
                const auto beforePlan = soundPlan(beforeGesture);
                const auto beforeMusic = musicWithoutAnchors(beforeGesture);
                const auto transform = view->transform();
                const int horizontal = view->horizontalScrollBar()->value();
                const int vertical = view->verticalScrollBar()->value();
                const QPoint press = view->mapFromScene(center);
                const auto size = beforeGesture.staffPages[std::size_t(selected->pageIndex)].sourceImage.size();
                const QPoint destination = press + QPoint(center.x() < size.width() / 2 ? 44 : -44,
                                                          center.y() < size.height() / 2 ? 28 : -28);
                check("Production gesture starts and ends within the real viewport",
                      view->viewport()->rect().contains(press) && view->viewport()->rect().contains(destination));
                mouseAt(*view, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
                mouseAt(*view, QEvent::MouseButtonRelease, press, Qt::LeftButton, Qt::NoButton);
                check("Click-only selection is a clean no-op",
                      !window.hasUnsavedChanges() && anchors(window.project()) == anchors(beforeGesture));
                mouseAt(*view, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
                mouseAt(*view, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(view, &escape);
                mouseAt(*view, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
                check("Escape cancels the uncommitted production drag",
                      !window.hasUnsavedChanges() && anchors(window.project()) == anchors(beforeGesture));
                const QPointF delta = view->mapToScene(destination) - view->mapToScene(press);
                const SourceRect expectedBox{
                    std::clamp(originalBox.x + delta.x(), 0.0, size.width() - originalBox.width),
                    std::clamp(originalBox.y + delta.y(), 0.0, size.height() - originalBox.height),
                    originalBox.width, originalBox.height};
                const auto expected = correctedStaffAnchor(beforeGesture, selectedIndex, expectedBox);
                mouseAt(*view, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
                mouseAt(*view, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
                mouseAt(*view, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
                check("MainWindow drag callback commits the exact independent full box and existing guide aliases",
                      anchors(window.project()) == anchors(expected) && window.hasUnsavedChanges() &&
                          undo->isEnabled());
                check("Production drag preserves all sound, music, original pixels and paused transport position",
                      soundPlan(window.project()) == beforePlan &&
                          musicWithoutAnchors(window.project()) == beforeMusic &&
                          samePixels(window.project(), beforeGesture) &&
                          window.player().positionTicks() == beforePosition);
                check("Production drag preserves zoom and both viewport scroll positions",
                      view->transform() == transform && view->horizontalScrollBar()->value() == horizontal &&
                          view->verticalScrollBar()->value() == vertical);
                undo->trigger();
                check("One production Undo restores the gesture and clean state with identical position and plan",
                      anchors(window.project()) == anchors(beforeGesture) && !window.hasUnsavedChanges() &&
                          window.player().positionTicks() == beforePosition &&
                          soundPlan(window.project()) == beforePlan);
                redo->trigger();
                check("Production Redo restores the exact gesture without changing sound or viewport",
                      anchors(window.project()) == anchors(expected) && window.hasUnsavedChanges() &&
                          window.player().positionTicks() == beforePosition &&
                          soundPlan(window.project()) == beforePlan && view->transform() == transform &&
                          view->horizontalScrollBar()->value() == horizontal &&
                          view->verticalScrollBar()->value() == vertical);
                const int laterPrimary = (window.project().staffPerformance->primaryProgram + 1) % 128;
                const int laterOther = (window.project().staffPerformance->otherProgram + 1) % 128;
                const double laterBpm = window.project().score.bpm < 399 ? window.project().score.bpm + 1
                                                                         : window.project().score.bpm - 1;
                programA->setCurrentIndex(programA->findData(laterPrimary));
                programB->setCurrentIndex(programB->findData(laterOther));
                tempo->setValue(laterBpm);
                check("Later explicit GM and tempo choices apply",
                      window.project().staffPerformance->primaryProgram == laterPrimary &&
                          window.project().staffPerformance->otherProgram == laterOther &&
                          window.project().score.bpm == laterBpm &&
                          waitUntil([&] { return !window.isAudioLoading(); }));
                if (window.project().staffPages.size() > 1)
                    window.setStaffPage((window.staffPageIndex() + 1) % int(window.project().staffPages.size()),
                                        false);
                view->scale(1.1, 1.1);
                const auto currentSize =
                    window.project().staffPages[std::size_t(window.staffPageIndex())].sourceImage.size();
                view->centerOn(currentSize.width() / 2.0, currentSize.height() / 2.0);
                QApplication::processEvents();
                const int laterPage = window.staffPageIndex();
                const auto laterTransform = view->transform();
                const int laterHorizontal = view->horizontalScrollBar()->value();
                const int laterVertical = view->verticalScrollBar()->value();
                const auto laterMusic = musicWithoutAnchors(window.project());
                const auto laterPlan = soundPlan(window.project());
                const auto laterPosition = window.player().positionTicks();
                undo->trigger();
                check("Anchor Undo keeps later GM/tempo choices and the currently viewed page and viewport",
                      anchors(window.project()) == anchors(beforeGesture) &&
                          musicWithoutAnchors(window.project()) == laterMusic &&
                          soundPlan(window.project()) == laterPlan &&
                          window.player().positionTicks() == laterPosition &&
                          window.staffPageIndex() == laterPage && view->transform() == laterTransform &&
                          view->horizontalScrollBar()->value() == laterHorizontal &&
                          view->verticalScrollBar()->value() == laterVertical);
                redo->trigger();
                check("Anchor Redo keeps later GM/tempo choices and the currently viewed page and viewport",
                      anchors(window.project()) == anchors(expected) &&
                          musicWithoutAnchors(window.project()) == laterMusic &&
                          soundPlan(window.project()) == laterPlan &&
                          window.player().positionTicks() == laterPosition &&
                          window.staffPageIndex() == laterPage && view->transform() == laterTransform &&
                          view->horizontalScrollBar()->value() == laterHorizontal &&
                          view->verticalScrollBar()->value() == laterVertical);
                metrics.insert("crossPageUndoChecked", beforeGesture.staffPages.size() > 1);
                save->trigger();
                const auto savedUi = loadProject(uiPath);
                check("Production Save writes the moved box and later user settings while preserving original "
                      "pixels",
                      !window.hasUnsavedChanges() && anchors(savedUi) == anchors(expected) &&
                          musicWithoutAnchors(savedUi) == laterMusic && samePixels(savedUi, beforeGesture) &&
                          soundPlan(savedUi) == laterPlan);
                window.setStaffPage(beforeGesture.staffPerformance->notes[std::size_t(selectedIndex)].pageIndex,
                                    false);
                view->centerOn(expectedBox.x + expectedBox.width / 2, expectedBox.y + expectedBox.height / 2);
                const auto screenshot = QDir(folder).filePath("anchor-ui-dragged.png");
                check("Production anchor correction screenshot saves", window.grab().save(screenshot));
                artifacts.append(screenshot);
                check("Production playback backend selects Windows MIDI explicitly",
                      window.player().setAudioBackend(AudioBackend::WindowsMidi));
                play->click();
                check("Starting production playback returns to practice mode",
                      waitUntil(
                          [&]
                          {
                              return window.player().isPlaying() && practiceMode->isChecked() &&
                                     !correctionMode->isChecked();
                          }));
                check("Playback mode transition preserves the moved anchors, original pixels and sounding plan",
                      anchors(window.project()) == anchors(expected) &&
                          samePixels(window.project(), beforeGesture) && soundPlan(window.project()) == laterPlan);
                stop->click();
                metrics.insert("selectedPerformanceIndex", selectedIndex);
                metrics.insert("noteCount", int(beforeGesture.staffPerformance->notes.size()));
                metrics.insert("guideNoteCount", int(beforeGesture.score.notes.size()));
                metrics.insert("soundEventCount", beforePlan.value("events").toArray().size());
                metrics.insert(
                    "evidence",
                    "Core and native Qt input/storage checks; no OMR rerun or acoustic accuracy measurement");
                passed = true;
            }
            catch (const std::exception &exception)
            {
                error = QString::fromUtf8(exception.what());
                window.player().stop();
            }
            QSaveFile report(args.value("report"));
            const auto bytes = QJsonDocument(QJsonObject{{"passed", passed},
                                                         {"checks", checks},
                                                         {"error", error},
                                                         {"metrics", metrics},
                                                         {"artifacts", artifacts},
                                                         {"recognitionAccuracyMeasured", false}})
                                   .toJson();
            const bool saved =
                report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() && report.commit();
            app.exit(saved ? passed ? 0 : 3 : 4);
        });
}
} // namespace singlilt

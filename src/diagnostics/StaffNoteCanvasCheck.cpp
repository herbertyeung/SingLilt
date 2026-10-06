// Original-image note insertion and deletion checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffNoteCanvasCheck.h"

#include "domain/Timeline.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ScoreView.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace singlilt
{
namespace
{
constexpr int AddedPitch = 85;
constexpr const char *AddedVoice = "canvas-check-new";

bool sameBox(const SourceRect &left, const SourceRect &right)
{
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}

QJsonArray imageHashes(const Project &project)
{
    const auto hash = [](const QImage &image)
    {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
            throw std::runtime_error("Cannot encode the original image for the canvas check");
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    };
    QJsonArray hashes{hash(project.image)};
    for (const auto &page : project.staffPages)
        hashes.append(QJsonArray{hash(page.sourceImage), hash(page.renderedImage)});
    return hashes;
}

QJsonArray soundEvents(const Project &project)
{
    const auto plan =
        buildStaffPerformancePlan(project.score, buildTimeline(project.score), *project.staffPerformance);
    if (!plan.valid())
        throw std::runtime_error("The canvas project has an invalid complete sound plan");
    QJsonArray events;
    for (const auto &event : plan.events)
        events.append(QJsonArray{double(event.startTick), double(event.durationTicks), event.midiPitch,
                                 event.velocity, event.role == AccompanimentRole::Chord ? "primary" : "other"});
    return events;
}

int soundingMatches(const QJsonArray &events, int pitch, std::int64_t startTick, std::int64_t durationTicks)
{
    return int(std::count_if(events.begin(), events.end(),
                             [&](const auto &entry)
                             {
                                 const auto event = entry.toArray();
                                 return event[0].toDouble() == double(startTick) &&
                                        event[1].toDouble() == double(durationTicks) && event[2].toInt() == pitch;
                             }));
}

int addedIndex(const Project &project)
{
    for (std::size_t index = 0; index < project.staffPerformance->notes.size(); ++index)
        if (project.staffPerformance->notes[index].midiPitch == AddedPitch &&
            project.staffPerformance->notes[index].voice == AddedVoice)
            return int(index);
    return -1;
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
    } while (elapsed.elapsed() < 10000);
    return ready();
}

void mouseAt(ScoreView &view, QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(position), QPointF(view.viewport()->mapToGlobal(position)), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &event);
}

Project fixtureProject()
{
    Project project;
    project.staffImagePlayback = true;
    project.notationStyle = NotationStyle::Staff;
    project.processing = {{"local", true}, {"fixture", "synthetic original-image note canvas"}};
    project.score.title = "Original-image note canvas fixture";
    project.score.writtenMeasures = {{0, 1920, 0, 4, 4, 0}};
    StaffPerformance performance;
    performance.durationTicks = 1920;
    performance.clefChanges = {{0, 1, false}, {0, 2, true}};
    for (int index = 0; index < 4; ++index)
    {
        const SourceRect box{140.0 + index * 100, 160, 24, 16};
        Note guide;
        guide.id = index;
        guide.degree = index + 1;
        guide.durationTicks = 480;
        guide.source = box;
        guide.hasImageAnchor = true;
        project.score.notes.push_back(guide);
        StaffPerformanceNote note;
        note.midiPitch = index == 0 ? 60 : index == 1 ? 62 : index == 2 ? 64 : 65;
        note.startTick = index * 480;
        note.durationTicks = 480;
        note.staff = 1;
        note.voice = "right";
        note.sourceNoteIndex = index;
        note.source = box;
        note.hasImageAnchor = true;
        performance.notes.push_back(note);
    }
    StaffPerformanceNote bass;
    bass.midiPitch = 43;
    bass.durationTicks = 1920;
    bass.staff = 2;
    bass.voice = "left";
    bass.source = {140, 280, 24, 16};
    bass.hasImageAnchor = true;
    performance.notes.push_back(bass);
    std::stable_sort(performance.notes.begin(), performance.notes.end(),
                     [](const auto &left, const auto &right) { return left.startTick < right.startTick; });
    performance.timingFingerprint = staffTimingFingerprint(project.score);
    project.staffPerformance = performance;
    QImage image(900, 1200, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    for (int line = 0; line < 5; ++line)
    {
        painter.drawLine(80, 130 + line * 10, 820, 130 + line * 10);
        painter.drawLine(80, 250 + line * 10, 820, 250 + line * 10);
    }
    painter.setBrush(Qt::black);
    for (const auto &note : performance.notes)
        painter.drawEllipse(QRectF(note.source.x, note.source.y, note.source.width, note.source.height));
    painter.end();
    project.image = image;
    project.staffPages.push_back({"Synthetic original", image, image, 0, 1920});
    return project;
}

bool finishAddDialog(MainWindow &window, bool accept, int measureIndex, std::int64_t durationTicks,
                     QString &automationError)
{
    auto *dialog = window.findChild<QDialog *>("staffNoteAddDialog");
    if (!dialog)
    {
        automationError = "The real add-note dialog did not open";
        return false;
    }
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    auto *pitch = dialog->findChild<QSpinBox *>("staffNotePitch");
    auto *measure = dialog->findChild<QComboBox *>("staffNoteMeasure");
    auto *onset = dialog->findChild<QLineEdit *>("staffNoteOnset");
    auto *duration = dialog->findChild<QLineEdit *>("staffNoteDuration");
    auto *staff = dialog->findChild<QSpinBox *>("staffNoteStaff");
    auto *voice = dialog->findChild<QLineEdit *>("staffNoteVoice");
    auto *apply = dialog->findChild<QPushButton *>("staffNoteAddApply");
    if (!buttons || !pitch || !measure || !onset || !duration || !staff || !voice || !apply)
    {
        automationError = "The production add-note controls are incomplete";
        dialog->reject();
        return false;
    }
    if (!accept)
    {
        buttons->button(QDialogButtonBox::Cancel)->click();
        return true;
    }
    pitch->setValue(AddedPitch);
    measure->setCurrentIndex(measure->findData(measureIndex));
    onset->setText("0");
    duration->setText(QString::number(double(durationTicks) / TicksPerQuarter, 'g', 15));
    staff->setValue(1);
    voice->setText(AddedVoice);
    apply->click();
    if (dialog->isVisible())
    {
        if (auto *error = dialog->findChild<QLabel *>("staffNoteError"))
            automationError = error->text();
        buttons->button(QDialogButtonBox::Cancel)->click();
        return false;
    }
    return true;
}
} // namespace

void runStaffNoteCanvasCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
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
                check("Canvas report folder exists", QDir().mkpath(folder));
                const auto provided = args.value("staff-note-project");
                const auto source = provided.isEmpty() ? fixtureProject() : loadProject(provided);
                check("Canvas source has original pages and complete local music",
                      source.staffImagePlayback && source.staffPerformance &&
                          source.processing.value("local").toBool() && !source.staffPages.empty() &&
                          !source.score.writtenMeasures.empty() && addedIndex(source) < 0);
                const auto path = QDir(folder).filePath("staff-note-canvas-copy.jpp");
                saveProject(path, source);
                artifacts.append(path);
                window.show();
                window.setProject(source, false);
                window.openFile(path);
                check("Production project is ready without recognition",
                      waitUntil([&] { return !window.isAudioLoading() && !window.isRecognizing(); }));
                auto *graphics = window.findChild<QGraphicsView *>("scoreView");
                auto *view = static_cast<ScoreView *>(graphics);
                auto *practiceMode = window.findChild<QPushButton *>("practiceModeButton");
                auto *correctionMode = window.findChild<QPushButton *>("correctionModeButton");
                auto *add = window.findChild<QPushButton *>("staffImageAddNote");
                auto *remove = window.findChild<QPushButton *>("staffImageDeleteNote");
                auto *undo = window.findChild<QAction *>("menuUndo");
                auto *redo = window.findChild<QAction *>("menuRedo");
                auto *save = window.findChild<QAction *>("menuSave");
                check("Production canvas, toolbar and history controls exist",
                      view && practiceMode && correctionMode && add && remove && undo && redo && save);
                if (auto *pitchDrag = window.findChild<QCheckBox *>("staffPitchDrag"))
                    pitchDrag->setChecked(false);
                if (auto *timingDrag = window.findChild<QCheckBox *>("staffTimingDrag"))
                    timingDrag->setChecked(false);
                correctionMode->click();
                window.setStaffPage(0);
                QApplication::processEvents();
                check("Original correction mode exposes enabled add and no stale delete selection",
                      add->isVisible() && add->isEnabled() && remove->isVisible() && !remove->isEnabled());
                const auto before = window.project();
                const auto images = imageHashes(before);
                const auto beforeSound = soundEvents(before);
                const int beforeCount = int(before.staffPerformance->notes.size());
                const auto &measure = before.score.writtenMeasures.front();
                const auto durationTicks = std::min<std::int64_t>(240, measure.durationTicks);
                const int soundingPitch = AddedPitch + before.score.tonic - before.staffPerformance->sourceTonic;
                const auto &image = before.staffPages.front().sourceImage;
                const QPointF addPoint(image.width() * 0.72, image.height() * 0.15);
                view->centerOn(addPoint);
                QApplication::processEvents();
                const auto position = view->mapFromScene(addPoint);
                check("Real original-image add point lies inside the viewport",
                      view->viewport()->rect().contains(position));
                bool contextChosen = false;
                bool dialogHandled = false;
                QString automationError;
                QTimer dialogTimer;
                dialogTimer.setSingleShot(true);
                QObject::connect(
                    &dialogTimer, &QTimer::timeout, &window,
                    [&] { dialogHandled = finishAddDialog(window, true, 0, durationTicks, automationError); });
                QTimer menuTimer;
                menuTimer.setSingleShot(true);
                QObject::connect(&menuTimer, &QTimer::timeout, &window,
                                 [&]
                                 {
                                     auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                     if (!menu)
                                         menu = view->findChild<QMenu *>();
                                     if (!menu || menu->actions().size() != 1)
                                     {
                                         automationError =
                                             "The original-image context menu did not expose its add action";
                                         if (menu)
                                             menu->close();
                                         return;
                                     }
                                     menu->setActiveAction(menu->actions().front());
                                     contextChosen = true;
                                     dialogTimer.start(25);
                                     QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                                     QApplication::sendEvent(menu, &enter);
                                 });
                menuTimer.start(25);
                QContextMenuEvent context(QContextMenuEvent::Mouse, position,
                                          view->viewport()->mapToGlobal(position));
                QApplication::sendEvent(view->viewport(), &context);
                menuTimer.stop();
                dialogTimer.stop();
                check("Real right-click Add opens and accepts the production note dialog",
                      contextChosen && dialogHandled && automationError.isEmpty());
                int index = addedIndex(window.project());
                check("Add creates exactly one complete anchored performance note",
                      index >= 0 && int(window.project().staffPerformance->notes.size()) == beforeCount + 1 &&
                          window.project().staffPerformance->notes[std::size_t(index)].hasImageAnchor &&
                          window.project().staffPerformance->notes[std::size_t(index)].durationTicks ==
                              durationTicks);
                const auto afterAddSound = soundEvents(window.project());
                check("The requested MIDI pitch and duration enter the actual sounding plan",
                      soundingMatches(afterAddSound, soundingPitch, measure.startTick, durationTicks) >
                          soundingMatches(beforeSound, soundingPitch, measure.startTick, durationTicks));
                check("Add preserves all original and displayed PNG bytes",
                      imageHashes(window.project()) == images);
                const auto addedBox = window.project().staffPerformance->notes[std::size_t(index)].source;
                const QPointF center(addedBox.x + addedBox.width / 2, addedBox.y + addedBox.height / 2);
                view->centerOn(center);
                view->setFocus(Qt::OtherFocusReason);
                QApplication::processEvents();
                const auto press = view->mapFromScene(center);
                const auto destination = press + QPoint(36, 24);
                check("Added note drag endpoints lie inside the actual viewport",
                      view->viewport()->rect().contains(press) && view->viewport()->rect().contains(destination));
                mouseAt(*view, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
                mouseAt(*view, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
                mouseAt(*view, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
                index = addedIndex(window.project());
                const auto movedBox = window.project().staffPerformance->notes.at(std::size_t(index)).source;
                check("Real canvas drag moves the added note without changing its sound or pixels",
                      !sameBox(addedBox, movedBox) && soundEvents(window.project()) == afterAddSound &&
                          imageHashes(window.project()) == images);
                view->setFocus(Qt::OtherFocusReason);
                QApplication::processEvents();
                check("Delete targets the focused original-image canvas",
                      view->hasFocus() || view->viewport()->hasFocus());
                QKeyEvent deletion(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
                QApplication::sendEvent(view, &deletion);
                check("Real Delete removes the selected written note and its actual sound",
                      addedIndex(window.project()) < 0 &&
                          int(window.project().staffPerformance->notes.size()) == beforeCount &&
                          soundEvents(window.project()) == beforeSound && imageHashes(window.project()) == images);
                undo->trigger();
                index = addedIndex(window.project());
                check("Undo Delete restores the moved box and complete added sound",
                      index >= 0 &&
                          sameBox(window.project().staffPerformance->notes[std::size_t(index)].source, movedBox) &&
                          soundEvents(window.project()) == afterAddSound);
                undo->trigger();
                index = addedIndex(window.project());
                check("Undo Drag restores the initial added box without changing music",
                      index >= 0 &&
                          sameBox(window.project().staffPerformance->notes[std::size_t(index)].source, addedBox) &&
                          soundEvents(window.project()) == afterAddSound);
                undo->trigger();
                check("Undo Add restores the original music and original images",
                      addedIndex(window.project()) < 0 && soundEvents(window.project()) == beforeSound &&
                          imageHashes(window.project()) == images);
                redo->trigger();
                index = addedIndex(window.project());
                check("Redo Add restores the written note and sound",
                      index >= 0 &&
                          sameBox(window.project().staffPerformance->notes[std::size_t(index)].source, addedBox) &&
                          soundEvents(window.project()) == afterAddSound);
                redo->trigger();
                index = addedIndex(window.project());
                check("Redo Drag restores the moved box and unchanged sound",
                      index >= 0 &&
                          sameBox(window.project().staffPerformance->notes[std::size_t(index)].source, movedBox) &&
                          soundEvents(window.project()) == afterAddSound);
                redo->trigger();
                check("Redo Delete restores the exact original sounding plan",
                      addedIndex(window.project()) < 0 && soundEvents(window.project()) == beforeSound);
                const auto beforeCancel = projectToJson(window.project());
                bool cancelHandled = false;
                QTimer cancelTimer;
                cancelTimer.setSingleShot(true);
                QObject::connect(
                    &cancelTimer, &QTimer::timeout, &window,
                    [&] { cancelHandled = finishAddDialog(window, false, 0, durationTicks, automationError); });
                cancelTimer.start(25);
                add->click();
                cancelTimer.stop();
                check("Real toolbar Add Cancel leaves project, pixels and music unchanged",
                      cancelHandled && projectToJson(window.project()) == beforeCancel &&
                          imageHashes(window.project()) == images);
                practiceMode->click();
                QApplication::processEvents();
                check("Practice mode disables both original-image mutation buttons",
                      !add->isEnabled() && !remove->isEnabled());
                add->click();
                view->setFocus(Qt::OtherFocusReason);
                QKeyEvent practiceDelete(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
                QApplication::sendEvent(view, &practiceDelete);
                check("Practice-mode Add and Delete input cannot change the project",
                      projectToJson(window.project()) == beforeCancel);
                correctionMode->click();
                undo->trigger();
                save->trigger();
                const auto reopened = loadProject(path);
                check("Actual Save and reopen retain note, moved box, sounding plan and original PNG bytes",
                      projectToJson(reopened) == projectToJson(window.project()) &&
                          imageHashes(reopened) == images && soundEvents(reopened) == afterAddSound &&
                          addedIndex(reopened) >= 0);
                metrics = {{"sourceKind", provided.isEmpty() ? "synthetic" : "provided-project"},
                           {"sourceProject", provided},
                           {"originalNotes", beforeCount},
                           {"addedPitch", AddedPitch},
                           {"addedDurationTicks", double(durationTicks)},
                           {"contextMenuAddTested", contextChosen},
                           {"recognitionRerun", false},
                           {"evidence",
                            "Production Qt controls/events, material undo/redo, sound plan and project storage"}};
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
                                                         {"artifacts", artifacts}})
                                   .toJson();
            const bool saved =
                report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() && report.commit();
            app.exit(saved ? passed ? 0 : 3 : 4);
        });
}
} // namespace singlilt

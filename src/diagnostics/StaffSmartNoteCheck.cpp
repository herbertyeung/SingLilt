// Interactive staff-note inspector and gesture checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffSmartNoteCheck.h"
#include "domain/Timeline.h"
#include "i18n/LanguageManager.h"
#include "recognition/CrispStaffSourceAnchors.h"
#include "storage/StaffEditRecovery.h"
#include "ui/MainWindow.h"
#include "ui/ScoreView.h"
#include "ui/StaffNoteDialog.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace singlilt
{
namespace
{
bool waitUntil(const std::function<bool()> &ready, int milliseconds = 3000)
{
    QElapsedTimer clock;
    clock.start();
    do
    {
        QApplication::processEvents();
        if (ready())
            return true;
        QThread::msleep(5);
    } while (clock.elapsed() < milliseconds);
    return ready();
}

QByteArray pixelHash(const Project &project)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &page : project.staffPages)
    {
        const auto image = page.sourceImage.convertToFormat(QImage::Format_ARGB32);
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()));
    }
    return hash.result().toHex();
}

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Smart-note source file cannot be reopened");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        throw std::runtime_error("Smart-note source file cannot be hashed");
    return hash.result().toHex();
}

QJsonObject material(const Project &project)
{
    auto score = scoreToJson(project.score);
    score.remove("imagePath");
    return {{"score", score},
            {"performance", staffPerformanceToJson(*project.staffPerformance)},
            {"visual", project.processing.value("staffVisualEdits")}};
}

QJsonObject recoverySettings(const Project &project)
{
    const auto json = projectToJson(project);
    return {{"mix", json.value("practiceMix")},
            {"baseVelocity", project.score.baseVelocity},
            {"versePrograms", json.value("versePrograms")},
            {"practice", json.value("practiceSettings")}};
}

QJsonObject visualRecord(const Project &project, const StaffPerformanceNote &note)
{
    const QJsonArray source{note.source.x, note.source.y, note.source.width, note.source.height};
    for (const auto &value : project.processing.value("staffVisualEdits").toArray())
    {
        const auto edit = value.toObject();
        if (edit.value("pageIndex").toInt(-1) == note.pageIndex && edit.value("staff").toInt() == note.staff &&
            edit.value("voice").toString().toStdString() == note.voice &&
            edit.value("startTick").toDouble(-1) == note.startTick &&
            edit.value("midiPitch").toInt(-1) == note.midiPitch && edit.value("source").toArray() == source)
            return edit;
    }
    return {};
}

QJsonArray soundPlan(const Project &project)
{
    const auto plan =
        buildStaffPerformancePlan(project.score, buildTimeline(project.score), *project.staffPerformance);
    if (!plan.valid())
        throw std::runtime_error("Smart-note project has an invalid sounding plan");
    QJsonArray events;
    for (const auto &event : plan.events)
        events.append(
            QJsonArray{double(event.startTick), double(event.durationTicks), event.midiPitch, event.velocity});
    return events;
}

bool soundContains(const Project &project, const StaffPerformanceNote &note)
{
    const int soundingPitch = note.midiPitch + project.score.tonic - project.staffPerformance->sourceTonic;
    for (const auto &value : soundPlan(project))
    {
        const auto event = value.toArray();
        if (event[0].toDouble() == note.startTick && event[1].toDouble() == note.durationTicks &&
            event[2].toInt() == soundingPitch)
            return true;
    }
    return false;
}

int targetIndex(const Project &project, const StaffPerformanceNote &target)
{
    int found = -1;
    for (std::size_t index = 0; index < project.staffPerformance->notes.size(); ++index)
    {
        const auto &note = project.staffPerformance->notes[index];
        if (note.pageIndex == target.pageIndex && note.staff == target.staff && note.voice == target.voice &&
            note.startTick == target.startTick && note.durationTicks == target.durationTicks &&
            std::abs(note.source.x - target.source.x) < 0.01)
        {
            if (found >= 0)
                throw std::runtime_error("Smart-note UI target is not unique");
            found = static_cast<int>(index);
        }
    }
    if (found < 0)
        throw std::runtime_error("Smart-note UI target disappeared");
    return found;
}

bool visualHeadAt(ScoreView &view, const SourceRect &source)
{
    const QPointF center(source.x + source.width / 2, source.y + source.height / 2);
    for (auto *item : view.scene()->items())
        if (item->data(0).toString() == "staffVisualNotehead" && item->sceneBoundingRect().contains(center))
            return true;
    return false;
}

bool visualHeadAbove(ScoreView &view, const SourceRect &source, double spacing)
{
    const double x = source.x + source.width / 2;
    const double y = source.y + source.height / 2;
    for (auto *item : view.scene()->items())
        if (item->data(0).toString() == "staffVisualNotehead")
        {
            const auto center = item->sceneBoundingRect().center();
            if (std::abs(center.x() - x) < 0.1 && center.y() < y - spacing * 0.4)
                return true;
        }
    return false;
}

void mouseAt(ScoreView &view, QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(position), QPointF(view.viewport()->mapToGlobal(position)), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &event);
}

void clickNote(ScoreView &view, const StaffPerformanceNote &note)
{
    const QPointF center(note.source.x + note.source.width / 2, note.source.y + note.source.height / 2);
    view.centerOn(center);
    QApplication::processEvents();
    const QPoint point = view.mapFromScene(center);
    mouseAt(view, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    mouseAt(view, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
}

bool inspectorDisplays(QWidget &editor, const Project &project, const StaffPerformanceNote &note)
{
    const auto *pitch = editor.findChild<QSpinBox *>("staffNotePitch");
    const auto *staff = editor.findChild<QSpinBox *>("staffNoteStaff");
    const auto *voice = editor.findChild<QLineEdit *>("staffNoteVoice");
    const auto *measure = editor.findChild<QComboBox *>("staffNoteMeasure");
    const auto *onset = editor.findChild<QLineEdit *>("staffNoteOnset");
    const auto *duration = editor.findChild<QLineEdit *>("staffNoteDuration");
    if (!pitch || !staff || !voice || !measure || !onset || !duration)
        return false;
    const int index = measure->currentData().toInt();
    if (index < 0 || index >= int(project.score.writtenMeasures.size()))
        return false;
    bool validOnset = false, validDuration = false;
    const double start = project.score.writtenMeasures[std::size_t(index)].startTick +
                         onset->text().toDouble(&validOnset) * TicksPerQuarter;
    const double ticks = duration->text().toDouble(&validDuration) * TicksPerQuarter;
    return editor.isVisible() && pitch->value() == note.midiPitch && staff->value() == note.staff &&
           voice->text().toStdString() == note.voice && validOnset && validDuration &&
           std::abs(start - note.startTick) < 0.000000001 && std::abs(ticks - note.durationTicks) < 0.000000001;
}

void drag(ScoreView &view, const SourceRect &source, double deltaY, double deltaX = 0)
{
    const QPointF center(source.x + source.width / 2, source.y + source.height / 2);
    view.centerOn(center);
    QApplication::processEvents();
    const QPoint from = view.mapFromScene(center);
    const QPoint to = view.mapFromScene(center + QPointF(deltaX, deltaY));
    if (!view.viewport()->rect().contains(from) || !view.viewport()->rect().contains(to))
        throw std::runtime_error("Smart-note drag is outside the actual viewport");
    mouseAt(view, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    mouseAt(view, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    mouseAt(view, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

Project fixtureProject()
{
    Project project;
    project.staffImagePlayback = true;
    project.notationStyle = NotationStyle::Staff;
    project.processing = {{"local", true}, {"recognitionAccuracyMeasured", false}};
    project.score.title = "Smart-note real-raster C-major fixture";
    project.score.bpm = 62;
    project.score.writtenMeasures = {{0, 1920, 0, 4, 4, 0}, {1920, 1920, 1, 4, 4, 0}};
    project.staffPerformance.emplace();
    auto &performance = *project.staffPerformance;
    performance.durationTicks = 3840;
    performance.clefChanges = {{0, 1, false}, {0, 2, true}};
    QImage image(800, 500, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(QPen(Qt::black, 1));
    for (const int top : {120, 300})
        for (int line = 0; line < 5; ++line)
            painter.drawLine(40, top + line * 12, 760, top + line * 12);
    const auto drawHead = [&](double x, double y, bool hollow)
    {
        painter.setPen(QPen(Qt::black, 1.5));
        painter.setBrush(hollow ? Qt::white : Qt::black);
        painter.save();
        painter.translate(x, y);
        painter.rotate(-20);
        painter.drawEllipse(QRectF(-7.5, -4.5, 15, 9));
        painter.restore();
        painter.drawLine(QPointF(x + 7, y), QPointF(x + 7, y - 36));
    };
    static constexpr std::array<int, 8> Pitches{60, 62, 64, 65, 67, 69, 71, 72};
    for (int index = 0; index < 8; ++index)
    {
        const double x = 150 + index * 75;
        const double y = 180 - index * 6;
        if (index == 0)
            painter.drawLine(QPointF(x - 11, y), QPointF(x + 11, y));
        drawHead(x, y, false);
        Note guide;
        guide.id = index;
        guide.degree = index % 7 + 1;
        guide.octave = index / 7;
        guide.durationTicks = 480;
        guide.measure = index / 4;
        guide.source = {x - 7.5, y - 4.5, 15, 9};
        project.score.notes.push_back(guide);
        StaffPerformanceNote note;
        note.startTick = index * 480;
        note.durationTicks = 480;
        note.midiPitch = Pitches[static_cast<std::size_t>(index)];
        note.staff = 1;
        note.voice = "right";
        note.sourceNoteIndex = index;
        note.source = guide.source;
        note.staffSpelling = StaffSpelling{"CDEFGABC"[index], 0, index < 7 ? 4 : 5};
        performance.notes.push_back(note);
    }
    static constexpr std::array<int, 4> LowerPitches{48, 43, 45, 47};
    static constexpr std::array<int, 4> LowerSteps{3, 0, 1, 2};
    for (int index = 0; index < 4; ++index)
    {
        const double x = 150 + index * 150;
        const double y = 348 - LowerSteps[static_cast<std::size_t>(index)] * 6;
        drawHead(x, y, true);
        StaffPerformanceNote note;
        note.startTick = index * 960;
        note.durationTicks = 960;
        note.midiPitch = LowerPitches[static_cast<std::size_t>(index)];
        note.staff = 2;
        note.voice = "left";
        note.source = {x - 7.5, y - 4.5, 15, 9};
        performance.notes.push_back(note);
    }
    painter.end();
    project.image = image;
    project.staffPages.push_back({"Real raster C-major grand staff", image, image, 0, 3840});
    performance.timingFingerprint = staffTimingFingerprint(project.score);
    return project;
}

void modalOperation(MainWindow &window, const std::function<void()> &open,
                    const std::function<void(StaffNoteDialog &)> &operate)
{
    QTimer timer;
    timer.setSingleShot(true);
    QString error;
    bool seen = false;
    QObject::connect(&timer, &QTimer::timeout, &window,
                     [&]
                     {
                         auto *base = window.findChild<QDialog *>("staffNoteAddDialog");
                         if (!base || !base->isVisible())
                         {
                             error = "The production staff-note dialog did not open";
                             return;
                         }
                         seen = true;
                         try
                         {
                             operate(*static_cast<StaffNoteDialog *>(base));
                         }
                         catch (const std::exception &exception)
                         {
                             error = QString::fromUtf8(exception.what());
                             base->reject();
                         }
                     });
    timer.start(20);
    open();
    if (!seen || !error.isEmpty())
        throw std::runtime_error(error.isEmpty() ? "The production staff-note dialog was not observed"
                                                 : error.toStdString());
}

void contextAdd(MainWindow &window, ScoreView &view, QPointF position, const char *actionKey,
                const std::function<void(StaffNoteDialog &)> &operate)
{
    view.centerOn(position);
    QApplication::processEvents();
    modalOperation(
        window,
        [&]
        {
            QTimer menuTimer;
            menuTimer.setSingleShot(true);
            QObject::connect(&menuTimer, &QTimer::timeout, &window,
                             [&]
                             {
                                 auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                 if (!menu)
                                     return;
                                 QAction *selected = nullptr;
                                 for (auto *action : menu->actions())
                                     if (action->text() == trText(actionKey) && action->isEnabled())
                                         selected = action;
                                 if (!selected)
                                 {
                                     menu->close();
                                     return;
                                 }
                                 menu->setActiveAction(selected);
                                 QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                                 QApplication::sendEvent(menu, &enter);
                             });
            menuTimer.start(0);
            const QPoint point = view.mapFromScene(position);
            QContextMenuEvent context(QContextMenuEvent::Mouse, point, view.viewport()->mapToGlobal(point));
            QApplication::sendEvent(view.viewport(), &context);
        },
        operate);
}
} // namespace

void runStaffSmartNoteCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        0, &window,
        [&window, &args, &app]
        {
            QJsonArray checks, artifacts;
            QJsonObject metrics;
            QString error;
            bool passed = false;
            const auto check = [&](const QString &name, bool condition)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", condition}});
                if (!condition)
                    throw std::runtime_error(name.toStdString());
            };
            try
            {
                const QString folder = QFileInfo(args.value("report")).absolutePath();
                const QString recovery = QDir(folder).filePath("recovery");
                check("Smart-note evidence and recovery directories exist",
                      QDir().mkpath(folder) && QDir().mkpath(recovery));
                qputenv("JIANPU_STAFF_RECOVERY_DIRECTORY", recovery.toUtf8());
                const QString provided = args.value("staff-smart-project");
                auto original = provided.isEmpty() ? fixtureProject() : loadProject(provided);
                original.practiceMix = {true, true, 0.37, 0.63};
                original.score.baseVelocity = 77;
                original.score.versePrograms = {40, 35};
                original.practiceSettings.emplace();
                original.practiceSettings->speed = 1.15;
                original.practiceSettings->metronome = false;
                original.practiceSettings->originalVolume = 0.41;
                original.staffPerformance->timingFingerprint = staffTimingFingerprint(original.score);
                const auto originalFileHash = provided.isEmpty() ? QByteArray{} : fileHash(provided);
                check("Input has a complete editable original-image performance",
                      original.staffImagePlayback && original.staffPerformance && !original.staffPages.empty());
                const auto originalPixels = pixelHash(original);
                const QString copyPath = QDir(folder).filePath("smart-ui-copy.jpp");
                saveProject(copyPath, original);
                artifacts.append(copyPath);
                window.openFile(copyPath);
                check("Production window is ready without rerunning recognition",
                      waitUntil([&] { return !window.isAudioLoading() && !window.isRecognizing(); }));
                auto *baseView = window.findChild<QGraphicsView *>("scoreView");
                auto *practiceMode = window.findChild<QPushButton *>("practiceModeButton");
                auto *correctionMode = window.findChild<QPushButton *>("correctionModeButton");
                auto *pitchMode = window.findChild<QCheckBox *>("staffPitchDrag");
                auto *timingMode = window.findChild<QCheckBox *>("staffTimingDrag");
                auto *inspector = window.findChild<QWidget *>("staffNoteInspector");
                auto *stack = window.findChild<QStackedWidget *>("noteInspectorStack");
                auto *staffScroll = window.findChild<QScrollArea *>("staffInspectorScroll");
                auto *pitch = inspector ? inspector->findChild<QSpinBox *>("staffNotePitch") : nullptr;
                auto *onset = inspector ? inspector->findChild<QLineEdit *>("staffNoteOnset") : nullptr;
                auto *duration = inspector ? inspector->findChild<QLineEdit *>("staffNoteDuration") : nullptr;
                auto *measureChoice = inspector ? inspector->findChild<QComboBox *>("staffNoteMeasure") : nullptr;
                auto *apply = inspector ? inspector->findChild<QPushButton *>("staffNoteApply") : nullptr;
                auto *cancel = inspector ? inspector->findChild<QPushButton *>("staffNoteCancel") : nullptr;
                auto *audition = inspector ? inspector->findChild<QPushButton *>("staffNoteAudition") : nullptr;
                auto *noteError = inspector ? inspector->findChild<QLabel *>("staffNoteError") : nullptr;
                auto *play = window.findChild<QPushButton *>("play");
                auto *stop = window.findChild<QPushButton *>("stop");
                auto *lyric = window.findChild<QLabel *>("lyric");
                auto *positionSlider = window.findChild<QSlider *>("position");
                auto *playbackTime = window.findChild<QLabel *>("playbackTime");
                auto *undo = window.findChild<QAction *>("menuUndo");
                auto *redo = window.findChild<QAction *>("menuRedo");
                auto *save = window.findChild<QAction *>("menuSave");
                auto *recover = window.findChild<QAction *>("menuRecoverStaffEdits");
                check("Actual canvas, smart editing controls and command actions exist",
                      baseView && practiceMode && correctionMode && pitchMode && timingMode && inspector &&
                          stack && staffScroll && pitch && onset && duration && measureChoice && apply && cancel &&
                          audition && noteError && play && stop && undo && redo && save && recover);
                auto *view = static_cast<ScoreView *>(baseView);
                window.resize(1320, 960);
                window.show();
                practiceMode->click();
                const bool oneClickPractice = practiceMode->isChecked() && !correctionMode->isChecked();
                correctionMode->click();
                QApplication::processEvents();
                check("Correction mode defaults to position and pitch linkage",
                      oneClickPractice && correctionMode->isChecked() && !practiceMode->isChecked() &&
                          !window.findChild<QComboBox *>("workspaceMode") && pitchMode->isChecked() &&
                          pitchMode->isEnabled() && timingMode->isChecked() && timingMode->isEnabled());
                check("Audition explicitly selects the tested Windows MIDI backend",
                      window.player().setAudioBackend(AudioBackend::WindowsMidi));
                const auto notes = window.project().staffPerformance->notes;
                const auto first = std::find_if(notes.begin(), notes.end(), [](const auto &note)
                                                { return note.staff == 1 && note.hasImageAnchor; });
                check("An actual upper-staff source head is available", first != notes.end());
                const auto target = *first;
                window.setStaffPage(target.pageIndex);
                view->resetTransform();
                view->scale(1.5, 1.5);
                const auto geometry =
                    crispStaffImageLines(window.project().staffPages[std::size_t(target.pageIndex)].sourceImage,
                                         window.project().staffPerformance->staffCount);
                const QPointF targetCenter(target.source.x + target.source.width / 2,
                                           target.source.y + target.source.height / 2);
                const auto staff =
                    std::find_if(geometry.begin(), geometry.end(), [&](const auto &entry)
                                 { return entry.staff == 1 && entry.bounds.contains(targetCenter); });
                check("True raster staff lines locate the selected note",
                      staff != geometry.end() && staff->spacing >= 5);
                bool bass = false;
                for (const auto &clef : window.project().staffPerformance->clefChanges)
                    if (clef.staff == 1 && clef.startTick <= target.startTick)
                        bass = clef.bassClef;
                const char step = bass ? 'A' : 'F';
                int expectedPitch = bass ? 45 : 65;
                const int fifths = window.project().staffKeyFifths;
                const QString order = fifths < 0 ? "BEADGCF" : "FCGDAEB";
                if (order.left(std::abs(fifths)).contains(QChar(step)))
                    expectedPitch += fifths < 0 ? -1 : 1;
                QPointF addPoint;
                bool empty = false;
                for (int offset = 0; offset < 20 && !empty; ++offset)
                {
                    addPoint = {targetCenter.x() + (1.5 + offset) * staff->spacing,
                                staff->linePositions.back() - staff->spacing / 2};
                    empty = staff->bounds.contains(addPoint);
                    for (const auto &note : notes)
                        if (note.pageIndex == target.pageIndex && note.hasImageAnchor &&
                            QRectF(note.source.x - 4, note.source.y - 6, note.source.width + 8,
                                   note.source.height + 12)
                                .contains(addPoint))
                            empty = false;
                }
                check("The add gesture uses an actual empty staff position", empty);
                view->centerOn(addPoint);
                QApplication::processEvents();
                const std::size_t beforeAdd = notes.size();
                StaffPerformanceNote added;
                modalOperation(
                    window,
                    [&]
                    {
                        QTimer menuTimer;
                        menuTimer.setSingleShot(true);
                        QObject::connect(&menuTimer, &QTimer::timeout, &window,
                                         [&]
                                         {
                                             auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                             if (!menu || menu->actions().isEmpty())
                                                 return;
                                             auto *action = menu->actions().front();
                                             if (action->text() != trText("ui.original_playback.add_note"))
                                             {
                                                 menu->close();
                                                 return;
                                             }
                                             menu->setActiveAction(action);
                                             QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                                             QApplication::sendEvent(menu, &enter);
                                         });
                        menuTimer.start(0);
                        const QPoint point = view->mapFromScene(addPoint);
                        QContextMenuEvent context(QContextMenuEvent::Mouse, point,
                                                  view->viewport()->mapToGlobal(point));
                        QApplication::sendEvent(view->viewport(), &context);
                    },
                    [&](StaffNoteDialog &dialog)
                    {
                        auto *pitch = dialog.findChild<QSpinBox *>("staffNotePitch");
                        auto *apply = dialog.findChild<QPushButton *>("staffNoteAddApply");
                        auto *onsetField = dialog.findChild<QLineEdit *>("staffNoteOnset");
                        if (onsetField && onsetField->text().trimmed().isEmpty())
                            onsetField->setText("0");
                        check("The real Add dialog derives the correct staff-line pitch and key signature",
                              pitch && apply && pitch->value() == expectedPitch && dialog.note().staff == 1 &&
                                  std::abs(dialog.note().source.y + dialog.note().source.height / 2 -
                                           addPoint.y()) < 1);
                        apply->click();
                        if (dialog.isVisible())
                            throw std::runtime_error("The Add dialog rejected its valid automatic hint");
                        added = dialog.note();
                    });
                check("Context-menu Add commits one complete sounding note",
                      window.project().staffPerformance->notes.size() == beforeAdd + 1);
                check("The added note has its real red symbol and audible plan entry",
                      visualHeadAt(*view, added.source) && soundContains(window.project(), added));
                const auto beforeDrag = soundPlan(window.project());
                auto selected =
                    window.project().staffPerformance->notes[std::size_t(targetIndex(window.project(), target))];
                drag(*view, selected.source, -staff->spacing);
                auto raised =
                    window.project().staffPerformance->notes[std::size_t(targetIndex(window.project(), target))];
                check("Actual upward dragging changes MIDI and moves at least one half-space",
                      raised.midiPitch > selected.midiPitch &&
                          raised.source.y < selected.source.y - staff->spacing * 0.4);
                check("Upward dragging changes the real full sounding plan",
                      soundPlan(window.project()) != beforeDrag && soundContains(window.project(), raised));
                drag(*view, raised.source, staff->spacing);
                auto lowered =
                    window.project().staffPerformance->notes[std::size_t(targetIndex(window.project(), target))];
                check("Actual downward dragging restores the diatonic pitch and source line",
                      lowered.midiPitch == selected.midiPitch &&
                          std::abs(lowered.source.y - selected.source.y) <= staff->spacing * 0.15);
                check("The downward sound agrees with its restored pitch",
                      soundPlan(window.project()) == beforeDrag);
                const auto oldSource = lowered.source;
                const int editedPitch = lowered.midiPitch + 2;
                clickNote(*view, lowered);
                const bool showsA = inspectorDisplays(*inspector, window.project(), lowered);
                std::optional<StaffPerformanceNote> otherSelection;
                for (const auto &note : window.project().staffPerformance->notes)
                {
                    if (!note.hasImageAnchor || note.pageIndex != lowered.pageIndex ||
                        note.midiPitch == lowered.midiPitch || note.startTick == lowered.startTick ||
                        note.durationTicks == lowered.durationTicks || note.durationTicks <= 1 || note.tieStart ||
                        note.tieStop)
                        continue;
                    const QPointF center(note.source.x + note.source.width / 2,
                                         note.source.y + note.source.height / 2);
                    int hits = 0;
                    for (const auto &candidate : window.project().staffPerformance->notes)
                        if (candidate.hasImageAnchor && candidate.pageIndex == note.pageIndex &&
                            QRectF(candidate.source.x - 4, candidate.source.y - 6, candidate.source.width + 8,
                                   candidate.source.height + 12)
                                .contains(center))
                            ++hits;
                    if (hits == 1)
                    {
                        otherSelection = note;
                        break;
                    }
                }
                if (!otherSelection)
                    throw std::runtime_error("A second distinct source note is required for the sidebar check");
                clickNote(*view, *otherSelection);
                check("Actual A/B source clicks bind distinct sidebar pitch, voice, onset and duration without a "
                      "top Properties button",
                      showsA && stack->currentWidget() == staffScroll && staffScroll->widget() == inspector &&
                          inspectorDisplays(*inspector, window.project(), *otherSelection) &&
                          !window.findChild<QPushButton *>("staffImageNoteProperties"));
                QApplication::processEvents();
                auto *title = window.findChild<QLabel *>("title");
                auto *identity = window.findChild<QLabel *>("projectIdentity");
                const QRect viewportBounds(view->viewport()->mapTo(&window, QPoint()), view->viewport()->size());
                const QRect playBounds(play->mapTo(&window, QPoint()), play->size());
                const QRect stopBounds(stop->mapTo(&window, QPoint()), stop->size());
                const QRect titleBounds(title ? title->mapTo(&window, QPoint()) : QPoint(),
                                        title ? title->size() : QSize());
                const QRect identityBounds(identity ? identity->mapTo(&window, QPoint()) : QPoint(),
                                           identity ? identity->size() : QSize());
                const QRect practiceBounds(practiceMode->mapTo(&window, QPoint()), practiceMode->size());
                const QRect correctionBounds(correctionMode->mapTo(&window, QPoint()), correctionMode->size());
                const QRect lyricBounds(lyric ? lyric->mapTo(&window, QPoint()) : QPoint(),
                                        lyric ? lyric->size() : QSize());
                const QRect positionBounds(positionSlider ? positionSlider->mapTo(&window, QPoint()) : QPoint(),
                                           positionSlider ? positionSlider->size() : QSize());
                const QRect timeBounds(playbackTime ? playbackTime->mapTo(&window, QPoint()) : QPoint(),
                                       playbackTime ? playbackTime->size() : QSize());
                metrics.insert("compactLayout",
                               QJsonObject{{"windowWidth", window.width()},
                                           {"windowHeight", window.height()},
                                           {"viewportHeight", viewportBounds.height()},
                                           {"viewportTop", viewportBounds.top()},
                                           {"viewportBottom", viewportBounds.bottom()},
                                           {"playTop", playBounds.top()},
                                           {"lyricLeft", lyricBounds.left()},
                                           {"lyricRight", lyricBounds.right()},
                                           {"positionLeft", positionBounds.left()},
                                           {"positionRight", positionBounds.right()},
                                           {"timeLeft", timeBounds.left()},
                                           {"lyricPositionOverlap", lyricBounds.intersects(positionBounds)}});
                check("A 1320-by-960 correction window leaves at least 600 pixels for the score and keeps "
                      "header and transport visible",
                      window.size() == QSize(1320, 960) && viewportBounds.height() >= 600 && title && identity &&
                          title->isVisible() && identity->isVisible() && practiceMode->isVisible() &&
                          correctionMode->isVisible() && play->isVisible() && stop->isVisible() &&
                          window.rect().contains(titleBounds) && window.rect().contains(identityBounds) &&
                          window.rect().contains(practiceBounds) && window.rect().contains(correctionBounds) &&
                          window.rect().contains(playBounds) && window.rect().contains(stopBounds) &&
                          std::max({titleBounds.bottom(), identityBounds.bottom(), practiceBounds.bottom(),
                                    correctionBounds.bottom()}) < viewportBounds.top() &&
                          lyric && positionSlider && playbackTime && lyric->isVisible() &&
                          positionSlider->isVisible() && playbackTime->isVisible() &&
                          window.rect().contains(lyricBounds) && window.rect().contains(positionBounds) &&
                          window.rect().contains(timeBounds) && lyricBounds.width() >= 100 &&
                          positionBounds.width() > 0 && lyricBounds.right() < positionBounds.left() &&
                          positionBounds.right() < timeBounds.left() &&
                          std::min(playBounds.top(), stopBounds.top()) > viewportBounds.bottom());
                const auto beforeScroll = material(window.project());
                const int oldMinimumHeight = staffScroll->minimumHeight();
                const int oldMaximumHeight = staffScroll->maximumHeight();
                int formHeight = std::max(inspector->minimumSizeHint().height(), inspector->sizeHint().height());
                if (inspector->hasHeightForWidth())
                    formHeight = std::max(formHeight, inspector->heightForWidth(staffScroll->viewport()->width()));
                const int sufficientHeight = formHeight + 2 * staffScroll->frameWidth() + 32;
                staffScroll->setFixedHeight(sufficientHeight);
                QApplication::processEvents();
                check("An active staff inspector with enough height has neither overflow nor a visible scrollbar",
                      staffScroll->verticalScrollBar()->maximum() == 0 &&
                          !staffScroll->verticalScrollBar()->isVisible() &&
                          material(window.project()) == beforeScroll);
                staffScroll->setFixedHeight(std::max(150, formHeight / 2));
                QApplication::processEvents();
                const bool shortOverflow = staffScroll->verticalScrollBar()->maximum() > 0 &&
                                           staffScroll->verticalScrollBar()->isVisible();
                staffScroll->verticalScrollBar()->setValue(staffScroll->verticalScrollBar()->maximum());
                QApplication::processEvents();
                const QRect cancelBounds(cancel->mapTo(staffScroll->viewport(), QPoint()), cancel->size());
                const bool bottomReachable = staffScroll->viewport()->rect().contains(cancelBounds);
                stack->setCurrentIndex(0);
                QApplication::processEvents();
                stack->setCurrentIndex(1);
                staffScroll->setFixedHeight(sufficientHeight);
                QApplication::processEvents();
                const bool independentPages = staffScroll->verticalScrollBar()->maximum() == 0 &&
                                              !staffScroll->verticalScrollBar()->isVisible();
                staffScroll->setMinimumHeight(oldMinimumHeight);
                staffScroll->setMaximumHeight(oldMaximumHeight);
                window.resize(1320, 960);
                QApplication::processEvents();
                check("A short staff inspector scrolls to its bottom controls and inactive legacy content cannot "
                      "force staff overflow",
                      shortOverflow && bottomReachable && independentPages &&
                          material(window.project()) == beforeScroll);
                const auto beforeTime = material(window.project());
                const auto measure =
                    window.project().score.writtenMeasures[std::size_t(measureChoice->currentData().toInt())];
                const auto newDuration = std::max<std::int64_t>(1, otherSelection->durationTicks / 2);
                const auto newOnset = std::min(otherSelection->startTick - measure.startTick + 120,
                                               measure.durationTicks - newDuration);
                onset->setText(QString::number(double(newOnset) / TicksPerQuarter, 'g', 15));
                duration->setText(QString::number(double(newDuration) / TicksPerQuarter, 'g', 15));
                apply->click();
                auto timed = *otherSelection;
                timed.startTick = measure.startTick + newOnset;
                timed.durationTicks = newDuration;
                bool timePresent = false;
                for (const auto &note : window.project().staffPerformance->notes)
                    if (note.midiPitch == timed.midiPitch && note.staff == timed.staff &&
                        note.voice == timed.voice && note.startTick == timed.startTick &&
                        note.durationTicks == timed.durationTicks && note.source.x == timed.source.x)
                        timePresent = soundContains(window.project(), note);
                if (!timePresent)
                    throw std::runtime_error("Sidebar did not commit a valid onset/duration edit: " +
                                             noteError->text().toStdString());
                undo->trigger();
                check("Sidebar onset and duration edits enter the sounding plan and Undo restores the original "
                      "clock",
                      timePresent && material(window.project()) == beforeTime);
                const auto beforeInvalid = material(window.project());
                onset->setText("-1");
                apply->click();
                const bool invalidKept =
                    material(window.project()) == beforeInvalid && !noteError->text().isEmpty();
                bool draftPromptSeen = false;
                QTimer rejectDraft;
                rejectDraft.setSingleShot(true);
                QObject::connect(&rejectDraft, &QTimer::timeout, &window,
                                 [&]
                                 {
                                     auto *dialog = window.findChild<QMessageBox *>("noteDraftConfirmation");
                                     if (dialog && dialog->isVisible())
                                     {
                                         draftPromptSeen = true;
                                         if (auto *button = dialog->button(QMessageBox::Cancel))
                                             button->click();
                                     }
                                 });
                rejectDraft.start(0);
                play->click();
                rejectDraft.stop();
                const bool playCancelled = draftPromptSeen && correctionMode->isChecked() &&
                                           !practiceMode->isChecked() && !window.player().isPlaying() &&
                                           material(window.project()) == beforeInvalid;
                cancel->click();
                check("An invalid sidebar draft cannot Apply and Cancel restores model and original fields",
                      invalidKept && playCancelled && material(window.project()) == beforeInvalid &&
                          noteError->text().isEmpty() &&
                          inspectorDisplays(*inspector, window.project(), *otherSelection));
                practiceMode->click();
                clickNote(*view, lowered);
                const bool practiceReadOnly = practiceMode->isChecked() && !correctionMode->isChecked() &&
                                              inspectorDisplays(*inspector, window.project(), lowered) &&
                                              !pitch->isEnabled() && !apply->isEnabled();
                const auto beforePlay = material(window.project());
                play->click();
                const bool playing = waitUntil([&] { return window.player().isPlaying(); });
                clickNote(*view, *otherSelection);
                apply->click();
                const bool playReadOnly = playing && !pitch->isEnabled() && !apply->isEnabled() &&
                                          material(window.project()) == beforePlay;
                stop->click();
                correctionMode->click();
                view->resetTransform();
                view->scale(1.5, 1.5);
                clickNote(*view, lowered);
                check("Practice and playback retain visible read-only staff properties without editing music",
                      practiceReadOnly && playReadOnly && correctionMode->isChecked() &&
                          !practiceMode->isChecked() && inspectorDisplays(*inspector, window.project(), lowered));
                if (!waitUntil([&] { return pitch->isEnabled() && !window.player().isPlaying(); }))
                {
                    metrics.insert("sidebarReady",
                                   QJsonObject{{"currentPitch", pitch->value()},
                                               {"staffError", noteError->text()},
                                               {"editorEnabled", inspector->isEnabled()},
                                               {"pitchEnabled", pitch->isEnabled()},
                                               {"playing", window.player().isPlaying()},
                                               {"audioLoading", window.isAudioLoading()},
                                               {"modeIndex", correctionMode->isChecked() ? 1 : 0}});
                    throw std::runtime_error(QString("The sidebar remained read-only after playback stopped: "
                                                     "pitchEnabled=%1 playing=%2 audioLoading=%3 mode=%4 error=%5")
                                                 .arg(pitch->isEnabled())
                                                 .arg(window.player().isPlaying())
                                                 .arg(window.isAudioLoading())
                                                 .arg(correctionMode->isChecked() ? 1 : 0)
                                                 .arg(noteError->text())
                                                 .toStdString());
                }
                const auto beforeProperties = material(window.project());
                pitch->setValue(editedPitch);
                QApplication::processEvents();
                const bool materialEqual = material(window.project()) == beforeProperties;
                const bool glyphExists = visualHeadAbove(*view, oldSource, staff->spacing);
                const auto *pitchName = inspector->findChild<QLabel *>("staffNotePitchName");
                metrics.insert("sidebarPreview",
                               QJsonObject{{"currentPitch", pitch->value()},
                                           {"requestedPitch", editedPitch},
                                           {"staffError", noteError->text()},
                                           {"pitchName", pitchName ? pitchName->text() : QString()},
                                           {"editorEnabled", inspector->isEnabled()},
                                           {"pitchEnabled", pitch->isEnabled()},
                                           {"playing", window.player().isPlaying()},
                                           {"audioLoading", window.isAudioLoading()},
                                           {"modeIndex", correctionMode->isChecked() ? 1 : 0},
                                           {"materialEqual", materialEqual},
                                           {"glyphExists", glyphExists}});
                check("Changing a property previews a red glyph without committing project music",
                      materialEqual && glyphExists);
                const auto previous = window.player().voiceState().previewNoteOns;
                audition->click();
                check("The actual sidebar Audition submits the edited MIDI pitch to the audio worker",
                      waitUntil(
                          [&]
                          {
                              const auto state = window.player().voiceState();
                              return state.previewNoteOns > previous &&
                                     state.previewPitch == editedPitch + window.player().transpose() +
                                                               window.project().score.tonic -
                                                               window.project().staffPerformance->sourceTonic;
                          }));
                cancel->click();
                check("Cancelling Properties restores the complete project and correction layer",
                      material(window.project()) == beforeProperties);
                pitch->setValue(editedPitch);
                apply->click();
                const auto edited =
                    window.project().staffPerformance->notes[std::size_t(targetIndex(window.project(), target))];
                check("Property Apply links accepted MIDI, original-pixel Y and actual red head",
                      edited.midiPitch == editedPitch && edited.source.y < oldSource.y &&
                          visualHeadAt(*view, edited.source) && soundContains(window.project(), edited));
                const auto afterProperties = material(window.project());
                undo->trigger();
                check("One Undo restores pitch, rectangle and overlay metadata together",
                      material(window.project()) == beforeProperties);
                redo->trigger();
                check("One Redo restores the accepted pitch, rectangle and red layer",
                      material(window.project()) == afterProperties && visualHeadAt(*view, edited.source));
                pitchMode->setChecked(false);
                timingMode->setChecked(false);
                const int editedIndex = targetIndex(window.project(), target);
                const auto beforeAnchorOnly = material(window.project());
                const auto beforeAnchorSound = soundPlan(window.project());
                const auto beforeManual = window.project().processing.value("manualImageAnchors");
                const auto originalLocation = visualRecord(window.project(), edited).value("originalSource");
                drag(*view, edited.source, 0, staff->spacing * 2);
                const auto anchorMoved = window.project().staffPerformance->notes[std::size_t(editedIndex)];
                const auto afterAnchorOnly = material(window.project());
                const auto afterManual = window.project().processing.value("manualImageAnchors");
                check("Anchor-only movement keeps sound and relocates an existing correction symbol without "
                      "losing its origin",
                      anchorMoved.midiPitch == edited.midiPitch && anchorMoved.source.y == edited.source.y &&
                          anchorMoved.source.x > edited.source.x &&
                          soundPlan(window.project()) == beforeAnchorSound &&
                          !visualRecord(window.project(), anchorMoved).isEmpty() &&
                          visualRecord(window.project(), anchorMoved).value("originalSource") ==
                              originalLocation &&
                          visualHeadAt(*view, anchorMoved.source));
                undo->trigger();
                check(
                    "Anchor-only Undo restores both rectangle and correction metadata while leaving sound intact",
                    material(window.project()) == beforeAnchorOnly &&
                        window.project().processing.value("manualImageAnchors") == beforeManual &&
                        visualHeadAt(*view, edited.source) && soundPlan(window.project()) == beforeAnchorSound);
                redo->trigger();
                check("Anchor-only Redo restores the moved symbol and both provenance fields",
                      material(window.project()) == afterAnchorOnly &&
                          window.project().processing.value("manualImageAnchors") == afterManual &&
                          visualHeadAt(*view, anchorMoved.source));
                const QString screenshot = QDir(folder).filePath("smart-linked-original.png");
                check("The actual corrected original-image screenshot saves", window.grab().save(screenshot));
                artifacts.append(screenshot);
                QString recoveryPath;
                check("The 400ms debounce creates a recovery package only in the isolated test directory",
                      waitUntil(
                          [&]
                          {
                              recoveryPath = latestStaffEditRecovery(recovery);
                              return !recoveryPath.isEmpty() &&
                                     material(loadProject(recoveryPath)) == material(window.project());
                          },
                          4000) &&
                          QFileInfo(recoveryPath).absolutePath() == QFileInfo(recovery).absoluteFilePath());
                const auto recovered = loadProject(recoveryPath);
                check("Recovery reopens the current sound, symbols and untouched source pixels",
                      material(recovered) == material(window.project()) && pixelHash(recovered) == originalPixels);
                artifacts.append(recoveryPath);
                view->setFocus(Qt::OtherFocusReason);
                window.activateWindow();
                QApplication::processEvents();
                QKeyEvent savePress(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier);
                QKeyEvent saveRelease(QEvent::KeyRelease, Qt::Key_S, Qt::ControlModifier);
                QApplication::sendEvent(view, &savePress);
                QApplication::sendEvent(view, &saveRelease);
                check("Actual Ctrl+S saves the current project and marks it clean",
                      waitUntil([&] { return !window.hasUnsavedChanges(); }));
                const auto saved = loadProject(copyPath);
                check("Formal saved JPP agrees with current complete notes and visual edits",
                      material(saved) == material(window.project()));
                check("Original source pixels and the supplied input package never change",
                      pixelHash(window.project()) == originalPixels && pixelHash(saved) == originalPixels &&
                          (provided.isEmpty() || fileHash(provided) == originalFileHash));
                const auto recoveryPreferences = recoverySettings(saved);
                const auto submittedAuditions = window.player().voiceState().previewNoteOns;
                recover->trigger();
                check("The actual Recover menu restores symbols and nondefault mix, dynamics, instruments and "
                      "practice settings as an unsaved draft",
                      waitUntil([&] { return !window.isAudioLoading() && !window.isRecognizing(); }) &&
                          material(window.project()) == afterAnchorOnly &&
                          recoverySettings(window.project()) == recoveryPreferences &&
                          window.hasUnsavedChanges() && pixelHash(window.project()) == originalPixels &&
                          visualHeadAt(*view, anchorMoved.source));
                window.setStaffPage(anchorMoved.pageIndex);
                view->centerOn(anchorMoved.source.x + anchorMoved.source.width / 2,
                               anchorMoved.source.y + anchorMoved.source.height / 2);
                QApplication::processEvents();
                const QPoint select =
                    view->mapFromScene(QPointF(anchorMoved.source.x + anchorMoved.source.width / 2,
                                               anchorMoved.source.y + anchorMoved.source.height / 2));
                mouseAt(*view, QEvent::MouseButtonPress, select, Qt::LeftButton, Qt::LeftButton);
                mouseAt(*view, QEvent::MouseButtonRelease, select, Qt::LeftButton, Qt::NoButton);
                const auto savedSourceHash = fileHash(copyPath);
                pitch->setValue(editedPitch + 1);
                apply->click();
                const auto pendingMaterial = material(window.project());
                if (pendingMaterial == afterAnchorOnly)
                    throw std::runtime_error("Quick-switch sidebar edit was not committed: " +
                                             noteError->text().toStdString());
                const auto pendingPixels = pixelHash(window.project());
                const auto pendingSource = window.project().score.imagePath;
                const auto pendingPreferences = recoverySettings(window.project());
                const QString blocked = QDir(folder).filePath("blocked-recovery-" +
                                                              QUuid::createUuid().toString(QUuid::WithoutBraces));
                QFile blocker(blocked);
                if (!blocker.open(QIODevice::WriteOnly) || blocker.write("not a directory") <= 0)
                    throw std::runtime_error("Quick-switch blocked-directory fixture failed");
                blocker.close();
                qputenv("JIANPU_STAFF_RECOVERY_DIRECTORY", (blocked + "/recovery").toUtf8());
                auto *newProject = window.findChild<QAction *>("menuNew");
                auto *status = window.findChild<QLabel *>("statusMessage");
                if (!newProject || !status)
                    throw std::runtime_error("The production New menu or status control is missing");
                bool discarded = false;
                bool reported = false;
                QString recoveryError;
                QTimer switchDialogs;
                QObject::connect(&switchDialogs, &QTimer::timeout, &window,
                                 [&]
                                 {
                                     auto *message =
                                         qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                                     if (!message)
                                         return;
                                     if (auto *discard = message->button(QMessageBox::Discard))
                                     {
                                         discarded = true;
                                         discard->click();
                                     }
                                     else if (message->icon() == QMessageBox::Warning)
                                     {
                                         reported = true;
                                         recoveryError = message->text();
                                         if (auto *ok = message->button(QMessageBox::Ok))
                                             ok->click();
                                     }
                                 });
                switchDialogs.start(5);
                newProject->trigger();
                switchDialogs.stop();
                const bool retained = material(window.project()) == pendingMaterial &&
                                      pixelHash(window.project()) == pendingPixels &&
                                      window.project().score.imagePath == pendingSource &&
                                      recoverySettings(window.project()) == pendingPreferences;
                check("The actual New menu catches a failed recovery write, reports its nonempty error and "
                      "retains the unsaved project",
                      discarded && reported && recoveryError.contains(blocked) &&
                          status->text() == recoveryError && retained && window.hasUnsavedChanges() &&
                          fileHash(copyPath) == savedSourceHash);
                qputenv("JIANPU_STAFF_RECOVERY_DIRECTORY", recovery.toUtf8());
                QFile::remove(blocked);
                discarded = false;
                reported = false;
                switchDialogs.start(5);
                newProject->trigger();
                switchDialogs.stop();
                const QString flushed = latestStaffEditRecovery(recovery);
                const auto flushedProject = loadProject(flushed);
                const auto flushedHash = fileHash(flushed);
                QElapsedTimer quiet;
                quiet.start();
                waitUntil([&] { return quiet.elapsed() >= 500; }, 1500);
                check("Retrying the actual New menu after directory repair flushes project A before replacing it "
                      "and the new project cannot overwrite its recovery",
                      discarded && !reported && retained && pendingMaterial != afterAnchorOnly &&
                          material(flushedProject) == pendingMaterial &&
                          pixelHash(flushedProject) == pendingPixels &&
                          recoverySettings(flushedProject) == pendingPreferences &&
                          window.project().generatedNotation && window.project().score.notes.empty() &&
                          !window.project().staffPerformance && window.hasUnsavedChanges() &&
                          latestStaffEditRecovery(recovery) == flushed && fileHash(flushed) == flushedHash &&
                          fileHash(copyPath) == savedSourceHash);
                artifacts.append(flushed);
                auto other = fixtureProject();
                other.score.title = "Untouched smart-note project B";
                window.setProject(other);
                // The distance/chord cases use B's measured raster anchors, not edited user-photo heuristics.
                practiceMode->click();
                correctionMode->click();
                QApplication::processEvents();
                view->resetTransform();
                view->scale(1.5, 1.5);
                const auto chordPixels = pixelHash(window.project());
                std::array<StaffPerformanceNote, 2> distanceNotes;
                for (std::size_t position = 0; position < distanceNotes.size(); ++position)
                {
                    const QPointF point(position == 0 ? 187.5 : 206.25, 162);
                    contextAdd(window, *view, point, "ui.original_playback.add_note",
                               [&](StaffNoteDialog &dialog)
                               {
                                   auto *accept = dialog.findChild<QPushButton *>("staffNoteAddApply");
                                   auto *time = dialog.findChild<QLineEdit *>("staffNoteOnset");
                                   if (!accept || !time || time->text().trimmed().isEmpty())
                                       throw std::runtime_error(
                                           "Known raster anchors did not produce an automatic onset");
                                   accept->click();
                                   if (dialog.isVisible())
                                       throw std::runtime_error("The automatic distance-based onset was rejected");
                                   distanceNotes[position] = dialog.note();
                               });
                }
                check("Different source X positions produce distinct distance-based starts in the continuous "
                      "sounding plan",
                      distanceNotes[0].startTick == 240 && distanceNotes[1].startTick == 360 &&
                          soundContains(window.project(), distanceNotes[0]) &&
                          soundContains(window.project(), distanceNotes[1]));
                const auto reference = other.staffPerformance->notes.front();
                std::array<StaffPerformanceNote, 2> chordNotes;
                QJsonObject beforeFinalChord;
                for (std::size_t tone = 0; tone < chordNotes.size(); ++tone)
                {
                    if (tone == 1)
                        beforeFinalChord = material(window.project());
                    const QPointF aboveHead(reference.source.x + reference.source.width / 2,
                                            reference.source.y - 1);
                    contextAdd(window, *view, aboveHead, "ui.original_playback.add_same_beat_note",
                               [&](StaffNoteDialog &dialog)
                               {
                                   auto *midi = dialog.findChild<QSpinBox *>("staffNotePitch");
                                   auto *accept = dialog.findChild<QPushButton *>("staffNoteAddApply");
                                   if (!midi || !accept)
                                       throw std::runtime_error("Same-beat note controls are missing");
                                   midi->setValue(tone == 0 ? 64 : 67);
                                   accept->click();
                                   if (dialog.isVisible())
                                       throw std::runtime_error("The same-beat chord tone was rejected");
                                   chordNotes[tone] = dialog.note();
                               });
                }
                const double referenceX = reference.source.x + reference.source.width / 2;
                check("An existing-head context menu adds three distinct vertical chord pitches with one onset "
                      "and all sounds retained",
                      chordNotes[0].midiPitch == 64 && chordNotes[1].midiPitch == 67 &&
                          chordNotes[0].startTick == reference.startTick &&
                          chordNotes[1].startTick == reference.startTick &&
                          chordNotes[0].staff == reference.staff && chordNotes[1].staff == reference.staff &&
                          std::abs(chordNotes[0].source.x + chordNotes[0].source.width / 2 - referenceX) < 0.01 &&
                          std::abs(chordNotes[1].source.x + chordNotes[1].source.width / 2 - referenceX) < 0.01 &&
                          chordNotes[0].source.y != chordNotes[1].source.y &&
                          soundContains(window.project(), reference) &&
                          soundContains(window.project(), chordNotes[0]) &&
                          soundContains(window.project(), chordNotes[1]));
                const auto afterChord = material(window.project());
                const auto chordPlan = soundPlan(window.project());
                undo->trigger();
                const bool chordUndo = material(window.project()) == beforeFinalChord;
                redo->trigger();
                const QString chordPath = QDir(folder).filePath("smart-chord-copy.jpp");
                saveProject(chordPath, window.project());
                const auto chordReopened = loadProject(chordPath);
                check("Chord Undo, Redo and reopened JPP preserve full polyphony, correction symbols and "
                      "immutable original pixels",
                      chordUndo && material(window.project()) == afterChord &&
                          material(chordReopened) == afterChord && soundPlan(chordReopened) == chordPlan &&
                          pixelHash(chordReopened) == chordPixels && pixelHash(window.project()) == chordPixels &&
                          fileHash(copyPath) == savedSourceHash);
                artifacts.append(chordPath);
                QString fixtureDragStage = "horizontal-original";
                const auto fixtureTone = [&](std::int64_t start, double x)
                {
                    std::optional<StaffPerformanceNote> selected;
                    for (const auto &note : window.project().staffPerformance->notes)
                        if (note.staff == 1 && note.voice == "right" && note.startTick == start &&
                            note.durationTicks == 480 &&
                            std::abs(note.source.x + note.source.width / 2 - x) < 0.75)
                        {
                            if (selected)
                                throw std::runtime_error("The horizontal-drag fixture note is not unique");
                            selected = note;
                        }
                    if (!selected)
                    {
                        metrics.insert(
                            "fixtureDragState",
                            QJsonObject{
                                {"stage", fixtureDragStage},
                                {"expectedOnset", double(start)},
                                {"expectedCenterX", x},
                                {"pitchEditing", pitchMode->isChecked()},
                                {"timingEditing", timingMode->isChecked()},
                                {"sidebarPitch", pitch->value()},
                                {"sidebarOnset", onset->text()},
                                {"sidebarDuration", duration->text()},
                                {"sidebarError", noteError->text()},
                                {"notes",
                                 staffPerformanceToJson(*window.project().staffPerformance).value("notes")},
                                {"visualEdits", window.project().processing.value("staffVisualEdits")}});
                        throw std::runtime_error(
                            "The horizontal-drag fixture note was not committed at its expected onset");
                    }
                    return *selected;
                };
                pitchMode->setChecked(false);
                timingMode->setChecked(true);
                const auto horizontalOriginal = fixtureTone(480, 225);
                fixtureDragStage = "horizontal-right";
                drag(*view, horizontalOriginal.source, 0, 37.5);
                const auto right = fixtureTone(720, 262.5);
                check("Timing-only right drag advances D4 to onset 720 without changing duration, pitch, staff or "
                      "voice",
                      right.midiPitch == 62 && right.durationTicks == horizontalOriginal.durationTicks &&
                          right.staff == horizontalOriginal.staff && right.voice == horizontalOriginal.voice &&
                          right.source.y == horizontalOriginal.source.y && soundContains(window.project(), right));
                drag(*view, right.source, 0, -37.5);
                fixtureDragStage = "horizontal-left";
                const auto left = fixtureTone(480, 225);
                check("Timing-only left drag restores onset 480 and the matching continuous sound",
                      left.midiPitch == horizontalOriginal.midiPitch &&
                          left.durationTicks == horizontalOriginal.durationTicks &&
                          soundContains(window.project(), left));
                pitchMode->setChecked(true);
                const auto beforeDiagonal = material(window.project());
                fixtureDragStage = "diagonal";
                drag(*view, left.source, -12, 37.5);
                const auto diagonal = fixtureTone(720, left.source.x + left.source.width / 2 + 37.5);
                const auto afterDiagonal = material(window.project());
                const bool diagonalSidebar = inspectorDisplays(*inspector, window.project(), diagonal);
                undo->trigger();
                const bool diagonalUndo = material(window.project()) == beforeDiagonal;
                redo->trigger();
                const QString diagonalPath = QDir(folder).filePath("smart-musical-drag-copy.jpp");
                saveProject(diagonalPath, window.project());
                const auto diagonalReopened = loadProject(diagonalPath);
                check("Diagonal drag links pitch and onset, sidebar fields, Undo/Redo and reopened original-pixel "
                      "storage",
                      diagonal.midiPitch > left.midiPitch && diagonal.startTick == 720 &&
                          diagonal.durationTicks == left.durationTicks && diagonalSidebar && diagonalUndo &&
                          material(window.project()) == afterDiagonal &&
                          material(diagonalReopened) == afterDiagonal &&
                          soundContains(diagonalReopened, diagonal) &&
                          pixelHash(diagonalReopened) == chordPixels && fileHash(copyPath) == savedSourceHash);
                artifacts.append(diagonalPath);
                metrics = {{"source",
                            provided.isEmpty() ? "real-raster-fixture" : QFileInfo(provided).absoluteFilePath()},
                           {"notes", int(saved.staffPerformance->notes.size())},
                           {"staffSpacing", staff->spacing},
                           {"automaticPitch", expectedPitch},
                           {"editedPitch", editedPitch},
                           {"previewNoteOns", double(submittedAuditions)},
                           {"originalPixelSHA256", QString::fromLatin1(originalPixels)},
                           {"distanceChordSource", "isolated-real-raster-project-B"},
                           {"distanceOnsets",
                            QJsonArray{double(distanceNotes[0].startTick), double(distanceNotes[1].startTick)}},
                           {"evidence", "Native Qt gestures/widgets, audio submission telemetry and real saved "
                                        "JPP; no model rerun or acoustic accuracy measurement"}};
                window.player().stop();
                passed = true;
            }
            catch (const std::exception &exception)
            {
                error = QString::fromUtf8(exception.what());
                window.player().stop();
            }
            QSaveFile report(args.value("report"));
            const auto bytes = QJsonDocument(QJsonObject{{"passed", passed},
                                                         {"error", error},
                                                         {"checks", checks},
                                                         {"metrics", metrics},
                                                         {"artifacts", artifacts}})
                                   .toJson();
            const bool saved =
                report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() && report.commit();
            app.exit(saved ? passed ? 0 : 3 : 4);
        });
}
} // namespace singlilt

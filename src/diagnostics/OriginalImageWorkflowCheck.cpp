// Original-page playback, cursor, and editing regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "OriginalImageWorkflowCheck.h"

#include "application/StaffScoreCorrection.h"
#include "recognition/CrispStaffSourceAnchors.h"
#include "storage/MusicXmlImporter.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ScoreView.h"
#include "ui/StaffCorrectionDialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace singlilt
{
namespace
{
bool waitUntil(const std::function<bool()> &ready, int timeout = 1500)
{
    QElapsedTimer elapsed;
    elapsed.start();
    do
    {
        QApplication::processEvents();
        if (ready())
            return true;
        QThread::msleep(5);
    } while (elapsed.elapsed() < timeout);
    return ready();
}

bool sameBox(const SourceRect &left, const SourceRect &right)
{
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}

bool imageAnchorsValid(const Project &project)
{
    const auto valid = [&project](int page, bool anchored, const SourceRect &box)
    {
        if (!anchored)
            return sameBox(box, {});
        if (page < 0 || std::size_t(page) >= project.staffPages.size())
            return false;
        const auto size = project.staffPages[std::size_t(page)].sourceImage.size();
        return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) &&
               std::isfinite(box.height) && box.x >= 0 && box.y >= 0 && box.width > 0 && box.height > 0 &&
               box.x + box.width <= size.width() && box.y + box.height <= size.height();
    };
    for (const auto &note : project.score.notes)
        if (!valid(note.pageIndex, note.hasImageAnchor, note.source))
            return false;
    for (const auto &note : project.staffPerformance->notes)
        if (!valid(note.pageIndex, note.hasImageAnchor, note.source))
            return false;
    return true;
}

bool originalPixels(const Project &project, const std::vector<QImage> &originals)
{
    if (!project.staffImagePlayback || project.generatedNotation ||
        project.staffPages.size() != originals.size() || project.image != originals.front())
        return false;
    for (std::size_t page = 0; page < originals.size(); ++page)
        if (project.staffPages[page].sourceImage != originals[page] ||
            project.staffPages[page].renderedImage != originals[page])
            return false;
    return true;
}

bool scenePixels(const ScoreView &view, const QImage &original)
{
    for (auto *item : view.scene()->items())
        if (const auto *pixmap = qgraphicsitem_cast<QGraphicsPixmapItem *>(item))
            return pixmap->pixmap().toImage().convertToFormat(QImage::Format_RGB32) ==
                   original.convertToFormat(QImage::Format_RGB32);
    return false;
}

int visibleRectangles(const ScoreView &view, const QColor &color)
{
    int count = 0;
    for (auto *item : view.scene()->items())
        if (const auto *rectangle = qgraphicsitem_cast<QGraphicsRectItem *>(item))
            if (rectangle->isVisible() && rectangle->pen().color() == color)
                ++count;
    return count;
}

void mouseAt(ScoreView &view, QPointF scenePoint, bool doubleClick = false)
{
    const auto local = view.mapFromScene(scenePoint);
    const auto global = view.viewport()->mapToGlobal(local);
    QMouseEvent press(doubleClick ? QEvent::MouseButtonDblClick : QEvent::MouseButtonPress, QPointF(local),
                      QPointF(global), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), QPointF(global), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
}

QJsonArray planRows(const Project &project)
{
    const auto plan =
        buildStaffPerformancePlan(project.score, buildTimeline(project.score), *project.staffPerformance);
    if (!plan.valid())
        throw std::runtime_error("The complete original-image sounding plan is invalid");
    QJsonArray rows;
    for (const auto &event : plan.events)
        rows.append(QJsonArray{double(event.startTick), double(event.durationTicks), event.midiPitch,
                               event.velocity, event.role == AccompanimentRole::Chord ? "primary" : "other"});
    return rows;
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

Project fixtureProject()
{
    const auto imported = parseMusicXml(R"XML(<score-partwise version="4.0">
<work><work-title>Original-image workflow fixture</work-title></work>
<part-list><score-part id="P1"><part-name>Hands</part-name></score-part></part-list><part id="P1">
<measure number="1"><attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>
<staves>2</staves><clef number="1"><sign>G</sign><line>2</line></clef>
<clef number="2"><sign>F</sign><line>4</line></clef></attributes>
<note><pitch><step>C</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><staff>1</staff></note>
<note><pitch><step>D</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><staff>1</staff></note>
<backup><duration>4</duration></backup>
<note><pitch><step>A</step><octave>2</octave></pitch><duration>4</duration><voice>2</voice><staff>2</staff></note>
<note><chord/><pitch><step>E</step><octave>3</octave></pitch><duration>4</duration><voice>2</voice><staff>2</staff></note>
<note><chord/><pitch><step>G</step><octave>2</octave></pitch><duration>4</duration><voice>2</voice><staff>2</staff></note>
</measure><measure number="2"><print new-page="yes"/>
<note><pitch><step>E</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice><staff>1</staff></note>
<backup><duration>4</duration></backup>
<note><pitch><step>B</step><octave>2</octave></pitch><duration>4</duration><voice>2</voice><staff>2</staff></note>
</measure></part></score-partwise>)XML");
    const auto converted = musicXmlToPerformance(imported, 0);
    if (!imported.valid() || !converted.valid())
        throw std::runtime_error("The two-page workflow MusicXML fixture could not be converted");
    Project project;
    project.score = *converted.selectedMelody.score;
    project.staffPerformance = converted.performance;
    project.notationStyle = NotationStyle::Staff;
    project.staffImagePlayback = true;
    project.generatedNotation = false;
    project.practiceMix.accompanimentEnabled = true;
    project.practiceSettings = ProjectPracticeSettings{};
    project.practiceSettings->metronome = false;
    project.processing = {{"local", true},
                          {"tempoNeedsConfirmation", true},
                          {"recognitionAccuracyMeasured", false},
                          {"sourceAnchorProvenance", "explicit-synthetic-head-bboxes"}};
    for (auto &note : project.staffPerformance->notes)
    {
        note.hasImageAnchor = true;
        if (note.pageIndex == 1)
            note.source = note.staff == 1 ? SourceRect{160, 155, 24, 16} : SourceRect{160, 290, 24, 16};
        else if (note.staff == 1)
            note.source = note.startTick == 0 ? SourceRect{140, 160, 24, 16} : SourceRect{360, 155, 24, 16};
        else if (note.midiPitch == 45)
            note.source = {140, 295, 24, 16};
        else if (note.midiPitch == 52)
            note.source = {140, 270, 24, 16};
        else
        {
            note.hasImageAnchor = false;
            note.source = {};
        }
    }
    std::int64_t tick = 0;
    for (auto &guide : project.score.notes)
    {
        guide.hasImageAnchor = false;
        guide.source = {};
        for (const auto &note : project.staffPerformance->notes)
            if (note.staff == 1 && note.startTick == tick)
            {
                guide.hasImageAnchor = note.hasImageAnchor;
                guide.source = note.source;
                break;
            }
        tick += guide.durationTicks;
    }
    for (int page = 0; page < 2; ++page)
    {
        QImage image(900, 1200, QImage::Format_RGB32);
        image.fill(page == 0 ? QColor("#FFFEF7") : QColor("#F6FAFF"));
        QPainter painter(&image);
        painter.setPen(QColor("#223B54"));
        painter.drawText(QPoint(65, 55), QString("Original page %1, quarter = 62").arg(page + 1));
        painter.setPen(QPen(Qt::black, 1.2));
        for (int line = 0; line < 5; ++line)
        {
            painter.drawLine(60, 150 + line * 12, 835, 150 + line * 12);
            painter.drawLine(60, 270 + line * 12, 835, 270 + line * 12);
        }
        painter.setBrush(Qt::black);
        for (const auto &note : project.staffPerformance->notes)
            if (note.pageIndex == page && note.hasImageAnchor)
                painter.drawEllipse(QRectF(note.source.x, note.source.y, note.source.width, note.source.height));
        if (page == 0)
            painter.drawEllipse(QRectF(140, 320, 24, 16));
        painter.end();
        project.staffPages.push_back(
            {QString("Original %1").arg(page + 1), image, image, page * 1920, (page + 1) * 1920});
    }
    project.image = project.staffPages.front().sourceImage;
    return project;
}

const StaffPerformanceNote *findNote(const Project &project, int pitch, std::int64_t tick)
{
    const auto &notes = project.staffPerformance->notes;
    const auto found = std::find_if(notes.begin(), notes.end(), [pitch, tick](const auto &note)
                                    { return note.midiPitch == pitch && note.startTick == tick; });
    return found == notes.end() ? nullptr : &*found;
}
} // namespace

void runOriginalImageWorkflowCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
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
                check("Workflow report folder exists", QDir().mkpath(folder));
                auto project = fixtureProject();
                const std::vector<QImage> originals{project.staffPages[0].sourceImage,
                                                    project.staffPages[1].sourceImage};
                const auto initialMusic = musicWithoutAnchors(project);
                const auto initialPlan = planRows(project);
                check("Synthetic full voices and true source anchors validate without notation rendering",
                      project.staffPerformance->notes.size() == 7 && project.score.writtenMeasures.size() == 2 &&
                          imageAnchorsValid(project) &&
                          projectToJson(project).value("schemaVersion").toInt() == 6);
                window.resize(1280, 880);
                window.show();
                window.setProject(project, false);
                QApplication::processEvents();
                auto *view = static_cast<ScoreView *>(window.findChild<QGraphicsView *>("scoreView"));
                auto *tabs = window.findChild<QTabWidget *>("staffScoreTabs");
                auto *play = window.findChild<QPushButton *>("play");
                auto *stop = window.findChild<QPushButton *>("stop");
                auto *primary = window.findChild<QCheckBox *>("melodyEnabled");
                auto *other = window.findChild<QCheckBox *>("accompanimentEnabled");
                auto *backend = window.findChild<QComboBox *>("audioBackend");
                auto *primaryProgram = window.findChild<QComboBox *>("programA");
                check("Production original mode has only the original-image viewport and mapped click callbacks",
                      view && tabs && play && stop && primary && other && backend && primaryProgram &&
                          tabs->currentIndex() == 0 && !tabs->isTabVisible(1) && !tabs->tabBar()->isVisible() &&
                          view->staffNoteClicked && scenePixels(*view, originals[0]));
                check("setProject and rebuild preserve complete music and all source pixels",
                      originalPixels(window.project(), originals) &&
                          musicWithoutAnchors(window.project()) == initialMusic &&
                          planRows(window.project()) == initialPlan);
                check("Unknown tempo blocks transport until explicit confirmation", !play->isEnabled());
                bool tempoAccepted = false;
                QTimer::singleShot(
                    0, &window,
                    [&tempoAccepted]
                    {
                        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                        if (dialog)
                        {
                            dialog->setDoubleValue(62);
                            dialog->accept();
                            tempoAccepted = true;
                        }
                        else if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                            modal->reject();
                    });
                window.confirmStaffTempo();
                check("Explicit source-tempo confirmation updates playback, not pixels or musical ticks",
                      tempoAccepted && window.project().score.bpm == 62 && play->isEnabled() &&
                          !window.project().processing.value("tempoNeedsConfirmation").toBool() &&
                          planRows(window.project()) == initialPlan &&
                          originalPixels(window.project(), originals));
                auto options = window.optionsContext();
                options.primaryStaffProgram = 40;
                options.otherStaffProgram = 35;
                options.score.bpm = 63;
                check("Options can rebuild the original-image score and change independent instruments",
                      window.applyOptions(window.appSettings(), options));
                options = window.optionsContext();
                options.score.bpm = 62;
                check("Options restores the explicit fixture tempo without drawing new notation",
                      window.applyOptions(window.appSettings(), options) &&
                          originalPixels(window.project(), originals) && imageAnchorsValid(window.project()) &&
                          planRows(window.project()) == initialPlan &&
                          window.project().staffPerformance->primaryProgram == 40 &&
                          window.project().staffPerformance->otherProgram == 35);
                backend->setCurrentIndex(1);
                check("Windows MIDI backend selected through the production UI",
                      window.player().audioBackend() == AudioBackend::WindowsMidi);
                window.player().setMetronome(false);
                view->ensureVisible(QRectF(350, 145, 45, 40), 20, 20);
                const auto previewCount = window.player().voiceState().previewNoteOns;
                mouseAt(*view, QPointF(372, 163));
                check("Real original-head mouse click seeks the actual full-staff event",
                      window.player().positionTicks() == 960 &&
                          waitUntil([&] { return window.player().voiceState().previewNoteOns > previewCount; }));
                check("Clicked source note auditions the chosen primary GM program",
                      window.player().voiceState().previewPitch == 62 &&
                          window.player().voiceState().previewProgram == 40);
                window.player().seek(0);
                play->click();
                check("Actual production transport starts original full-staff playback",
                      waitUntil(
                          [&]
                          {
                              const auto voices = window.player().voiceState();
                              return !window.isAudioLoading() && window.player().isPlaying() &&
                                     voices.chordPitches.size() == 1 && voices.bassPitches.size() == 3 &&
                                     visibleRectangles(*view, QColor("#12A78C")) == 3;
                          }));
                const auto voices = window.player().voiceState();
                check("Missing zero-bbox note still sounds without guide duplication or fabricated overlay",
                      voices.melodyNoteOns == 0 && voices.melodyPitch == -1 &&
                          std::find(voices.bassPitches.begin(), voices.bassPitches.end(), 43) !=
                              voices.bassPitches.end() &&
                          visibleRectangles(*view, QColor("#12A78C")) == 3 && scenePixels(*view, originals[0]));
                const auto bassNoteOns = voices.bassNoteOns;
                const auto primaryNoteOns = voices.chordNoteOns;
                primaryProgram->setCurrentIndex(primaryProgram->findData(41));
                check("Live instrument change recovers only its held hand without redrawing the original",
                      waitUntil(
                          [&]
                          {
                              const auto changed = window.player().voiceState();
                              return changed.chordNoteOns > primaryNoteOns && changed.chordPitches.size() == 1 &&
                                     changed.bassNoteOns == bassNoteOns && changed.bassPitches.size() == 3 &&
                                     window.project().staffPerformance->primaryProgram == 41 &&
                                     originalPixels(window.project(), originals);
                          }));
                primaryProgram->setCurrentIndex(primaryProgram->findData(40));
                primary->setChecked(false);
                check("Primary-hand mute releases only that hand and hides only its source cursor",
                      waitUntil(
                          [&]
                          {
                              return window.player().voiceState().chordPitches.empty() &&
                                     window.player().voiceState().bassPitches.size() == 3 &&
                                     visibleRectangles(*view, QColor("#12A78C")) == 2;
                          }));
                primary->setChecked(true);
                other->setChecked(false);
                check("Other-hand mute leaves primary sound and original-image highlight active",
                      waitUntil(
                          [&]
                          {
                              return window.player().voiceState().chordPitches.size() == 1 &&
                                     window.player().voiceState().bassPitches.empty() &&
                                     visibleRectangles(*view, QColor("#12A78C")) == 1;
                          }));
                other->setChecked(true);
                check("Both hands recover their held original notes without duplicated guide",
                      waitUntil(
                          [&]
                          {
                              return window.player().voiceState().bassPitches.size() == 3 &&
                                     visibleRectangles(*view, QColor("#12A78C")) == 3;
                          }));
                check("Playing screenshot captures unchanged original pixels",
                      originalPixels(window.project(), originals) &&
                          window.grab().save(folder + "/original-playing.png"));
                artifacts.append(folder + "/original-playing.png");
                window.player().seek(1980);
                check("Source-timeline playback automatically turns to the correct physical page",
                      waitUntil(
                          [&]
                          {
                              return window.staffPageIndex() == 1 &&
                                     visibleRectangles(*view, QColor("#12A78C")) == 2 &&
                                     scenePixels(*view, originals[1]);
                          }));
                play->click();
                check("Pause clears green while retaining a real selected blue source anchor",
                      waitUntil(
                          [&]
                          {
                              return !window.player().isPlaying() &&
                                     visibleRectangles(*view, QColor("#12A78C")) == 0 &&
                                     visibleRectangles(*view, QColor("#2463EB")) == 1;
                          }));
                const auto pausedTick = window.player().positionTicks();
                const auto pausedTransform = view->transform();
                QElapsedTimer pausedElapsed;
                pausedElapsed.start();
                check("Paused frame keeps position, zoom, page and original image",
                      waitUntil(
                          [&]
                          {
                              return pausedElapsed.elapsed() >= 80 &&
                                     window.player().positionTicks() == pausedTick &&
                                     view->transform() == pausedTransform && window.staffPageIndex() == 1 &&
                                     originalPixels(window.project(), originals);
                          }));
                window.setStaffPage(0);
                check("Manual page selection displays the same original instead of a rendered substitute",
                      scenePixels(*view, originals[0]) && originalPixels(window.project(), originals));
                stop->click();
                const auto saved = folder + "/original-two-page.jpp";
                const auto beforeSave = window.project();
                saveProject(saved, beforeSave);
                artifacts.append(saved);
                const auto loaded = loadProject(saved);
                check("Schema6 reopens all music, explicit anchor flags and exact source bitmaps",
                      projectToJson(loaded).value("schemaVersion").toInt() == 6 &&
                          originalPixels(loaded, originals) && imageAnchorsValid(loaded) &&
                          scoreToJson(loaded.score) == scoreToJson(beforeSave.score) &&
                          staffPerformanceToJson(*loaded.staffPerformance) ==
                              staffPerformanceToJson(*beforeSave.staffPerformance));
                window.setProject(loaded, false);
                window.openFile(saved);
                check("Production openFile retains original mode, chosen instruments and full plan",
                      originalPixels(window.project(), originals) && planRows(window.project()) == initialPlan &&
                          window.project().staffPerformance->primaryProgram == 40 &&
                          window.project().staffPerformance->otherProgram == 35);
                auto correctedNotes = loaded.staffPerformance->notes;
                const auto firstPrimary =
                    std::find_if(correctedNotes.begin(), correctedNotes.end(), [](const auto &note)
                                 { return note.staff == 1 && note.startTick == 0 && note.midiPitch == 60; });
                check("Synthetic original has the intended first primary note",
                      firstPrimary != correctedNotes.end());
                const auto sourceBox = firstPrimary->source;
                firstPrimary->midiPitch = 61;
                firstPrimary->durationTicks = 480;
                StaffPerformanceNote added;
                added.midiPitch = 48;
                added.staff = 2;
                added.voice = "2";
                added.durationTicks = 960;
                added.hasImageAnchor = false;
                correctedNotes.push_back(added);
                const auto corrected = correctedStaffProject(loaded, correctedNotes, loaded.score.writtenMeasures);
                const auto correctedPlan = planRows(corrected);
                const auto *edited = findNote(corrected, 61, 0);
                const auto *unbound = findNote(corrected, 48, 0);
                check("Original correction edits audible pitch/time but keeps the same measured paper bbox",
                      edited && edited->durationTicks == 480 && edited->hasImageAnchor &&
                          sameBox(edited->source, sourceBox) && originalPixels(corrected, originals) &&
                          imageAnchorsValid(corrected));
                check("Added note starts unbound with a zero bbox yet remains in the sounding plan",
                      unbound && !unbound->hasImageAnchor && sameBox(unbound->source, {}) &&
                          std::any_of(correctedPlan.begin(), correctedPlan.end(),
                                      [](const QJsonValue &row) { return row.toArray()[2].toInt() == 48; }));
                window.setProject(loaded, false);
                window.applyStaffCorrection(corrected);
                auto *undo = window.findChild<QAction *>("menuUndo");
                auto *redo = window.findChild<QAction *>("menuRedo");
                check("Original correction enters production Undo history", undo && redo && undo->isEnabled());
                undo->trigger();
                check("Undo restores exact original full events and anchor flags without redrawing",
                      staffPerformanceToJson(*window.project().staffPerformance) ==
                              staffPerformanceToJson(*loaded.staffPerformance) &&
                          scoreToJson(window.project().score) == scoreToJson(loaded.score) &&
                          originalPixels(window.project(), originals));
                redo->trigger();
                check("Redo restores corrected music and unchanged original bboxes/pixels",
                      staffPerformanceToJson(*window.project().staffPerformance) ==
                              staffPerformanceToJson(*corrected.staffPerformance) &&
                          originalPixels(window.project(), originals));
                StaffCorrectionDialog binding(corrected, &window);
                binding.show();
                QApplication::processEvents();
                auto *rows = binding.findChild<QTableWidget *>("staffCorrectionNotes");
                auto *bind = binding.findChild<QPushButton *>("staffCorrectionBindSource");
                auto *source =
                    static_cast<ScoreView *>(binding.findChild<QGraphicsView *>("staffCorrectionSourceView"));
                check("Original correction dialog exposes explicit source-point binding", rows && bind && source);
                int row = -1;
                for (int index = 0; index < rows->rowCount(); ++index)
                    if (rows->item(index, 0)->text() == "48")
                        row = index;
                check("Added chord tone appears in the correction dialog", row >= 0);
                rows->setCurrentCell(row, 0);
                bind->click();
                mouseAt(*source, QPointF(-15, 328), true);
                check("An out-of-image source-point click never fabricates an anchor",
                      rows->item(row, 9)->checkState() == Qt::Unchecked);
                source->ensureVisible(QRectF(138, 318, 28, 22), 10, 10);
                mouseAt(*source, QPointF(152, 328), true);
                check("An explicit source-head point sets a finite image-local anchor",
                      rows->item(row, 9)->checkState() == Qt::Checked && scenePixels(*source, originals[0]));
                binding.accept();
                check("Source-point binding validates without replacing any original pixels",
                      binding.correctedProject().has_value());
                const auto boundProject = *binding.correctedProject();
                const auto *bound = findNote(boundProject, 48, 0);
                check("Hand-bound source coordinates retain actual music, page identity and finite bounds",
                      bound && bound->hasImageAnchor && bound->source.width > 0 && bound->source.height > 0 &&
                          imageAnchorsValid(boundProject) && originalPixels(boundProject, originals) &&
                          planRows(boundProject) == planRows(corrected));
                window.applyStaffCorrection(boundProject);
                undo->trigger();
                check("Undo of binding restores unanchored zero bbox, not a generated coordinate",
                      findNote(window.project(), 48, 0) && !findNote(window.project(), 48, 0)->hasImageAnchor &&
                          sameBox(findNote(window.project(), 48, 0)->source, {}) &&
                          originalPixels(window.project(), originals));
                redo->trigger();
                check("Redo of binding restores the exact explicit source rectangle",
                      findNote(window.project(), 48, 0) &&
                          sameBox(findNote(window.project(), 48, 0)->source, bound->source) &&
                          originalPixels(window.project(), originals));
                saveProject(folder + "/original-corrected-bound.jpp", window.project());
                artifacts.append(folder + "/original-corrected-bound.jpp");
                check("Final corrected original-image screenshot saved",
                      window.grab().save(folder + "/original-corrected.png"));
                artifacts.append(folder + "/original-corrected.png");

                const bool anyNative =
                    args.isSet("original-source-musicxml") || args.isSet("original-source-image");
                if (anyNative)
                {
                    check("Native fixture includes MusicXML and original raster",
                          args.isSet("original-source-musicxml") && args.isSet("original-source-image"));
                    QFile xml(args.value("original-source-musicxml"));
                    check("Same-run native MusicXML is bounded and readable",
                          xml.open(QIODevice::ReadOnly) && xml.size() > 0 && xml.size() <= 16 * 1024 * 1024);
                    const auto bytes = xml.readAll();
                    const auto imported = parseMusicXml(bytes, xml.fileName());
                    const auto converted = musicXmlToPerformance(imported, 0, true);
                    check("Same-run native full voices validate in explicit unresolved-tie review mode",
                          imported.valid() && converted.valid());
                    QImage sourceImage(args.value("original-source-image"));
                    check("Native original source raster is readable", !sourceImage.isNull());
                    sourceImage = sourceImage.convertToFormat(QImage::Format_RGB32);
                    Project native;
                    native.score = *converted.selectedMelody.score;
                    native.staffPerformance = converted.performance;
                    native.notationStyle = NotationStyle::Staff;
                    native.staffImagePlayback = true;
                    native.generatedNotation = false;
                    native.practiceMix.accompanimentEnabled = true;
                    native.practiceSettings = ProjectPracticeSettings{};
                    native.practiceSettings->metronome = false;
                    native.processing = {{"local", true}, {"recognitionAccuracyMeasured", false}};
                    native.warnings = imported.warnings + converted.warnings;
                    check("This same-run optional fixture has one real source page",
                          std::all_of(native.score.writtenMeasures.begin(), native.score.writtenMeasures.end(),
                                      [](const auto &measure) { return measure.pageIndex == 0; }));
                    native.image = sourceImage;
                    native.staffPages.push_back({"Same-run native original", sourceImage, sourceImage, 0,
                                                 native.staffPerformance->durationTicks});
                    const auto beforeMusic = musicWithoutAnchors(native);
                    const auto beforePlan = planRows(native);
                    const auto anchorReport = applyCrispStaffSourceAnchors({sourceImage}, imported, native.score,
                                                                           *native.staffPerformance);
                    metrics.insert("nativeAnchorError", anchorReport.error);
                    check("Native image matching supplies verified original-image anchors: " + anchorReport.error,
                          anchorReport.error.isEmpty() && anchorReport.anchoredNotes > 0 &&
                              imageAnchorsValid(native));
                    check("Geometry matching changes no recognized notes, written clock or sounding plan",
                          musicWithoutAnchors(native) == beforeMusic && planRows(native) == beforePlan);
                    native.warnings += anchorReport.warnings;
                    native.processing.insert("sourceAnchorProvenance", "native-image-notehead-sequence");
                    QJsonArray anchorRows;
                    for (std::size_t index = 0; index < native.staffPerformance->notes.size(); ++index)
                    {
                        const auto &note = native.staffPerformance->notes[index];
                        if (note.hasImageAnchor)
                            anchorRows.append(
                                QJsonObject{{"event", int(index)},
                                            {"page", note.pageIndex},
                                            {"staff", note.staff},
                                            {"voice", QString::fromStdString(note.voice)},
                                            {"startTick", double(note.startTick)},
                                            {"bbox", QJsonArray{note.source.x, note.source.y, note.source.width,
                                                                note.source.height}}});
                    }
                    metrics.insert("sourceAnchorMap", QJsonObject{{"anchored", anchorReport.anchoredNotes},
                                                                  {"unanchored", anchorReport.unanchoredNotes},
                                                                  {"accuracyMeasured", false},
                                                                  {"rows", anchorRows}});
                    if (args.isSet("original-source-bpm"))
                    {
                        bool bpmValid = false;
                        const auto bpm = args.value("original-source-bpm").toDouble(&bpmValid);
                        check("Native fixture tempo comes from an explicit source input",
                              bpmValid && std::isfinite(bpm) && bpm >= 10 && bpm <= 400);
                        native.score.bpm = bpm;
                        native.processing.insert("tempoSource", "explicit-cli-source-input");
                        native.processing.insert("confirmedQuarterBpm", bpm);
                    }
                    else
                        check("Native playback requires written tempo or explicit --original-source-bpm",
                              !imported.tempos.empty());
                    native.staffPerformance->primaryProgram = 40;
                    native.staffPerformance->otherProgram = 35;
                    const auto nativePath = folder + "/native-original-image.jpp";
                    saveProject(nativePath, native);
                    const auto reopenedNative = loadProject(nativePath);
                    artifacts.append(nativePath);
                    check("Native original-image project reopens with unchanged exact source pixels and full "
                          "recognized plan",
                          originalPixels(reopenedNative, {sourceImage}) &&
                              planRows(reopenedNative) == planRows(native) && imageAnchorsValid(reopenedNative));
                    window.setProject(reopenedNative, false);
                    check("Native original workflow uses real source pixels after production rebuild",
                          scenePixels(*view, sourceImage));
                    check("Native Windows MIDI backend starts explicitly",
                          window.player().setAudioBackend(AudioBackend::WindowsMidi));
                    const auto firstAnchored =
                        std::find_if(native.staffPerformance->notes.begin(), native.staffPerformance->notes.end(),
                                     [](const auto &note) { return note.hasImageAnchor; });
                    window.player().seek(firstAnchored->startTick);
                    play->click();
                    check("Native recognized full voices play while highlighting same-run source coordinates",
                          waitUntil(
                              [&]
                              {
                                  return !window.isAudioLoading() && window.player().isPlaying() &&
                                         visibleRectangles(*view, QColor("#12A78C")) > 0;
                              }));
                    check("Native playback does not duplicate guide or replace original pixels",
                          window.player().voiceState().melodyNoteOns == 0 &&
                              originalPixels(window.project(), {sourceImage}) && scenePixels(*view, sourceImage));
                    check("Native original-image playback screenshot saved",
                          window.grab().save(folder + "/native-original-playing.png"));
                    artifacts.append(folder + "/native-original-playing.png");
                    stop->click();
                    metrics.insert("nativeNotes", int(native.staffPerformance->notes.size()));
                    metrics.insert("nativeGuideNotes", int(native.score.notes.size()));
                    metrics.insert("nativeRecognizedBpm", converted.selectedMelody.score->bpm);
                    metrics.insert("nativePlaybackBpm", native.score.bpm);
                    metrics.insert("nativeDurationTicks", double(native.staffPerformance->durationTicks));
                }
                metrics.insert(
                    "audioEvidence",
                    "Windows MIDI device note submission and production transport state; not an acoustic capture");
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

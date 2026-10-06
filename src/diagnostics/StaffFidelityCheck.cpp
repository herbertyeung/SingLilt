// Staff notation and timing checks against explicit references.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffFidelityCheck.h"
#include "application/StaffScoreCorrection.h"
#include "audio/WaveRenderer.h"
#include "recognition/StaffTempoRecognizer.h"
#include "storage/MusicXmlImporter.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/RecognitionPreviewDialog.h"
#include "ui/ScoreView.h"
#include "ui/StaffCorrectionDialog.h"
#include "ui/StaffRenderer.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace singlilt
{
namespace
{
bool scenePixels(QGraphicsView *view, const QImage &expected)
{
    if (!view || !view->scene())
        return false;
    for (auto *item : view->scene()->items())
        if (auto *image = qgraphicsitem_cast<QGraphicsPixmapItem *>(item))
            if (image->pixmap().toImage().convertToFormat(QImage::Format_RGB32) ==
                expected.convertToFormat(QImage::Format_RGB32))
                return true;
    return false;
}
} // namespace
void runStaffFidelityCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        0, &window,
        [&window, &args, &app]
        {
            QJsonArray checks;
            QString error;
            const auto check = [&](const QString &name, bool passed)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", passed}});
                if (!passed)
                    throw std::runtime_error(name.toStdString());
            };
            bool passed = false;
            try
            {
                const auto folder = QFileInfo(args.value("report")).absolutePath();
                check("Fidelity report folder exists", QDir().mkpath(folder));
                const auto imported = parseMusicXml(R"XML(<score-partwise version="4.0">
<part-list><score-part id="P1"><part-name>Hands</part-name></score-part></part-list><part id="P1">
<measure number="1"><attributes><divisions>2</divisions><key><fifths>0</fifths></key>
<time><beats>4</beats><beat-type>4</beat-type></time><staves>2</staves>
<clef number="1"><sign>G</sign><line>2</line></clef><clef number="2"><sign>F</sign><line>4</line></clef></attributes>
<note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration><voice>1</voice><staff>1</staff><stem>up</stem><beam number="1">begin</beam></note>
<note><pitch><step>D</step><octave>4</octave></pitch><duration>1</duration><voice>1</voice><staff>1</staff><stem>up</stem><beam number="1">end</beam></note>
<note><pitch><step>E</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice><staff>1</staff></note>
<note><pitch><step>F</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><staff>1</staff></note>
<backup><duration>8</duration></backup>
<note><pitch><step>A</step><octave>2</octave></pitch><duration>4</duration><voice>2</voice><staff>2</staff></note>
<note><chord/><pitch><step>E</step><octave>3</octave></pitch><duration>4</duration><voice>2</voice><staff>2</staff></note>
<note><pitch><step>B</step><octave>2</octave></pitch><duration>4</duration><voice>2</voice><staff>2</staff></note>
<note><chord/><pitch><step>F</step><alter>1</alter><octave>3</octave></pitch><duration>4</duration><voice>2</voice><staff>2</staff></note>
</measure><measure number="2"><print new-page="yes"/>
<note><pitch><step>G</step><octave>4</octave></pitch><duration>8</duration><voice>1</voice><staff>1</staff></note>
<backup><duration>8</duration></backup>
<note><pitch><step>A</step><octave>2</octave></pitch><duration>8</duration><voice>2</voice><staff>2</staff></note>
</measure></part></score-partwise>)XML");
                check("Full staff fixture imports", imported.valid());
                const auto converted = musicXmlToPerformance(imported, 0);
                check("Complete voices convert without inferring notes", converted.valid());
                Project project;
                project.score = *converted.selectedMelody.score;
                project.staffPerformance = converted.performance;
                project.generatedNotation = true;
                project.notationStyle = NotationStyle::Staff;
                project.processing = {{"local", true}, {"tempoNeedsConfirmation", true}, {"tempoSuggestion", 62}};
                const auto images = renderGrandStaffPages(project.score, *project.staffPerformance);
                check("Each physical page has one rendered image", images.size() == 2);
                for (int page = 0; page < 2; ++page)
                {
                    QImage source(800, 1100, QImage::Format_RGB32);
                    source.fill(page == 0 ? Qt::white : QColor("#EEF3F8"));
                    QPainter painter(&source);
                    painter.setPen(Qt::black);
                    painter.drawText(35, 50, QString("Original page %1, quarter=62").arg(page + 1));
                    painter.end();
                    project.staffPages.push_back({QString("Source %1").arg(page + 1), source, images[page],
                                                  page * 1920, (page + 1) * 1920});
                }
                project.image = images.front();
                const auto musicBefore = staffPerformanceToJson(*project.staffPerformance);
                const auto ticksBefore = buildTimeline(project.score).durationTicks;
                RecognitionPreviewDialog preview(project, "Explicit review fixture", &window);
                preview.setApplyEnabled(true);
                auto *apply = preview.findChild<QPushButton *>("recognitionApply");
                auto *bpm = preview.findChild<QDoubleSpinBox *>("staffTempoReview");
                auto *confirmed = preview.findChild<QCheckBox *>("staffTempoConfirmed");
                auto *previewTabs = preview.findChild<QTabWidget *>("localStaffImageTabs");
                check("Unknown native tempo requires an explicit review, not a silent90 default",
                      apply && bpm && confirmed && !apply->isEnabled() && !preview.projectForApply());
                check("Local preview starts on the actual original pixels",
                      previewTabs && previewTabs->currentIndex() == 1 &&
                          scenePixels(preview.findChild<QGraphicsView *>("localStaffOriginalView"),
                                      project.staffPages[0].sourceImage));
                window.setProject(project, false);
                auto *pendingExport = window.findChild<QAction *>("menuExportWave");
                check("Pending tempo still has an explicit export action", pendingExport != nullptr);
                pendingExport->trigger();
                check("Pending tempo never opens the WAV export dialog at a guessed speed",
                      QApplication::activeModalWidget() == nullptr &&
                          !pendingExport->property("waveExportRunning").toBool());
                bpm->setValue(62);
                confirmed->setChecked(true);
                const auto reviewed = preview.projectForApply();
                check("Confirmed source tempo changes the playback clock",
                      apply->isEnabled() && reviewed && reviewed->score.bpm == 62 &&
                          !reviewed->processing.value("tempoNeedsConfirmation").toBool());
                check("Tempo confirmation changes no pitches, ticks, stems or beams",
                      staffPerformanceToJson(*reviewed->staffPerformance) == musicBefore &&
                          buildTimeline(reviewed->score).durationTicks == ticksBefore);
                bpm->setValue(63);
                check("Editing tempo requires renewed confirmation",
                      !apply->isEnabled() && !preview.projectForApply());
                window.setProject(*reviewed, false);
                auto *tabs = window.findChild<QTabWidget *>("staffScoreTabs");
                auto *sourceView = static_cast<ScoreView *>(window.findChild<QGraphicsView *>("staffSourceView"));
                check("Applying a local candidate does not replace the default original view",
                      tabs && sourceView && tabs->currentIndex() == 1 &&
                          scenePixels(sourceView, reviewed->staffPages[0].sourceImage));
                check("Original view has no fabricated note click coordinates",
                      !sourceView->staffNoteClicked && !sourceView->noteClicked);
                window.setStaffPage(1);
                check("Page turning updates the correct original, not another page's anchors",
                      scenePixels(sourceView, reviewed->staffPages[1].sourceImage));
                tabs->setCurrentIndex(0);
                check("The recognized view remains separately accessible",
                      scenePixels(window.findChild<QGraphicsView *>("scoreView"),
                                  window.project().staffPages[1].renderedImage));
                const auto saved = QDir(folder).filePath("fidelity-original-and-recognized.jpp");
                saveProject(saved, window.project());
                const auto loaded = loadProject(saved);
                check("Original and candidate notation both roundtrip",
                      loaded.staffPages.size() == 2 &&
                          loaded.staffPages[0].sourceImage == reviewed->staffPages[0].sourceImage &&
                          loaded.staffPages[1].sourceImage == reviewed->staffPages[1].sourceImage &&
                          staffPerformanceToJson(*loaded.staffPerformance) ==
                              staffPerformanceToJson(*window.project().staffPerformance));
                check("Confirmed tempo and its provenance persist",
                      loaded.score.bpm == 62 &&
                          loaded.processing.value("tempoSource").toString() == "user-confirmed");
                const auto plan =
                    buildStaffPerformancePlan(loaded.score, buildTimeline(loaded.score), *loaded.staffPerformance);
                check("Written opening left-hand chord still attacks at zero",
                      plan.valid() && std::any_of(plan.events.begin(), plan.events.end(),
                                                  [](const auto &event)
                                                  {
                                                      return event.midiPitch == 45 && event.startTick == 0 &&
                                                             event.durationTicks == 960;
                                                  }));
                check("Explicit quarter-note header is only a tempo suggestion",
                      parseQuarterTempoText(QString(QChar(0x2669)) + " = 62").quarterBpm == 62);
                check("A page number alone is never a tempo", !parseQuarterTempoText("62").quarterBpm);
                check("A dotted/other-unit header is not silently quarter-normalized",
                      !parseQuarterTempoText(QString(QChar(0x2669)) + ". = 62").quarterBpm);
                QImage transparent(120, 80, QImage::Format_ARGB32);
                transparent.fill(Qt::transparent);
                transparent.setPixelColor(30, 40, Qt::black);
                const auto normalized = makeStaffInputCanvas(transparent);
                check("Transparent clipboard backgrounds become white only in the engine copy",
                      normalized.format() == QImage::Format_RGB32 && normalized.pixelColor(0, 0) == Qt::white &&
                          normalized.pixelColor(30, 40) == Qt::black && transparent.pixelColor(0, 0).alpha() == 0);
                {
                    auto longMeasures = loaded.score.writtenMeasures;
                    longMeasures[0].durationTicks = 2400;
                    longMeasures[1].startTick = 2400;
                    auto longNotes = loaded.staffPerformance->notes;
                    for (auto &note : longNotes)
                        if (note.startTick >= 1920)
                            note.startTick += 480;
                    const auto overfull = correctedStaffProject(loaded, longNotes, longMeasures);
                    check("An explicit five-beat candidate retains its source images and edited clock",
                          buildTimeline(overfull.score).durationTicks == 4320 &&
                              overfull.staffPages[1].startTick == 2400 &&
                              overfull.staffPages[0].sourceImage == loaded.staffPages[0].sourceImage);
                    auto correctedMeasures = overfull.score.writtenMeasures;
                    correctedMeasures[0].durationTicks = 1920;
                    auto correctedNotes = overfull.staffPerformance->notes;
                    for (auto &note : correctedNotes)
                        if (note.startTick >= 2400)
                            note.startTick -= 480;
                    auto added = correctedNotes.front();
                    added.midiPitch = 48;
                    added.staff = 2;
                    added.voice = "2";
                    added.startTick = 0;
                    added.durationTicks = 960;
                    added.sourceNoteIndex = -1;
                    added.staffSpelling.reset();
                    added.beams.clear();
                    added.tieStart = added.tieStop = added.unresolvedSoundTie = false;
                    correctedNotes.push_back(added);
                    correctedNotes.front().midiPitch = 61;
                    const auto fixed = correctedStaffProject(overfull, correctedNotes, correctedMeasures);
                    const auto fixedPlan = buildStaffPerformancePlan(fixed.score, buildTimeline(fixed.score),
                                                                     *fixed.staffPerformance);
                    check("Correcting an overlong bar synchronizes all later notes and page ranges",
                          fixedPlan.valid() && fixedPlan.durationTicks == 3840 &&
                              fixed.staffPages[1].startTick == 1920);
                    check("Adding a missing chord tone changes the actual performance, not only its picture",
                          std::any_of(fixedPlan.events.begin(), fixedPlan.events.end(),
                                      [](const auto &event)
                                      {
                                          return event.midiPitch == 48 && event.startTick == 0 &&
                                                 event.durationTicks == 960;
                                      }));
                    check("Edited absolute MIDI pitch appears in the audible plan",
                          std::any_of(fixedPlan.events.begin(), fixedPlan.events.end(), [](const auto &event)
                                      { return event.midiPitch == 61 && event.startTick == 0; }));
                    check("Manual corrections keep their provenance and original pixels",
                          fixed.processing.value("manualStaffCorrection").isObject() &&
                              fixed.staffPages[0].sourceImage == loaded.staffPages[0].sourceImage &&
                              fixed.staffPages[1].sourceImage == loaded.staffPages[1].sourceImage);
                    auto deleted = fixed.staffPerformance->notes;
                    std::erase_if(deleted,
                                  [](const auto &note) { return note.staff == 2 && note.midiPitch == 48; });
                    const auto withoutAdded = correctedStaffProject(fixed, deleted, fixed.score.writtenMeasures);
                    check("Deleting a wrong chord tone updates the complete voices",
                          withoutAdded.staffPerformance->notes.size() + 1 == fixed.staffPerformance->notes.size());
                    bool rejected = false;
                    auto invalid = correctedNotes;
                    invalid.front().durationTicks = 2401;
                    try
                    {
                        correctedStaffProject(overfull, invalid, correctedMeasures);
                    }
                    catch (const std::exception &)
                    {
                        rejected = true;
                    }
                    check("A corrected note extending outside its edited bar is rejected", rejected);
                    rejected = false;
                    try
                    {
                        staffCorrectionTicks(std::numeric_limits<double>::quiet_NaN());
                    }
                    catch (const std::exception &)
                    {
                        rejected = true;
                    }
                    check("Nonfinite editor timing is rejected", rejected);
                    window.setProject(overfull, false);
                    const auto originalFull = staffPerformanceToJson(*window.project().staffPerformance);
                    StaffCorrectionDialog cancelled(window.project(), &window);
                    cancelled.reject();
                    check("Cancelling correction does not mutate the open project",
                          !cancelled.correctedProject() &&
                              staffPerformanceToJson(*window.project().staffPerformance) == originalFull);
                    {
                        StaffCorrectionDialog editor(overfull, &window);
                        auto *notes = editor.findChild<QTableWidget *>("staffCorrectionNotes");
                        auto *bars = editor.findChild<QTableWidget *>("staffCorrectionMeasures");
                        auto *add = editor.findChild<QPushButton *>("staffCorrectionAddNote");
                        check("Correction dialog exposes the actual full voices and written bars",
                              notes && bars && add &&
                                  notes->rowCount() == int(overfull.staffPerformance->notes.size()));
                        notes->item(0, 0)->setText("61");
                        bars->item(0, 4)->setText("4");
                        add->click();
                        const int addedRow = notes->rowCount() - 1;
                        notes->item(addedRow, 0)->setText("48");
                        notes->item(addedRow, 1)->setText("1");
                        notes->item(addedRow, 2)->setText("0");
                        notes->item(addedRow, 3)->setText("2");
                        notes->item(addedRow, 4)->setText("2");
                        notes->item(addedRow, 5)->setText("2");
                        editor.accept();
                        check("Actual correction controls commit edited pitch, missing chord and bar length",
                              editor.result() == QDialog::Accepted && editor.correctedProject() &&
                                  editor.correctedProject()->staffPerformance->notes.size() ==
                                      overfull.staffPerformance->notes.size() + 1 &&
                                  buildTimeline(editor.correctedProject()->score).durationTicks == 3840);
                        check("Correction dialog is still copy-on-apply, not an implicit main-window mutation",
                              staffPerformanceToJson(*window.project().staffPerformance) == originalFull);
                        check("Actual correction dialog screenshot saved",
                              editor.grab().save(QDir(folder).filePath("staff-correction-controls.png")));
                    }
                    {
                        StaffCorrectionDialog invalidEditor(overfull, &window);
                        auto *notes = invalidEditor.findChild<QTableWidget *>("staffCorrectionNotes");
                        notes->item(0, 3)->setText("10");
                        invalidEditor.accept();
                        auto *errorLabel = invalidEditor.findChild<QLabel *>("staffCorrectionError");
                        check("Invalid UI timing stays unapplied with a visible validation error",
                              !invalidEditor.correctedProject() && invalidEditor.result() != QDialog::Accepted &&
                                  errorLabel && !errorLabel->text().isEmpty());
                    }
                    window.applyStaffCorrection(fixed);
                    auto *undo = window.findChild<QAction *>("menuUndo");
                    auto *redo = window.findChild<QAction *>("menuRedo");
                    check("A full-staff correction is one undoable material change",
                          undo && redo && undo->isEnabled() && window.hasUnsavedChanges());
                    undo->trigger();
                    check("Undo restores the complete previous music and page clock",
                          staffPerformanceToJson(*window.project().staffPerformance) == originalFull &&
                              window.timeline().durationTicks == 4320 && !window.hasUnsavedChanges());
                    redo->trigger();
                    check("Redo restores corrected voices, pages and duration",
                          window.timeline().durationTicks == 3840 &&
                              window.project().staffPerformance->notes.size() ==
                                  fixed.staffPerformance->notes.size());
                    auto *program = window.findChild<QComboBox *>("programA");
                    check("Independent hand program selector is available", program && program->findData(40) >= 0);
                    program->setCurrentIndex(program->findData(40));
                    undo->trigger();
                    check("Undoing note corrections preserves a later independent instrument choice",
                          window.project().staffPerformance->primaryProgram == 40 &&
                              window.timeline().durationTicks == 4320);
                    redo->trigger();
                    check("Redo preserves the later instrument while restoring corrected music",
                          window.project().staffPerformance->primaryProgram == 40 &&
                              window.timeline().durationTicks == 3840);
                    saveProject(QDir(folder).filePath("fidelity-corrected.jpp"), window.project());
                    check("Corrected full staff still saves and reopens",
                          loadProject(QDir(folder).filePath("fidelity-corrected.jpp"))
                                  .staffPerformance->notes.size() == fixed.staffPerformance->notes.size());
                    window.setProject(overfull, false);
                    QString modalError;
                    bool modalAccepted = false;
                    QTimer::singleShot(
                        0, &window,
                        [&]
                        {
                            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                            try
                            {
                                if (!dialog || dialog->objectName() != "staffCorrectionDialog")
                                    throw std::runtime_error("The correction menu did not open its dialog");
                                auto *notes = dialog->findChild<QTableWidget *>("staffCorrectionNotes");
                                auto *bars = dialog->findChild<QTableWidget *>("staffCorrectionMeasures");
                                bars->item(0, 4)->setText("4");
                                notes->item(0, 0)->setText("61");
                                dialog->findChild<QPushButton *>("staffCorrectionAddNote")->click();
                                const int row = notes->rowCount() - 1;
                                for (const auto &[column, text] : std::vector<std::pair<int, QString>>{
                                         {0, "48"}, {1, "1"}, {2, "0"}, {3, "2"}, {4, "2"}, {5, "2"}})
                                    notes->item(row, column)->setText(text);
                                dialog->grab().save(QDir(folder).filePath("actual-staff-correction-dialog.png"));
                                dialog->findChild<QPushButton *>("staffCorrectionApply")->click();
                                modalAccepted = !dialog->isVisible();
                                if (!modalAccepted)
                                    throw std::runtime_error(
                                        dialog->findChild<QLabel *>("staffCorrectionError")->text().toStdString());
                            }
                            catch (const std::exception &exception)
                            {
                                modalError = QString::fromUtf8(exception.what());
                                if (dialog)
                                    dialog->reject();
                            }
                        });
                    window.findChild<QAction *>("menuStaffCorrection")->trigger();
                    check("Actual menu-dialog-apply commits full-staff corrections",
                          modalError.isEmpty() && modalAccepted && window.hasUnsavedChanges() &&
                              window.timeline().durationTicks == 3840 &&
                              window.project().staffPerformance->notes.size() ==
                                  fixed.staffPerformance->notes.size());
                    window.findChild<QAction *>("menuUndo")->trigger();
                    check("Actual modal correction is one recoverable undo step",
                          window.timeline().durationTicks == 4320 && !window.hasUnsavedChanges());
                }
                check("Fidelity screenshot saved",
                      window.grab().save(QDir(folder).filePath("fidelity-views.png")));
                if (args.isSet("staff-fidelity-reference"))
                {
                    const auto reference = importMusicXml(args.value("staff-fidelity-reference"));
                    const auto referenceMusic = musicXmlToPerformance(reference, 0);
                    check("Human-transcribed first-three-bar reference validates",
                          reference.valid() && referenceMusic.valid());
                    Project manual;
                    manual.score = *referenceMusic.selectedMelody.score;
                    manual.staffPerformance = referenceMusic.performance;
                    manual.generatedNotation = true;
                    manual.notationStyle = NotationStyle::Staff;
                    manual.staffKeyFifths = referenceMusic.selectedMelody.keyFifths;
                    manual.processing = {{"local", true},
                                         {"musicSource", "manual-first-three-reference"},
                                         {"recognitionAccuracyMeasured", false},
                                         {"manualBarCount", 3},
                                         {"sourceMusicXmlSHA256", QString::fromLatin1(reference.sourceSha256)}};
                    manual.warnings = {"ui.fidelity.manual_reference"};
                    const auto rendered = renderGrandStaffPages(manual.score, *manual.staffPerformance,
                                                                {false, manual.staffKeyFifths, false});
                    const QImage original(args.value("staff-fidelity-original"));
                    check("Reference has the supplied original page", !original.isNull() && rendered.size() == 1);
                    const QRect sourceCrop(60, 205, 720, 156);
                    check("Human reference crop is inside the supplied source",
                          original.rect().contains(sourceCrop));
                    manual.image = rendered.front();
                    manual.staffPages.push_back({"Original first three bars", original.copy(sourceCrop),
                                                 rendered.front(), 0, manual.staffPerformance->durationTicks});
                    manual.processing.insert("sourceCrop", QJsonArray{60, 205, 720, 156});
                    const auto path = QDir(folder).filePath("first-three-manual-reference.jpp");
                    saveProject(path, manual);
                    const auto timeline = buildTimeline(manual.score);
                    const auto referencePlan = buildStaffPerformancePlan(manual.score, timeline, *manual.staffPerformance);
                    check("Manual first-three reference has the explicit common5760-tick clock",
                          referencePlan.valid() && timeline.durationTicks == 5760 && manual.score.bpm == 62);
                    WaveRenderOptions options;
                    options.maxSeconds = 600;
                    options.settings.originalStaff = true;
                    options.settings.chordProgram = manual.staffPerformance->primaryProgram;
                    options.settings.bassProgram = manual.staffPerformance->otherProgram;
                    const auto wave =
                        renderWave(manual.score, timeline, referencePlan,
                                   QDir(folder).filePath("first-three-manual-reference.wav"), options);
                    check("Actual corrected reference WAV covers the complete three bars without clipping",
                          !wave.fragment && wave.clippedSamples == 0 && wave.rms > 0);
                    window.setProject(std::move(manual), false);
                    window.grab().save(QDir(folder).filePath("first-three-original-view.png"));
                    window.findChild<QTabWidget *>("staffScoreTabs")->setCurrentIndex(0);
                    window.grab().save(QDir(folder).filePath("first-three-recognized-view.png"));
                }
                passed = true;
            }
            catch (const std::exception &exception)
            {
                error = QString::fromUtf8(exception.what());
            }
            const QJsonObject report{
                {"passed", passed}, {"checks", checks}, {"error", error}, {"recognitionAccuracyMeasured", false}};
            QFile file(args.value("report"));
            const auto bytes = QJsonDocument(report).toJson();
            const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
            app.exit(written ? (passed ? 0 : 3) : 4);
        });
}
} // namespace singlilt

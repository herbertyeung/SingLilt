// Regression cases from the numbered-authoring code review.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT
#include "NumberedReviewCheck.h"
#include "cli/JsonReport.h"
#include "domain/NumberedEditing.h"
#include "i18n/LanguageManager.h"
#include "ui/MainWindow.h"
#include "ui/NotationRenderer.h"
#include "ui/ScoreView.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsSimpleTextItem>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include "ui/ScorePropertiesPanel.h"
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

namespace singlilt
{
void runNumberedReviewCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        100, &window,
        [&]
        {
            QJsonArray checks;
            QJsonObject metrics;
            bool passed = true;
            const auto folder = QFileInfo(args.value("report")).absolutePath();
            QDir().mkpath(folder);
            auto check = [&](const char *name, bool condition)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", condition}});
                passed &= condition;
            };
            auto attempt = [&](const char *name, const auto &operation)
            {
                try
                {
                    operation();
                }
                catch (const std::exception &error)
                {
                    passed = false;
                    checks.append(QJsonObject{
                        {"name", name}, {"passed", false}, {"error", QString::fromUtf8(error.what())}});
                }
            };
            auto fixture = [](bool multi = true, int bars = 4)
            {
                Project project;
                project.generatedNotation = true;
                project.score.title = "Review regression";
                project.score.editorMinimumBars = bars;
                Note first, second;
                second.degree = 3;
                project.score.notes = {first, second};
                if (multi)
                    project.numberedComposition =
                        NumberedComposition{{{1, "Upper", project.score.notes}, {2, "Lower", {first}}}, 3};
                return project;
            };
            auto setup = [&](Project project)
            {
                window.setProject(std::move(project));
                window.setCorrectionMode(true);
                window.selectNote(0, false);
                QApplication::processEvents();
            };
            auto click = [&](const char *name) { window.findChild<QAbstractButton *>(name)->click(); };
            auto cancelDialogs = [&](const auto &operation)
            {
                int count = 0;
                QTimer timer;
                QObject::connect(&timer, &QTimer::timeout, &window,
                                 [&]
                                 {
                                     if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                                     {
                                         ++count;
                                         if (auto *message = qobject_cast<QMessageBox *>(dialog))
                                         {
                                             if (auto *cancel = message->button(QMessageBox::Cancel))
                                                 cancel->click();
                                             else
                                                 message->reject();
                                         }
                                         else
                                             dialog->reject();
                                     }
                                 });
                timer.start(1);
                operation();
                return count;
            };
            attempt("capabilities",
                    [&]
                    {
                        setup(fixture());
                        window.refreshNotationControls();
                        check("Repeat menu stays disabled after generic refresh",
                              !window.findChild<QAction *>("menuRepeats")->isEnabled());
                        check("Staff conversion stays disabled after generic refresh",
                              !window.findChild<QAction *>("menuStaffView")->isEnabled());
                        auto *style = window.findChild<QComboBox *>("notationStyle");
                        style->setCurrentIndex(style->findData(int(NotationStyle::Staff)));
                        check("Rejected Staff selection keeps the view selector on Numbered",
                              style->currentData().toInt() == int(NotationStyle::Numbered));
                        auto withArrangement = fixture();
                        withArrangement.accompaniment = generateAccompaniment(withArrangement.score, {});
                        setup(withArrangement);
                        auto *pattern = window.findChild<QComboBox *>("accompanimentPattern");
                        check("An authored composition disables the legacy accompaniment selector",
                              !pattern->isEnabled());
                        const auto originalPattern = window.project_.accompaniment->settings.pattern;
                        pattern->setCurrentIndex((pattern->currentIndex() + 1) % pattern->count());
                        check("A retained arrangement cannot replace the composed voice plan",
                              window.project_.accompaniment->settings.pattern == originalPattern);
                        check("Shared instrument label survives refresh",
                              window.findChild<QLabel *>("programALabel")->text() ==
                                  trText("ui.editor.shared_sound"));
                        QPushButton *repeat = nullptr;
                        for (auto *button : window.findChildren<QPushButton *>())
                            if (button->property("_ui_text").toByteArray() == "ui.inspector.repeats")
                                repeat = button;
                        check("Inspector repeat control is disabled", repeat && !repeat->isEnabled());
                        const auto before = scoreToJson(window.project_.score);
                        const int count = cancelDialogs([&] { window.editRepeats(); });
                        check("Repeat entry rejects composition before opening a dialog",
                              count == 0 && before == scoreToJson(window.project_.score));
                    });
            attempt("draft and note edits compose",
                    [&]
                    {
                        setup(fixture());
                        QTimer responder;
                        QObject::connect(
                            &responder, &QTimer::timeout, &window,
                            []
                            {
                                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                                if (auto *message = qobject_cast<QMessageBox *>(dialog))
                                    message->button(QMessageBox::Apply)->click();
                                else if (dialog)
                                {
                                    if (auto *text = dialog->findChild<QPlainTextEdit *>("numberedLyricsText"))
                                        text->setPlainText("L");
                                    dialog->accept();
                                }
                            });
                        responder.start(5);
                        window.duration_->setValue(.5);
                        window.findChild<QAction *>("menuLyrics")->trigger();
                        check("Lyrics command preserves an applied inspector duration",
                              window.project_.score.notes.front().durationTicks == 240 &&
                                  window.project_.score.notes.front().lyric == "L");
                        window.duration_->setValue(.75);
                        auto *title = window.findChild<QLineEdit *>("editorTitle");
                        title->setText("Requested title");
                        title->editingFinished();
                        check("Changing a property preserves it and the applied inspector draft",
                              window.project_.score.title == "Requested title" &&
                                  window.project_.score.notes.front().durationTicks == 360);
                        window.busy_ = true;
                        const auto before = window.project_.score;
                        window.findChild<QAction *>("menuLyrics")->trigger();
                        window.busy_ = false;
                        check("Busy lyrics command leaves the score unchanged", window.project_.score == before);
                        responder.stop();
                        window.inputNumberedPitch(0, 0, 0);
                        const auto &rest = window.project_.score.notes.front();
                        check("Converting a sung note to rest clears both lyric forms",
                              rest.degree == 0 && rest.lyric.empty() && rest.verseLyrics.empty());
                    });
            attempt("playback exception boundary",
                    [&]
                    {
                        setup(fixture());
                        window.project_.score.repeats = {{0, 2, 2}};
                        bool escaped = false;
                        try
                        {
                            window.rebuild(true);
                        }
                        catch (const std::exception &)
                        {
                            escaped = true;
                        }
                        check("Invalid composition rebuild contains exceptions", !escaped);
                        check("Invalid composition disables playback", !window.play_->isEnabled());
                    });
            attempt("legacy meter atomicity",
                    [&]
                    {
                        auto project = fixture(true, 128);
                        Note held;
                        held.durationTicks = ticksPerBar(project.score) * 32;
                        project.numberedComposition->voices[0].notes = {held, held, held, held};
                        project.numberedComposition->voices[1].notes.clear();
                        project.score.notes = project.numberedComposition->voices[0].notes;
                        setup(project);
                        const auto before = projectToJson(window.project_);
                        bool escaped = false;
                        try
                        {
                            window.meterTop_->setValue(1);
                        }
                        catch (const std::exception &)
                        {
                            escaped = true;
                        }
                        check("Legacy meter change does not throw", !escaped);
                        check("Rejected meter edit preserves document",
                              window.project_.score.beatsPerBar == 4 && projectToJson(window.project_) == before);
                        check("Rejected meter edit restores its control", window.meterTop_->value() == 4);
                    });
            for (int target : {0, 1, 2})
                attempt("draft cancellation",
                        [&]
                        {
                            setup(fixture());
                            window.lyricEdit_->setText("keep this draft");
                            const int count = cancelDialogs(
                                [&]
                                {
                                    if (target < 2)
                                        window.view_->numberedVoiceNoteSelected(1, target);
                                    else
                                        window.view_->numberedVoiceInputSelected(1, 960);
                                });
                            check("Cancelled canvas selection prompts exactly once", count == 1);
                            check("Cancelled canvas selection preserves draft",
                                  window.lyricEdit_->text() == "keep this draft" && window.selected_ == 0);
                            window.lyricEdit_->setText("");
                        });
            attempt("legacy draft protection",
                    [&]
                    {
                        setup(fixture(false));
                        window.lyricEdit_->setText("legacy draft");
                        const int count = cancelDialogs([&] { window.view_->noteClicked(1); });
                        check("Single-voice different-note cancellation still works",
                              count == 1 && window.lyricEdit_->text() == "legacy draft");
                        window.lyricEdit_->setText("");
                    });
            attempt("toolbar synchronization",
                    [&]
                    {
                        setup(fixture());
                        window.selectNumberedInput(960);
                        click("editorDotted");
                        click("editorDuration240");
                        check("Duration selection clears dotted entry state",
                              window.inputDuration_ == 240 &&
                                  !window.findChild<QPushButton *>("editorDotted")->isChecked());
                        window.selectNote(0, false);
                        window.lyricEdit_->setText("keep toolbar draft");
                        const int oldDuration = window.inputDuration_;
                        cancelDialogs([&] { click("editorDuration120"); });
                        check("Cancelled duration restores entry and button state",
                              window.inputDuration_ == oldDuration &&
                                  window.findChild<QPushButton *>("editorDuration480")->isChecked());
                        auto *dynamic = window.findChild<QComboBox *>("numberedDynamics");
                        cancelDialogs([&] { dynamic->setCurrentIndex(2); });
                        check("Cancelled dynamic restores combo without clearing draft",
                              dynamic->currentIndex() == 0 && window.lyricEdit_->text() == "keep toolbar draft");
                        window.lyricEdit_->setText("");
                    });
            attempt("measure cursor",
                    [&]
                    {
                        auto project = fixture(false);
                        project.score.notes.clear();
                        setup(project);
                        window.selectNumberedInput(3 * ticksPerBar(project.score));
                        click("editorBarDelete");
                        check("Deleting last empty measure leaves a visible input slot",
                              std::any_of(window.numberedLayout_.slots.begin(), window.numberedLayout_.slots.end(),
                                          [&](const auto &slot)
                                          { return slot.tick == window.numberedInputTick_; }));
                        project = fixture(false, 1);
                        project.score.notes.resize(1);
                        project.score.notes[0].durationTicks = ticksPerBar(project.score);
                        setup(project);
                        window.selectNumberedInput(ticksPerBar(project.score));
                        bool visible = false;
                        for (auto *item : window.view_->scene()->items())
                            visible |= item->data(0).toString() == "numberedAppendCursor";
                        check("Full last measure exposes an explicit append cursor", visible);
                    });
            for (int duration : {120, 60})
                attempt("dense shared layout",
                        [&]
                        {
                            auto project = fixture();
                            auto &notes = project.numberedComposition->voices[0].notes;
                            notes.assign(4, Note{});
                            for (auto &note : notes)
                                note.durationTicks = duration;
                            Note held;
                            held.durationTicks = 1920 - duration * 4;
                            notes.push_back(held);
                            project.score.notes = notes;
                            NumberedScoreLayout layout;
                            auto image =
                                renderNumberedComposition(project.score, *project.numberedComposition, 1, layout);
                            bool separated = true;
                            const auto &rendered = project.numberedComposition->voices[0].notes;
                            for (int i = 1; i < 4; ++i)
                                separated &=
                                    rendered[std::size_t(i)].source.x - rendered[std::size_t(i - 1)].source.x >=
                                    28;
                            check("Dense short-note digits and underlines do not overlap", separated);
                            bool contained = true;
                            for (const auto &slot : layout.slots)
                                contained &=
                                    slot.bounds.right() <= layout.measures[std::size_t(slot.tick / 1920)].right();
                            check("Empty slots stay inside their measure", contained);
                            check("Dense-layout PNG was written",
                                  image.save(folder + QString("/dense-%1.png").arg(duration)));
                        });
            attempt("opening measure signature",
                    [&]
                    {
                        auto project = fixture();
                        auto &composition = *project.numberedComposition;
                        composition.structure = {{{1, 3, 4, 67, 2}}, {}, 2};
                        const auto measures = numberedMeasures(project.score, composition);
                        auto inherited = project.score;
                        auto explicitOpening = inherited;
                        explicitOpening.tonic = 2;
                        explicitOpening.beatsPerBar = 3;
                        explicitOpening.bpm = 67;
                        NumberedScoreLayout layout;
                        const auto inferredImage = renderNumberedScore(inherited, &layout, nullptr, &measures);
                        const auto explicitImage =
                            renderNumberedScore(explicitOpening, &layout, nullptr, &measures);
                        const auto header = QRect(0, 0, inferredImage.width(), int(layout.systems.front().top()));
                        check("Header key, meter and tempo reflect the opening written measure",
                              inferredImage.copy(header) == explicitImage.copy(header));
                    });
            attempt("wide empty meter",
                    [&]
                    {
                        Score score;
                        score.editorMinimumBars = 1;
                        score.beatsPerBar = 64;
                        NumberedScoreLayout layout;
                        renderNumberedScore(score, &layout);
                        check("Supported 64-beat empty meter renders", layout.slots.size() == 64);
                    });
            attempt("render reuse and selection purity",
                    [&]
                    {
                        setup(fixture());
                        const auto start = numberedRasterizationCount();
                        window.inputNumberedPitch(4, 0, 0);
                        const auto editCount = numberedRasterizationCount() - start;
                        metrics.insert("editRasterizations", double(editCount));
                        check("Composition edit rasterizes each voice and final page once", editCount == 3);
                        OptionsContext context;
                        context.score = window.project_.score;
                        context.mix = window.project_.practiceMix;
                        context.speed = 1.25;
                        auto settings = window.settings_;
                        const auto optionsStart = numberedRasterizationCount();
                        window.applyOptions(settings, context);
                        check("Applying playback speed does not rasterize the score",
                              numberedRasterizationCount() == optionsStart);
                        context.score.title = "Updated in options";
                        const auto changedStart = numberedRasterizationCount();
                        window.applyOptions(settings, context);
                        check("Changing score metadata applies the prepared raster exactly once",
                              window.project_.score.title == "Updated in options" &&
                                  numberedRasterizationCount() - changedStart == 3);
                        const auto image = window.project_.image;
                        const auto selectionStart = numberedRasterizationCount();
                        window.selectNumberedVoice(2);
                        check("Voice selection leaves persistent score pixels unchanged",
                              image == window.project_.image);
                        const auto selectionCount = numberedRasterizationCount() - selectionStart;
                        metrics.insert("selectionRasterizations", double(selectionCount));
                        check("Voice selection does not rasterize", selectionCount == 0);
                    });
            attempt("translation",
                    [&]
                    {
                        setup(fixture());
                        window.languageManager_.setLanguage("en_US");
                        QApplication::processEvents();
                        auto *propertyTitle = window.findChild<QLineEdit *>("editorTitle");
                        auto *propertyTempo = window.findChild<QDoubleSpinBox *>("editorTempo");
                        check("Property accessible label follows the installed language",
                              propertyTitle && propertyTitle->accessibleName() == trText("ui.editor.title"));
                        check("Properties do not repeat the tempo unit",
                              propertyTempo && propertyTempo->suffix().isEmpty());
                        QApplication::processEvents();
                        check("Position text retranslates immediately",
                              window.findChild<QLabel *>("editorPosition")->text().startsWith("Measure"));
                        auto *keys = window.findChild<QComboBox *>("editorKey");
                        check("Property key names retranslate", keys->itemText(3) == trText("ui.key.3"));
                        bool translated = false;
                        for (auto *item : window.view_->scene()->items())
                            if (auto *text = dynamic_cast<QGraphicsSimpleTextItem *>(item))
                                translated |= text->text() == trText("ui.editor.add_bars");
                        check("Canvas add-bars text retranslates immediately", translated);
                        window.languageManager_.setLanguage("zh_CN");
                    });
            writeJsonReport(args.value("report"), {{"passed", passed}, {"checks", checks}, {"metrics", metrics}});
            QTextStream(stdout) << "NUMBERED_REVIEW passed=" << passed << " checks=" << checks.size() << Qt::endl;
            app.exit(passed ? 0 : 3);
        });
}
} // namespace singlilt

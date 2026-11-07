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
                            image.save(folder + QString("/dense-%1.png").arg(duration));
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
                        check("Composition edit rasterizes each voice and final page once", editCount <= 3);
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

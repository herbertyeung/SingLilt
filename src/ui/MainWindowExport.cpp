// WAV export requests and output-range handling.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MainWindow.h"
#include "audio/WaveRenderer.h"
#include "i18n/LanguageManager.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QException>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace singlilt
{
void MainWindow::exportWaveFile()
{
    if (project_.staffPerformance && project_.processing.value("tempoNeedsConfirmation").toBool())
    {
        setStatus("ui.fidelity.tempo_required");
        return;
    }
    auto *action = findChild<QAction *>("menuExportWave");
    if (busy_ || audioLoading_ || notePreviewLoading_ || project_.score.notes.empty() || !action ||
        !action->isEnabled() || action->property("waveExportRunning").toBool() || !resolveNoteDraft())
        return;
    try
    {
        const auto score = project_.score;
        const auto timeline = timeline_;
        if (!timeline.valid() || timeline.events.empty())
            throw std::runtime_error(trText("messages.audio.wave_no_timeline").toStdString());
        AccompanimentPlan plan;
        plan.durationTicks = timeline.durationTicks;
        WaveRenderOptions options;
        options.maxSeconds = 600.0;
        options.metronome = metronome_->isChecked();
        options.transpose = player_.transpose();
        options.speed = player_.speed();
        options.mix = project_.practiceMix;
        options.gmSoundFontPath = player_.gmSoundFontPath();
        if (project_.staffPerformance)
        {
            plan = buildStaffPerformancePlan(score, timeline, *project_.staffPerformance);
            options.settings.originalStaff = true;
            options.settings.chordProgram = project_.staffPerformance->primaryProgram;
            options.settings.bassProgram = project_.staffPerformance->otherProgram;
        }
        else if (options.mix.accompanimentEnabled)
        {
            if (!project_.accompaniment)
                throw std::runtime_error(trText("messages.audio.invalid_accompaniment").toStdString());
            plan = buildAccompanimentPlan(score, timeline, *project_.accompaniment);
            options.settings = project_.accompaniment->settings;
        }
        if (!plan.valid())
            throw std::runtime_error(trText("messages.audio.invalid_accompaniment").toStdString());
        const bool staleArrangement =
            project_.accompaniment && project_.accompaniment->melodyFingerprint != accompanimentFingerprint(score);
        const char *arrangementKey = project_.staffPerformance ? "ui.staff.original_parts"
                                     : staleArrangement        ? "ui.accompaniment.stale_confirmed"
                                     : project_.accompaniment  ? "ui.export.confirmed"
                                                               : "ui.export.none";

        // The range belongs to the performed clock, including repeats and practice speed.
        const double fullSeconds = std::llround(timeline.durationSeconds() / options.speed * 48000.0) / 48000.0;
        QDialog dialog(this);
        dialog.setObjectName("waveExportOptions");
        dialog.setWindowTitle(trText("ui.export.title"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *summary =
            new QLabel(trText(project_.staffPerformance ? "ui.staff.export_settings" : "ui.export.settings")
                           .arg(QString::number(options.speed, 'f', 2), QString::number(options.transpose),
                                QString::number(project_.staffPerformance ? options.settings.chordProgram + 1
                                                                          : programForVerse(score, 0) + 1),
                                QString::number(project_.staffPerformance ? options.settings.bassProgram + 1
                                                                          : programForVerse(score, 1) + 1),
                                trText(options.metronome ? "ui.export.on" : "ui.export.off")),
                       &dialog);
        summary->setObjectName("waveExportSettings");
        summary->setTextFormat(Qt::PlainText);
        summary->setWordWrap(true);
        layout->addWidget(summary);
        auto *mix = new QLabel(
            trText(project_.staffPerformance ? "ui.staff.export_mix" : "ui.export.mix")
                .arg(trText(options.mix.melodyEnabled ? "ui.export.on" : "ui.export.off"),
                     QString::number(options.mix.melodyVolume * 100, 'f', 0),
                     trText(options.mix.accompanimentEnabled ? "ui.export.on" : "ui.export.off"),
                     QString::number(options.mix.accompanimentVolume * 100, 'f', 0), trText(arrangementKey)),
            &dialog);
        mix->setObjectName("waveExportMix");
        mix->setTextFormat(Qt::PlainText);
        mix->setWordWrap(true);
        layout->addWidget(mix);
        auto *help = new QLabel(trText("ui.export.help").arg(QString::number(fullSeconds, 'f', 3)), &dialog);
        help->setObjectName("waveExportHelp");
        help->setWordWrap(true);
        layout->addWidget(help);
        auto *form = new QFormLayout;
        auto *start = new QDoubleSpinBox(&dialog);
        start->setObjectName("waveExportStart");
        start->setDecimals(6);
        start->setSingleStep(1.0);
        const double minimumSpan = std::min(0.001, fullSeconds);
        start->setRange(0.0, std::max(0.0, fullSeconds - minimumSpan));
        auto *end = new QDoubleSpinBox(&dialog);
        end->setObjectName("waveExportEnd");
        end->setDecimals(6);
        end->setSingleStep(1.0);
        end->setRange(minimumSpan, std::min(fullSeconds, 600.0));
        end->setValue(std::min(fullSeconds, 600.0));
        form->addRow(trText("ui.export.start"), start);
        form->addRow(trText("ui.export.end"), end);
        layout->addLayout(form);
        auto *range = new QLabel(&dialog);
        range->setObjectName("waveExportRange");
        range->setWordWrap(true);
        layout->addWidget(range);
        auto *tail = new QLabel(trText("ui.export.tail"), &dialog);
        tail->setWordWrap(true);
        layout->addWidget(tail);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        buttons->setObjectName("waveExportButtons");
        buttons->button(QDialogButtonBox::Save)->setText(trText("ui.export.choose_file"));
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        const auto updateRange = [start, end, range, fullSeconds]
        {
            const bool fragment = start->value() > 0.0 || end->value() < fullSeconds - 0.5 / 48000;
            range->setText(trText("ui.export.range")
                               .arg(trText(fragment ? "ui.export.fragment" : "ui.export.full"),
                                    QString::number(start->value(), 'f', 3), QString::number(end->value(), 'f', 3),
                                    QString::number(end->value() - start->value(), 'f', 3)));
        };
        connect(start, &QDoubleSpinBox::valueChanged, &dialog,
                [end, fullSeconds, minimumSpan, updateRange](double seconds)
                {
                    end->setRange(seconds + minimumSpan, std::min(fullSeconds, seconds + 600.0));
                    updateRange();
                });
        connect(end, &QDoubleSpinBox::valueChanged, &dialog, [updateRange] { updateRange(); });
        updateRange();
        dialog.resize(510, dialog.sizeHint().height());
        if (dialog.exec() != QDialog::Accepted)
            return;
        options.startSeconds = start->value();
        options.endSeconds = end->value() >= fullSeconds - 0.5 / 48000 ? -1.0 : end->value();

        QFileDialog fileDialog(this, trText("ui.menu.export_wave"));
        fileDialog.setObjectName("waveExportFile");
        fileDialog.setAcceptMode(QFileDialog::AcceptSave);
        fileDialog.setNameFilter("WAV (*.wav)");
        fileDialog.setDefaultSuffix("wav");
        if (fileDialog.exec() != QDialog::Accepted || fileDialog.selectedFiles().isEmpty())
            return;
        const QString path = fileDialog.selectedFiles().first();
        auto *watcher = new QFutureWatcher<WaveRenderResult>(this);
        action->setProperty("waveExportRunning", true);
        action->setEnabled(false);
        connect(watcher, &QFutureWatcher<WaveRenderResult>::finished, this,
                [this, watcher, action, path]
                {
                    action->setProperty("waveExportRunning", false);
                    action->setEnabled(true);
                    try
                    {
                        const auto result = watcher->result();
                        setStatusMessage(
                            trText("ui.export.finished")
                                .arg(trText(result.fragment ? "ui.export.fragment" : "ui.export.full"),
                                     QString::number(result.startSeconds, 'f', 3),
                                     QString::number(result.endSeconds, 'f', 3),
                                     QString::number(double(result.musicFrames) / result.sampleRate, 'f', 3),
                                     path));
                    }
                    catch (const QUnhandledException &error)
                    {
                        if (const auto original = error.exception())
                        {
                            try
                            {
                                std::rethrow_exception(original);
                            }
                            catch (const std::exception &cause)
                            {
                                showError(QString::fromUtf8(cause.what()));
                            }
                            catch (...)
                            {
                                showError(trText("messages.export.failed"));
                            }
                        }
                        else
                        {
                            showError(trText("messages.export.failed"));
                        }
                    }
                    catch (const std::exception &error)
                    {
                        showError(QString::fromUtf8(error.what()));
                    }
                    watcher->deleteLater();
                });
        watcher->setFuture(QtConcurrent::run([score, timeline, plan, path, options]
                                             { return renderWave(score, timeline, plan, path, options); }));
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}
} // namespace singlilt

// Whole-song arrangement selection and practice-flow checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "WholeSongCheck.h"
#include "storage/ProjectStore.h"
#include "ui/AccompanimentPanel.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLayout>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
bool sameArrangement(const AccompanimentArrangement &a, const AccompanimentArrangement &b)
{
    if (a.chords.size() != b.chords.size() || a.name != b.name || a.userAuthored != b.userAuthored ||
        a.melodyFingerprint != b.melodyFingerprint || a.settings.pattern != b.settings.pattern)
        return false;
    for (size_t i = 0; i < a.chords.size(); ++i)
    {
        const auto &left = a.chords[i];
        const auto &right = b.chords[i];
        if (left.startTick != right.startTick || left.endTick != right.endTick ||
            left.rootPitchClass != right.rootPitchClass || left.quality != right.quality ||
            left.inversion != right.inversion || left.userEdited != right.userEdited ||
            left.patternOverride != right.patternOverride)
            return false;
    }
    return true;
}
bool sameMix(const PracticeMix &a, const PracticeMix &b)
{
    return a.melodyEnabled == b.melodyEnabled && a.accompanimentEnabled == b.accompanimentEnabled &&
           std::abs(a.melodyVolume - b.melodyVolume) < 1e-9 &&
           std::abs(a.accompanimentVolume - b.accompanimentVolume) < 1e-9;
}
class WholeSongProbe final : public QObject
{
  public:
    WholeSongProbe(MainWindow &window, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), args_(args), app_(app),
          folder_(QFileInfo(args.value("report")).absolutePath())
    {
        QDir().mkpath(folder_);
        timer_.setInterval(100);
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        elapsed_.start();
        timer_.start();
    }

  private:
    template <typename T> T *control(const char *name)
    {
        auto *widget = panel_ ? panel_->findChild<T *>(name) : nullptr;
        if (!widget)
            widget = window_.findChild<T *>(name);
        if (!widget)
            throw std::runtime_error(QString("Missing UI control: %1").arg(name).toStdString());
        return widget;
    }
    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ = passed_ && passed;
    }
    AccompanimentPanel *open()
    {
        control<QPushButton>("generateAccompaniment")->click();
        // WA_DeleteOnClose queues deletion; a rejected panel can still be a child here.
        for (auto *dialog : window_.findChildren<QDialog *>("accompanimentPreview"))
            if (dialog->isVisible())
                if (auto *panel = dynamic_cast<AccompanimentPanel *>(dialog))
                    return panel;
        throw std::runtime_error("Whole-song panel did not open");
    }
    void finish()
    {
        timer_.stop();
        window_.player().stop();
        const QJsonObject report{{"passed", passed_},
                                 {"checks", checks_},
                                 {"error", error_},
                                 {"elapsedMilliseconds", elapsed_.elapsed()}};
        QFile file(args_.value("report"));
        const auto bytes = QJsonDocument(report).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        {
            app_.exit(4);
            return;
        }
        app_.exit(passed_ ? 0 : 3);
    }
    void advance()
    {
        if (elapsed_.elapsed() > 45000)
        {
            check("whole_song_timeout", false);
            finish();
            return;
        }
        if (window_.isAudioLoading())
            return;
        try
        {
            if (stage_ == 0)
            {
                Project fixture = makePracticeScore();
                fixture.score.bpm = 240;
                fixture.score.versePrograms = {0};
                for (size_t i = 0; i < fixture.score.notes.size(); ++i)
                {
                    fixture.score.notes[i].durationTicks = 4 * TicksPerQuarter;
                    fixture.score.notes[i].measure = int(i);
                }
                fixture.accompaniment = generateAccompanimentCandidates(fixture.score).front().arrangement;
                fixture.practiceMix = {true, false, .63, .4};
                original_ = *fixture.accompaniment;
                originalMix_ = fixture.practiceMix;
                window_.setProject(std::move(fixture));
                panel_ = open();
                auto *options = control<QListWidget>("accompanimentWholeSongCandidates");
                check("three_whole_song_options_and_manual", options->count() == 4);
                check("ordinary_measure_list_visible",
                      control<QListWidget>("accompanimentSections")->isVisible() &&
                          !control<QTableWidget>("accompanimentChords")->isVisible());
                options->setCurrentRow(0);
                const auto block = panel_->candidate();
                options->setCurrentRow(1);
                const auto flowing = panel_->candidate();
                options->setCurrentRow(2);
                const auto sparse = panel_->candidate();
                check("real_whole_song_profile_difference",
                      block.settings.pattern == AccompanimentPattern::BlockChords &&
                          flowing.settings.pattern == AccompanimentPattern::Arpeggio &&
                          sparse.settings.pattern == AccompanimentPattern::Sparse);
                panel_->layout()->activate();
                check("whole_song_screenshot", panel_->grab().save(folder_ + "/whole-song-options.png"));
                options->setCurrentRow(1);
                control<QPushButton>("accompanimentCopyCandidate")->click();
                control<QPushButton>("accompanimentAdvancedToggle")->click();
                control<QTableWidget>("accompanimentChords")->selectRow(0);
                control<QComboBox>("accompanimentRoot")->setCurrentIndex(9);
                auto *pattern = control<QComboBox>("accompanimentSectionPattern");
                pattern->setCurrentIndex(pattern->findData(int(AccompanimentPattern::Sparse)));
                control<QPushButton>("accompanimentSplitSpan")->click();
                const auto splitCount = panel_->candidate().chords.size();
                control<QPushButton>("accompanimentMergeSpan")->click();
                check("manual_split_merge",
                      panel_->candidate().chords.size() + 1 == splitCount &&
                          panel_->candidate().chords.front().patternOverride == AccompanimentPattern::Sparse);
                const auto beforeInvalid = panel_->candidate();
                control<QSpinBox>("accompanimentEndTick")->setValue(int(beforeInvalid.chords.front().endTick + 1));
                control<QPushButton>("accompanimentApplyInterval")->click();
                check("overlapping_manual_interval_rejected", sameArrangement(panel_->candidate(), beforeInvalid));
                control<QSpinBox>("accompanimentEndTick")->setValue(int(beforeInvalid.chords.front().endTick));
                control<QPushButton>("accompanimentApplyInterval")->click();
                control<QPushButton>("accompanimentDeleteSpan")->click();
                control<QPushButton>("accompanimentAddSpan")->click();
                check("manual_delete_add_gap_as_silence",
                      panel_->candidate().chords.front().quality == ChordQuality::None);
                control<QComboBox>("accompanimentQuality")->setCurrentIndex(int(ChordQuality::Minor));
                control<QComboBox>("accompanimentRoot")->setCurrentIndex(9);
                pattern->setCurrentIndex(pattern->findData(int(AccompanimentPattern::Sparse)));
                manual_ = panel_->candidate();
                control<QPushButton>("accompanimentRegenerate")->click();
                check("regeneration_retains_manual_draft", sameArrangement(panel_->candidate(), manual_));
                options->setCurrentRow(0);
                check("automatic_profile_not_modified", sameArrangement(panel_->candidate(), block));
                options->setCurrentRow(3);
                check("switching_options_retains_manual_draft", sameArrangement(panel_->candidate(), manual_));
                control<QPushButton>("accompanimentAudition")->click();
                stage_ = 1;
            }
            else if (stage_ == 1)
            {
                check("manual_audition_playing", window_.player().isPlaying());
                const auto mix = window_.player().practiceMix();
                const auto tick = window_.player().positionTicks();
                const auto melodyAttacks = window_.player().voiceState().melodyNoteOns;
                control<QListWidget>("accompanimentWholeSongCandidates")->setCurrentRow(1);
                check("live_option_switch_keeps_tick_and_playing",
                      window_.player().isPlaying() && window_.player().positionTicks() >= tick &&
                          window_.player().positionTicks() - tick < TicksPerQuarter);
                check("live_option_switch_keeps_mix", sameMix(window_.player().practiceMix(), mix));
                check("live_option_switch_keeps_melody_voice",
                      window_.player().voiceState().melodyNoteOns == melodyAttacks);
                control<QListWidget>("accompanimentWholeSongCandidates")->setCurrentRow(3);
                const QString previewPath = folder_ + "/whole-song-preview.jpp";
                saveProject(previewPath, window_.project());
                auto preview = loadProject(previewPath);
                check("unconfirmed_whole_song_not_saved", preview.accompaniment &&
                                                              sameArrangement(*preview.accompaniment, original_) &&
                                                              sameMix(preview.practiceMix, originalMix_));
                control<QPushButton>("accompanimentCancel")->click();
                check("whole_song_cancel_restores_mix_and_confirmed",
                      sameMix(window_.player().practiceMix(), originalMix_) && window_.project().accompaniment &&
                          sameArrangement(*window_.project().accompaniment, original_));
                panel_.clear();
                stage_ = 2;
            }
            else if (stage_ == 2)
            {
                panel_ = open();
                control<QPushButton>("accompanimentCopyCandidate")->click();
                control<QPushButton>("accompanimentAdvancedToggle")->click();
                auto *pattern = control<QComboBox>("accompanimentSectionPattern");
                pattern->setCurrentIndex(pattern->findData(int(AccompanimentPattern::Sparse)));
                control<QComboBox>("accompanimentRoot")->setCurrentIndex(9);
                manual_ = panel_->candidate();
                control<QPushButton>("accompanimentConfirm")->click();
                const QString savedPath = folder_ + "/my-arrangement.jpp";
                saveProject(savedPath, window_.project());
                const auto saved = loadProject(savedPath);
                check("manual_name_authorship_and_pattern_roundtrip",
                      saved.accompaniment && saved.accompaniment->userAuthored &&
                          !saved.accompaniment->name.empty() && sameArrangement(*saved.accompaniment, manual_));
                panel_.clear();
                stage_ = 3;
            }
            else
            {
                panel_ = open();
                control<QListWidget>("accompanimentWholeSongCandidates")->setCurrentRow(3);
                check("reopening_restores_confirmed_manual_draft", sameArrangement(panel_->candidate(), manual_));
                panel_->reject();
                panel_.clear();
                window_.setProject(makePracticeScore());
                panel_ = open();
                control<QPushButton>("accompanimentBlankDraft")->click();
                check("blank_manual_arrangement_covers_measures_as_silence",
                      !panel_->candidate().chords.empty() &&
                          std::all_of(panel_->candidate().chords.begin(), panel_->candidate().chords.end(),
                                      [](const ChordSpan &span) { return span.quality == ChordQuality::None; }));
                panel_->reject();
                finish();
            }
        }
        catch (const std::exception &exception)
        {
            error_ = QString::fromUtf8(exception.what());
            check("whole_song_exception", false);
            finish();
        }
    }
    MainWindow &window_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QString folder_, error_;
    QPointer<AccompanimentPanel> panel_;
    AccompanimentArrangement original_, manual_;
    PracticeMix originalMix_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QJsonArray checks_;
    bool passed_ = true;
    int stage_ = 0;
};
} // namespace
void runWholeSongCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    new WholeSongProbe(window, args, app);
}
} // namespace singlilt

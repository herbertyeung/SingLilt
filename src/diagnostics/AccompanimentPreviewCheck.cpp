// Arrangement-preview acceptance and cancellation checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AccompanimentPreviewCheck.h"
#include "i18n/LanguageManager.h"
#include "storage/ProjectStore.h"
#include "ui/AccompanimentPanel.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
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
bool sameChords(const std::vector<ChordSpan> &left, const std::vector<ChordSpan> &right)
{
    if (left.size() != right.size())
        return false;
    for (size_t i = 0; i < left.size(); ++i)
    {
        const auto &a = left[i];
        const auto &b = right[i];
        if (a.startTick != b.startTick || a.endTick != b.endTick || a.rootPitchClass != b.rootPitchClass ||
            a.quality != b.quality || a.inversion != b.inversion || a.userEdited != b.userEdited)
            return false;
    }
    return true;
}
bool sameArrangement(const AccompanimentArrangement &left, const AccompanimentArrangement &right)
{
    const auto &a = left.settings;
    const auto &b = right.settings;
    return left.melodyFingerprint == right.melodyFingerprint && left.generatorVersion == right.generatorVersion &&
           a.pattern == b.pattern && a.mode == b.mode && a.harmonicTonic == b.harmonicTonic &&
           a.chordProgram == b.chordProgram && a.bassProgram == b.bassProgram &&
           a.chordVelocity == b.chordVelocity && a.bassVelocity == b.bassVelocity &&
           sameChords(left.chords, right.chords);
}
bool sameMix(const PracticeMix &left, const PracticeMix &right)
{
    return left.melodyEnabled == right.melodyEnabled && left.accompanimentEnabled == right.accompanimentEnabled &&
           std::abs(left.melodyVolume - right.melodyVolume) < 1e-9 &&
           std::abs(left.accompanimentVolume - right.accompanimentVolume) < 1e-9;
}
class PreviewProbe final : public QObject
{
  public:
    PreviewProbe(MainWindow &window, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), args_(args), app_(app)
    {
        folder_ = QFileInfo(args_.value("report")).absolutePath();
        QDir().mkpath(folder_);
        timer_.setInterval(100);
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        elapsed_.start();
        timer_.start();
    }

  private:
    template <typename T> T *control(const char *name)
    {
        auto *widget = window_.findChild<T *>(name);
        if (!widget)
            throw std::runtime_error(QString("Missing UI control: %1").arg(name).toStdString());
        return widget;
    }
    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ = passed_ && passed;
    }
    AccompanimentPanel *openCandidate()
    {
        control<QPushButton>("generateAccompaniment")->click();
        auto *panel = dynamic_cast<AccompanimentPanel *>(window_.findChild<QDialog *>("accompanimentPreview"));
        if (!panel)
            throw std::runtime_error("The accompaniment candidate panel did not open");
        return panel;
    }
    void editCandidate(AccompanimentPanel &panel)
    {
        if (auto *advanced=panel.findChild<QPushButton *>("accompanimentAdvancedToggle"))
            if (!panel.findChild<QTableWidget *>("accompanimentChords")->isVisible()) advanced->click();
        if (auto *copy=panel.findChild<QPushButton *>("accompanimentCopyCandidate")) copy->click();
        auto *table = panel.findChild<QTableWidget *>("accompanimentChords");
        if (!table || table->rowCount() < 2)
            throw std::runtime_error("Candidate chord table is missing or too short");
        table->selectRow(0);
        control<QComboBox>("accompanimentRoot")->setCurrentIndex(9);
        control<QComboBox>("accompanimentQuality")->setCurrentIndex(int(ChordQuality::Minor));
        control<QSpinBox>("accompanimentInversion")->setValue(2);
        table->selectRow(1);
        auto *alternatives = control<QComboBox>("accompanimentCandidate");
        alternatives->setCurrentIndex(alternatives->count() - 1);
        control<QComboBox>("candidatePattern")->setCurrentIndex(int(AccompanimentPattern::Arpeggio));
        const auto &candidate = panel.candidate();
        check("candidate_root_quality_inversion_corrected",
              candidate.chords[0].rootPitchClass == 9 && candidate.chords[0].quality == ChordQuality::Minor &&
                  candidate.chords[0].inversion == 2 && candidate.chords[0].userEdited);
        check("candidate_no_accompaniment_interval", candidate.chords[1].quality == ChordQuality::None &&
                                                         candidate.chords[1].inversion == 0 &&
                                                         candidate.chords[1].userEdited);
        check("candidate_pattern_changed", candidate.settings.pattern == AccompanimentPattern::Arpeggio);
    }
    Project roundTrip(const QString &name)
    {
        const QString path = folder_ + "/" + name + ".jpp";
        saveProject(path, window_.project());
        artifacts_.append(path);
        return loadProject(path);
    }
    void finish()
    {
        timer_.stop();
        window_.player().stop();
        QJsonObject report{{"passed", passed_},
                           {"checks", checks_},
                           {"artifacts", artifacts_},
                           {"elapsedMilliseconds", elapsed_.elapsed()},
                           {"error", error_}};
        if (args_.isSet("screenshot"))
            report.insert("screenshotSaved", window_.grab().save(args_.value("screenshot")));
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
            check("preview_check_timeout", false);
            finish();
            return;
        }
        if (window_.isAudioLoading())
            return;
        try
        {
            switch (stage_)
            {
            case 0:
            {
                Project fixture = makePracticeScore();
                fixture.score.bpm = 240;
                fixture.score.versePrograms = {0};
                for (size_t i = 0; i < fixture.score.notes.size(); ++i)
                {
                    fixture.score.notes[i].durationTicks = 4 * TicksPerQuarter;
                    fixture.score.notes[i].measure = int(i);
                }
                fixture.accompaniment = generateAccompaniment(fixture.score);
                fixture.practiceMix = {true, false, .67, .31};
                original_ = *fixture.accompaniment;
                originalMix_ = fixture.practiceMix;
                window_.setProject(std::move(fixture));
                panel_ = openCandidate();
                check("candidate_panel_nonmodal", !panel_->isModal() && panel_->isVisible());
                check("candidate_harmonic_warning_visible",
                      control<QLabel>("accompanimentStatus")
                          ->text()
                          .contains(trText("messages.accompaniment.harmonic_suggestion")));
                editCandidate(*panel_);
                check("candidate_separate_from_confirmed",
                      window_.project().accompaniment &&
                          sameArrangement(*window_.project().accompaniment, original_));
                auto saved = roundTrip("candidate-not-confirmed");
                check("save_excludes_candidate", saved.accompaniment &&
                                                     sameArrangement(*saved.accompaniment, original_) &&
                                                     sameMix(saved.practiceMix, originalMix_));
                control<QPushButton>("accompanimentAudition")->click();
                stage_ = 1;
                break;
            }
            case 1:
            {
                if (!window_.player().isPlaying())
                {
                    error_ = window_.player().errorString();
                    check("audition_started_transport", false);
                    finish();
                    return;
                }
                check("audition_started_transport", true);
                control<QCheckBox>("melodyEnabled")->setChecked(false);
                control<QSlider>("outputVolume")->setValue(42);
                control<QSlider>("accompanimentVolume")->setValue(77);
                auto saved = roundTrip("audition-not-confirmed");
                check("audition_mix_not_persisted", sameMix(saved.practiceMix, originalMix_));
                check("audition_arrangement_not_persisted",
                      saved.accompaniment && sameArrangement(*saved.accompaniment, original_));
                const auto before = window_.player().positionTicks();
                control<QPushButton>("accompanimentCancel")->click();
                const auto after = window_.player().positionTicks();
                check("cancel_keeps_playing", window_.player().isPlaying());
                check("cancel_keeps_current_tick", after >= before && after - before < TicksPerQuarter);
                check("cancel_restores_confirmed",
                      window_.project().accompaniment &&
                          sameArrangement(*window_.project().accompaniment, original_));
                check("cancel_restores_project_mix", sameMix(window_.project().practiceMix, originalMix_));
                check("cancel_restores_engine_mix", sameMix(window_.player().practiceMix(), originalMix_));
                panel_.clear();
                stage_ = 2;
                break;
            }
            case 2:
            {
                panel_ = openCandidate();
                editCandidate(*panel_);
                accepted_ = panel_->candidate();
                control<QPushButton>("accompanimentConfirm")->click();
                check("confirmation_applies_corrections",
                      window_.project().accompaniment &&
                          sameArrangement(*window_.project().accompaniment, accepted_));
                auto saved = roundTrip("confirmed-corrections");
                check("confirmed_corrections_roundtrip",
                      saved.accompaniment && sameArrangement(*saved.accompaniment, accepted_) &&
                          sameMix(saved.practiceMix, window_.project().practiceMix));
                const auto chords = window_.project().accompaniment->chords;
                control<QComboBox>("accompanimentPattern")
                    ->setCurrentIndex(int(AccompanimentPattern::BlockChords));
                check("pattern_change_keeps_confirmed_chords",
                      sameChords(window_.project().accompaniment->chords, chords) &&
                          window_.project().accompaniment->settings.pattern == AccompanimentPattern::BlockChords);
                panel_.clear();
                stage_ = 3;
                break;
            }
            case 3:
            {
                panel_ = openCandidate();
                control<QPushButton>("accompanimentAudition")->click();
                stage_ = 4;
                break;
            }
            case 4:
            {
                const auto fingerprint = panel_->candidate().melodyFingerprint;
                control<QLineEdit>("lyricAEditor")->setText("lyric edit during audition");
                control<QPushButton>("applyNoteChanges")->click();
                check("lyrics_do_not_stale_candidate",
                      control<QPushButton>("accompanimentConfirm")->isEnabled() &&
                          accompanimentFingerprint(window_.project().score) == fingerprint);
                check("lyrics_keep_audition_track", window_.player().practiceMix().accompanimentEnabled);
                control<QDoubleSpinBox>("noteDuration")->setValue(.5);
                control<QPushButton>("applyNoteChanges")->click();
                check("duration_edit_stales_candidate",
                      !control<QPushButton>("accompanimentConfirm")->isEnabled() &&
                          accompanimentFingerprint(window_.project().score) != fingerprint);
                check("stale_audition_disabled", !window_.player().practiceMix().accompanimentEnabled &&
                                                     window_.player().voiceState().chordPitches.empty() &&
                                                     window_.player().voiceState().bassPitches.empty());
                const auto preserved = *window_.project().accompaniment;
                const auto saved = roundTrip("stale-history");
                std::int64_t duration = 0;
                for (const auto &note : saved.score.notes)
                    duration += note.durationTicks;
                check("stale_history_extends_past_edited_score",
                      !preserved.chords.empty() && preserved.chords.back().endTick > duration);
                check("stale_history_roundtrip_retains_corrections",
                      saved.accompaniment && sameArrangement(*saved.accompaniment, preserved) &&
                          saved.accompaniment->melodyFingerprint != accompanimentFingerprint(saved.score));
                const QString screenshot = folder_ + "/stale-candidate.png";
                panel_->layout()->activate();
                check("stale_panel_screenshot_saved", panel_->grab().save(screenshot));
                artifacts_.append(screenshot);
                const auto before = window_.player().positionTicks();
                panel_->close();
                check("closing_stale_preview_keeps_playing", window_.player().isPlaying());
                check("closing_stale_preview_keeps_tick", window_.player().positionTicks() >= before);
                check("closing_stale_preview_keeps_confirmed_disabled",
                      !window_.player().practiceMix().accompanimentEnabled);
                panel_.clear();
                stage_ = 5;
                break;
            }
            case 5:
            {
                panel_ = openCandidate();
                control<QPushButton>("accompanimentAudition")->click();
                stage_ = 6;
                break;
            }
            case 6:
            {
                const auto fingerprint = panel_->candidate().melodyFingerprint;
                auto *degree = control<QComboBox>("noteDegree");
                degree->setCurrentIndex(degree->findData(3));
                control<QPushButton>("applyNoteChanges")->click();
                check("pitch_edit_stales_candidate",
                      !control<QPushButton>("accompanimentConfirm")->isEnabled() &&
                          accompanimentFingerprint(window_.project().score) != fingerprint);
                check("pitch_edit_disables_candidate_audio", !window_.player().practiceMix().accompanimentEnabled);
                control<QPushButton>("accompanimentCancel")->click();
                finish();
                break;
            }
            }
        }
        catch (const std::exception &exception)
        {
            error_ = QString::fromUtf8(exception.what());
            check("preview_check_exception", false);
            finish();
        }
    }
    MainWindow &window_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QPointer<AccompanimentPanel> panel_;
    AccompanimentArrangement original_, accepted_;
    PracticeMix originalMix_;
    QString folder_, error_;
    QJsonArray checks_, artifacts_;
    int stage_ = 0;
    bool passed_ = true;
};
} // namespace
void runAccompanimentPreviewCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    new PreviewProbe(window, args, app);
}
} // namespace singlilt

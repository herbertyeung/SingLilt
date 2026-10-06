// Arrangement selection, audition, and practice mixing.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AccompanimentPanel.h"
#include "MainWindow.h"
#include "i18n/LanguageManager.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>

namespace singlilt
{
void MainWindow::createPracticeControls(QVBoxLayout *layout)
{
    auto *controls = new QHBoxLayout;
    auto *generate = button("ui.accompaniment.generate", controls);
    generate->setObjectName("generateAccompaniment");
    accompanimentPattern_ = new QComboBox;
    accompanimentPattern_->setObjectName("accompanimentPattern");
    addTranslatedItem(accompanimentPattern_, "ui.accompaniment.block", int(AccompanimentPattern::BlockChords));
    addTranslatedItem(accompanimentPattern_, "ui.accompaniment.arpeggio", int(AccompanimentPattern::Arpeggio));
    addTranslatedItem(accompanimentPattern_, "ui.whole.sparse_pattern", int(AccompanimentPattern::Sparse));
    int patternWidth = 0;
    for (int i = 0; i < accompanimentPattern_->count(); ++i)
        patternWidth =
            std::max(patternWidth,
                     accompanimentPattern_->fontMetrics().horizontalAdvance(accompanimentPattern_->itemText(i)));
    accompanimentPattern_->setMinimumWidth(patternWidth + 40);
    controls->addWidget(accompanimentPattern_);
    accompanimentState_ = new QLabel;
    accompanimentState_->setObjectName("confirmedAccompanimentStatus");
    accompanimentState_->setWordWrap(true);
    controls->addWidget(accompanimentState_, 1);
    layout->addLayout(controls);
    auto *mixControls = new QHBoxLayout;
    melodyEnabled_ = checkBox("ui.accompaniment.melody_enabled");
    melodyEnabled_->setObjectName("melodyEnabled");
    melodyEnabled_->setChecked(true);
    mixControls->addWidget(melodyEnabled_);
    accompanimentEnabled_ = checkBox("ui.accompaniment.accompaniment_enabled");
    accompanimentEnabled_->setObjectName("accompanimentEnabled");
    mixControls->addWidget(accompanimentEnabled_);
    mixControls->addWidget(label("ui.accompaniment.volume"));
    accompanimentVolume_ = new QSlider(Qt::Horizontal);
    accompanimentVolume_->setObjectName("accompanimentVolume");
    accompanimentVolume_->setRange(0, 100);
    accompanimentVolume_->setValue(55);
    accompanimentVolume_->setMaximumWidth(100);
    mixControls->addWidget(accompanimentVolume_);
    mixControls->addStretch();
    layout->addLayout(mixControls);
    auto *fontControls = new QHBoxLayout;
    fontControls->addWidget(label("ui.accompaniment.gm_label"));
    auto *system = button("ui.accompaniment.system_gm", fontControls);
    system->setObjectName("systemGmSoundFont");
    auto *generalUser = button("ui.accompaniment.generaluser", fontControls);
    generalUser->setObjectName("generalUserSoundFont");
    auto *choose = button("ui.accompaniment.custom_gm", fontControls);
    choose->setObjectName("chooseGmSoundFont");
    auto *fontPath = new QLabel;
    fontPath->setObjectName("gmSoundFontPath");
    fontPath->setWordWrap(true);
    fontControls->addWidget(fontPath, 1);
    fontPath->setText(QSettings().value("audio/gmSoundFontPath").toString());
    layout->addLayout(fontControls);
    auto setPath = [this, fontPath](const QString &path)
    {
        if (audioLoading_)
            return;
        const bool wasPlaying = player_.isPlaying();
        if (!player_.setGmSoundFontPath(path))
        {
            setStatusMessage(player_.errorString());
            return;
        }
        QSettings().setValue("audio/gmSoundFontPath", path);
        fontPath->setText(path);
        fontPath->setToolTip(path);
        playIntent_ = false;
        if (wasPlaying)
            togglePlayback();
    };
    connect(system, &QPushButton::clicked, this, [setPath] { setPath({}); });
    connect(generalUser, &QPushButton::clicked, this,
            [setPath] { setPath(QApplication::applicationDirPath() + "/assets/soundfonts/GeneralUser-GS.sf2"); });
    connect(choose, &QPushButton::clicked, this, [this] { chooseGmSoundFont(); });
    connect(generate, &QPushButton::clicked, this, [this] { generatePracticeAccompaniment(); });
    connect(accompanimentPattern_, &QComboBox::currentIndexChanged, this,
            [this] { changeAccompanimentPattern(); });
    connect(melodyEnabled_, &QCheckBox::toggled, this, [this] { updatePracticeMix(); });
    connect(accompanimentEnabled_, &QCheckBox::toggled, this, [this] { updatePracticeMix(); });
    connect(accompanimentVolume_, &QSlider::valueChanged, this, [this] { updatePracticeMix(); });
}
void MainWindow::chooseGmSoundFont()
{
    if (audioLoading_)
        return;
    QFileDialog dialog(this);
    dialog.setObjectName("gmSoundFontDialog");
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setWindowTitle(trText("ui.accompaniment.custom_gm"));
    dialog.setNameFilter(trText("ui.accompaniment.gm_filter"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    const QString path = dialog.selectedFiles().first();
    const bool wasPlaying = player_.isPlaying();
    if (!player_.setGmSoundFontPath(path))
    {
        setStatusMessage(player_.errorString());
        return;
    }
    QSettings().setValue("audio/gmSoundFontPath", path);
    if (auto *label = findChild<QLabel *>("gmSoundFontPath"))
    {
        label->setText(path);
        label->setToolTip(path);
    }
    playIntent_ = false;
    if (wasPlaying)
        togglePlayback();
}
AccompanimentPlan MainWindow::activeAccompanimentPlan() const
{
    if (project_.staffPerformance)
        return buildStaffPerformancePlan(project_.score, timeline_, *project_.staffPerformance);
    const auto &arrangement = auditionArrangement_ ? auditionArrangement_ : project_.accompaniment;
    if (!project_.staffPerformance &&
        (!arrangement || arrangement->melodyFingerprint != accompanimentFingerprint(project_.score)))
        return {};
    auto plan = buildAccompanimentPlan(project_.score, timeline_, *arrangement);
    return plan.valid() ? plan : AccompanimentPlan{};
}
void MainWindow::generatePracticeAccompaniment()
{
    if (project_.staffPerformance)
    {
        setStatus("ui.staff.original_parts");
        return;
    }
    if (busy_ || audioLoading_ || project_.score.notes.empty())
        return;
    if (accompanimentPanel_)
    {
        accompanimentPanel_->show();
        accompanimentPanel_->raise();
        accompanimentPanel_->activateWindow();
        return;
    }
    auto settings = project_.accompaniment ? project_.accompaniment->settings : AccompanimentSettings{};
    settings.pattern = AccompanimentPattern(accompanimentPattern_->currentData().toInt());
    auto candidate = generateAccompaniment(project_.score, settings);
    accompanimentPanel_ =
        new AccompanimentPanel(project_.score, std::move(candidate), project_.accompaniment, this);
    accompanimentPanel_->auditionRequested = [this](const AccompanimentArrangement &arrangement)
    { return auditionAccompaniment(arrangement); };
    accompanimentPanel_->confirmationRequested = [this](const AccompanimentArrangement &arrangement)
    { return confirmAccompaniment(arrangement); };
    accompanimentPanel_->cancelled = [this] { cancelAccompanimentPreview(); };
    const auto preview = accompanimentPanel_;
    connect(preview.data(), &QDialog::finished, this,
            [this, preview]
            {
                if (accompanimentPanel_ == preview)
                    accompanimentPanel_.clear();
            });
    accompanimentPanel_->show();
}
bool MainWindow::auditionAccompaniment(const AccompanimentArrangement &arrangement)
{
    if (audioLoading_ || arrangement.melodyFingerprint != accompanimentFingerprint(project_.score))
        return false;
    const bool firstAudition = !auditionArrangement_;
    const auto plan = buildAccompanimentPlan(project_.score, timeline_, arrangement);
    if (!plan.valid() || !player_.setAccompanimentPlan(plan, arrangement.settings))
    {
        setStatusMessage(player_.errorString());
        return false;
    }
    if (!preAuditionMix_)
    {
        preAuditionMix_ = project_.practiceMix;
        preAuditionPlaybackSource_ = playbackSource_ ? playbackSource_->currentData().toInt() : 0;
    }
    if (originalAudioMode())
        playbackSource_->setCurrentIndex(playbackSource_->findData(0));
    auditionArrangement_ = arrangement;
    auditionMix_ = player_.practiceMix();
    auditionMix_.accompanimentEnabled = true;
    player_.setPracticeMix(auditionMix_);
    refreshPracticeControls();
    if (firstAudition && !player_.isPlaying())
        togglePlayback();
    return true;
}
bool MainWindow::confirmAccompaniment(const AccompanimentArrangement &arrangement)
{
    if (audioLoading_ || arrangement.melodyFingerprint != accompanimentFingerprint(project_.score))
        return false;
    const auto plan = buildAccompanimentPlan(project_.score, timeline_, arrangement);
    if (!plan.valid() || !player_.setAccompanimentPlan(plan, arrangement.settings))
    {
        setStatusMessage(player_.errorString());
        return false;
    }
    project_.accompaniment = arrangement;
    if (preAuditionMix_)
        project_.practiceMix = auditionMix_;
    else
        project_.practiceMix.accompanimentEnabled = true;
    auditionArrangement_.reset();
    preAuditionMix_.reset();
    preAuditionPlaybackSource_.reset();
    player_.setPracticeMix(project_.practiceMix);
    markModified();
    refreshPracticeControls();
    refreshIssues();
    return true;
}
void MainWindow::cancelAccompanimentPreview()
{
    if (!preAuditionMix_)
        return;
    const auto previousMix = *preAuditionMix_;
    const bool wasPlaying = player_.isPlaying();
    const int previousSource = preAuditionPlaybackSource_.value_or(0);
    const bool restoreOriginal = previousSource > 0;
    auditionArrangement_.reset();
    preAuditionMix_.reset();
    preAuditionPlaybackSource_.reset();
    const auto settings = project_.accompaniment ? project_.accompaniment->settings : AccompanimentSettings{};
    player_.setAccompanimentPlan(activeAccompanimentPlan(), settings);
    project_.practiceMix = previousMix;
    auto playbackMix = previousMix;
    if (project_.accompaniment &&
        project_.accompaniment->melodyFingerprint != accompanimentFingerprint(project_.score))
        playbackMix.accompanimentEnabled = false;
    player_.setPracticeMix(playbackMix);
    if (restoreOriginal && playbackSource_)
    {
        playbackSource_->setCurrentIndex(playbackSource_->findData(previousSource));
        if (wasPlaying)
            playIntent_ = originalAudio_.play();
    }
    refreshPracticeControls();
}
void MainWindow::updatePracticeMix()
{
    if (loading_ || audioLoading_ || !melodyEnabled_)
        return;
    const auto &currentMix = auditionArrangement_ ? auditionMix_ : project_.practiceMix;
    const double melodyVolume = playbackSource_ && playbackSource_->currentData().toInt() > 0
                                    ? currentMix.melodyVolume
                                    : volume_->value() / 100.0;
    PracticeMix mix{melodyEnabled_->isChecked(), accompanimentEnabled_->isChecked(), melodyVolume,
                    accompanimentVolume_->value() / 100.0};
    if (auditionArrangement_)
        auditionMix_ = mix;
    else
    {
        project_.practiceMix = mix;
        markModified();
    }
    const auto &arrangement = auditionArrangement_ ? auditionArrangement_ : project_.accompaniment;
    if (!project_.staffPerformance &&
        (!arrangement || arrangement->melodyFingerprint != accompanimentFingerprint(project_.score)))
        mix.accompanimentEnabled = false;
    if (!player_.setPracticeMix(mix))
        setStatusMessage(player_.errorString());
}
void MainWindow::refreshPracticeControls()
{
    if (!melodyEnabled_)
        return;
    const auto &mix = auditionArrangement_ ? auditionMix_ : project_.practiceMix;
    const QSignalBlocker melodyBlock(melodyEnabled_), accompanimentBlock(accompanimentEnabled_),
        melodyVolumeBlock(volume_), accompanimentVolumeBlock(accompanimentVolume_),
        patternBlock(accompanimentPattern_);
    melodyEnabled_->setChecked(mix.melodyEnabled);
    accompanimentEnabled_->setChecked(mix.accompanimentEnabled);
    if (!playbackSource_ || playbackSource_->currentData().toInt() == 0)
        volume_->setValue(int(mix.melodyVolume * 100));
    accompanimentVolume_->setValue(int(mix.accompanimentVolume * 100));
    if (auto *percent = findChild<QLabel *>("outputVolumePercent"))
        percent->setText(trText("ui.format.percent").arg(volume_->value()));
    const auto &arrangement = auditionArrangement_ ? auditionArrangement_ : project_.accompaniment;
    if (arrangement)
        accompanimentPattern_->setCurrentIndex(
            accompanimentPattern_->findData(int(arrangement->settings.pattern)));
    const bool stale = arrangement && arrangement->melodyFingerprint != accompanimentFingerprint(project_.score);
    accompanimentPattern_->setEnabled(!project_.staffPerformance && !auditionArrangement_);
    accompanimentEnabled_->setEnabled(project_.staffPerformance || (arrangement.has_value() && !stale));
    bindText(melodyEnabled_,
             project_.staffPerformance ? "ui.staff.primary_parts" : "ui.accompaniment.melody_enabled");
    bindText(accompanimentEnabled_,
             project_.staffPerformance ? "ui.staff.other_parts" : "ui.accompaniment.accompaniment_enabled");
    if (auto *generate = findChild<QPushButton *>("generateAccompaniment"))
        generate->setEnabled(!project_.staffPerformance);
    if (auto *generate = findChild<QPushButton *>("quickGenerateAccompaniment"))
        generate->setEnabled(!project_.staffPerformance);
    if (auto *action = findChild<QAction *>("menuGenerateAccompaniment"))
        action->setEnabled(!project_.staffPerformance);
    accompanimentState_->setText(trText(project_.staffPerformance ? "ui.staff.original_parts"
                                        : stale                   ? "ui.accompaniment.stale_confirmed"
                                        : auditionArrangement_    ? "ui.accompaniment.auditioning"
                                        : project_.accompaniment  ? "ui.accompaniment.confirmed"
                                                                  : "ui.accompaniment.not_generated"));
}
void MainWindow::changeAccompanimentPattern()
{
    if (loading_ || audioLoading_ || auditionArrangement_ || !project_.accompaniment)
        return;
    auto changed = *project_.accompaniment;
    changed.settings.pattern = AccompanimentPattern(accompanimentPattern_->currentData().toInt());
    if (changed.melodyFingerprint == accompanimentFingerprint(project_.score))
    {
        const auto plan = buildAccompanimentPlan(project_.score, timeline_, changed);
        if (!plan.valid() || !player_.setAccompanimentPlan(plan, changed.settings))
        {
            setStatusMessage(player_.errorString());
            refreshPracticeControls();
            return;
        }
    }
    project_.accompaniment = std::move(changed);
    markModified();
    refreshPracticeControls();
}
} // namespace singlilt

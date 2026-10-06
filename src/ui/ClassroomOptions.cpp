// Classroom input, playback, and assessment options.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ClassroomDialog.h"
#include "MainWindow.h"
#include "OptionsDialog.h"
#include "i18n/LanguageManager.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>

namespace singlilt
{
void ClassroomDialog::showEarTraining()
{
    tabs_->setCurrentIndex(1);
}
void ClassroomDialog::openOptions()
{
    if (recording_ || calibrating_ || demonstrating_ || previewStarting_)
        return;
    OptionsContext context;
    context.classroom = true;
    if (lessonIndex_ >= 0)
        context.score = lessons_[static_cast<std::size_t>(lessonIndex_)].score;
    context.transpose = transpose_->value();
    context.speed = speed_->value();
    auto settings = loadAppSettings();
    settings.language = languages_.language();
    auto *owner = dynamic_cast<MainWindow *>(parentWidget());
    if (owner)
        settings.vision.apiKey = owner->appSettings().vision.apiKey;
    OptionsDialog dialog(settings, context, this);
    dialog.applyChanges = [this, owner, previous = settings](const AppSettings &s, const OptionsContext &c) mutable
    {
        validateAppSettings(s, &previous);
        if (owner)
            owner->applyOptions(s, c);
        else
            saveAppSettings(s, &previous);
        if (s.lessonDirectory != lessonDirectory_)
            reloadLessons(s.lessonDirectory.isEmpty() ? defaultLessonDirectory() : s.lessonDirectory);
        stopActivity();
        if (s.gmSoundFontPath != previous.gmSoundFontPath)
            player_.setGmSoundFontPath(s.gmSoundFontPath);
        player_.setAudioBackend(s.audioBackend == 1 ? AudioBackend::WindowsMidi : AudioBackend::SampledPiano);
        const QSignalBlocker speedBlock(speed_), difficultyBlock(difficulty_), transposeBlock(transpose_);
        speed_->setValue(c.speed);
        transpose_->setValue(c.transpose);
        tolerance_->setValue(s.centsTolerance);
        latency_->setValue(s.latencyMilliseconds);
        difficulty_->setCurrentIndex(s.difficulty);
        guide_->setCurrentIndex(s.guide);
        if (s.microphoneId.isEmpty())
            devices_->setCurrentIndex(0);
        else if (devices_->findData(s.microphoneId) >= 0)
            devices_->setCurrentIndex(devices_->findData(s.microphoneId));
        else if (!s.microphoneId.isEmpty())
        {
            devices_->addItem(trText("ui.options.missing_device"), s.microphoneId);
            devices_->setCurrentIndex(devices_->count() - 1);
        }
        preferences_ = s;
        previous = s;
        heard_ = false;
        if (earMode() && !answered_)
            newQuestion();
        languages_.setLanguage(s.language);
        setBusy(false);
        status_->setText(trText("ui.options.saved"));
        return true;
    };
    dialog.exec();
}
} // namespace singlilt

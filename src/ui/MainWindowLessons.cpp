// Classroom entry and lesson-to-workspace coordination.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AccompanimentPanel.h"
#include "ClassroomDialog.h"
#include "MainWindow.h"
#include "NotationRenderer.h"
#include "PracticeScore.h"
#include "i18n/LanguageManager.h"
#include "storage/LessonStore.h"
#include <QApplication>
#include <QStandardPaths>
#include <exception>

namespace singlilt
{
void MainWindow::openPracticeExample(bool imageOnly)
{
    if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning() || !confirmDiscard())
        return;
    try
    {
        auto example = makePracticeScore();
        if (imageOnly)
            recognize(std::move(example.image), "practice-example.png");
        else
            setProject(std::move(example));
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}

void MainWindow::openClassroom()
{
    if (classroom_)
    {
        classroom_->show();
        classroom_->raise();
        classroom_->activateWindow();
        return;
    }
    if (accompanimentPanel_)
        accompanimentPanel_->reject();
    playIntent_ = false;
    if (audioLoading_)
        audioWatcher_.waitForFinished();
    player_.releaseAudioDevice();
    originalAudio_.pause();
    try
    {
        const QString folder =
            settings_.lessonDirectory.isEmpty() ? defaultLessonDirectory() : settings_.lessonDirectory;
        const QString history =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/practice-history.json";
        classroom_ = new ClassroomDialog(languageManager_, player_.audioBackend(), folder, history, this);
        classroom_->openLessonScore = [this](Score score)
        {
            if (!confirmDiscard())
                return;
            try
            {
                Project project;
                project.score = std::move(score);
                project.image = renderNumberedScore(project.score);
                project.generatedNotation = true;
                setProject(std::move(project));
            }
            catch (const std::exception &error)
            {
                showError(QString::fromUtf8(error.what()));
                return;
            }
        };
        // No nested event loop. Other asynchronous tasks still finish, but main audio
        // controls cannot compete with the microphone exercise's owned transport.
        classroom_->setWindowModality(Qt::WindowModal);
        classroom_->show();
    }
    catch (const std::exception &error)
    {
        showError(QString::fromUtf8(error.what()));
    }
}
} // namespace singlilt

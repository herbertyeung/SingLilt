// Default practice material from the bundled lessons.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "PracticeScore.h"
#include "NotationRenderer.h"
#include "i18n/LanguageManager.h"
#include "storage/LessonStore.h"
#include <QApplication>

namespace singlilt
{
Project makePracticeScore()
{
    const auto lessons =
        loadSingingLessons(QApplication::applicationDirPath() + "/assets/lessons", trText("common.lesson_locale"));
    Project project;
    project.score = lessons.front().score;
    project.image = renderNumberedScore(project.score);
    project.generatedNotation = true;
    return project;
}

} // namespace singlilt

// Practice-history write failure and recovery checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "HistoryRecoveryCheck.h"
#include "i18n/LanguageManager.h"
#include "storage/LessonStore.h"
#include "storage/PracticeHistory.h"
#include "ui/ClassroomDialog.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <memory>
#include <stdexcept>

namespace singlilt
{
void runHistoryRecoveryCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                             QApplication &app)
{
    QTimer::singleShot(
        0, &window,
        [&window, &languages, &args, &app]
        {
            QJsonArray checks;
            bool passed = true;
            const auto check = [&](const QString &name, bool valid)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", valid}});
                passed &= valid;
            };
            const QString folder = QFileInfo(args.value("report")).absolutePath();
            try
            {
                const QString fixture = folder + "/history-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
                if (!QDir().mkpath(fixture))
                    throw std::runtime_error("History fixture directory could not be created");
                const QString blockedParent = fixture + "/blocked-parent";
                QFile blocker(blockedParent);
                if (!blocker.open(QIODevice::WriteOnly) || blocker.write("fixture") != 7)
                    throw std::runtime_error("History blocker could not be created");
                blocker.close();
                const QString historyPath = blockedParent + "/history.json";
                auto classroom = std::make_unique<ClassroomDialog>(languages, AudioBackend::WindowsMidi,
                                                                   defaultLessonDirectory(), historyPath, &window);
                classroom->setAttribute(Qt::WA_DeleteOnClose, false);
                classroom->show();
                QApplication::processEvents();
                const QJsonObject attempt{{"kind", "singing"}, {"lessonId", 1},  {"reliable", true},
                                          {"pitch", 85.0},     {"rhythm", 82.0}, {"coverage", 90.0}};
                check("Write failure retains the completed result",
                      !classroom->appendHistory(attempt) && classroom->pendingHistory_.size() == 1);
                const QString firstId =
                    classroom->pendingHistory_.first().toObject().value("attemptId").toString();
                const QString firstTime = classroom->pendingHistory_.first().toObject().value("time").toString();
                classroom->status_->setText(trText("ui.classroom.completed"));
                check("Completion text cannot hide persistent history failure",
                      classroom->historySaveStatus_->isVisible() &&
                          !classroom->historySaveStatus_->text().isEmpty() &&
                          classroom->retryHistory_->isVisible());
                classroom->grab().save(folder + "/history-pending.png");
                check("Further attempts are queued, not silently dropped",
                      !classroom->appendHistory(attempt) && classroom->pendingHistory_.size() == 2);
                check("Failed retry preserves both pending attempts",
                      !classroom->retryHistory() && classroom->pendingHistory_.size() == 2);
                const auto cancelClose = [&]
                {
                    QTimer::singleShot(50, classroom.get(),
                                       []
                                       {
                                           if (auto *dialog =
                                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                                               dialog->button(QMessageBox::Cancel)->click();
                                       });
                };
                cancelClose();
                classroom->reject();
                check("Escape/reject cancellation preserves the window and queue",
                      classroom->isVisible() && classroom->pendingHistory_.size() == 2);
                cancelClose();
                classroom->done(QDialog::Rejected);
                check("Direct done cancellation preserves pending records",
                      classroom->isVisible() && classroom->pendingHistory_.size() == 2);
                window.classroom_ = classroom.get();
                cancelClose();
                check("Parent application close cannot discard classroom records",
                      !window.close() && classroom->pendingHistory_.size() == 2 && window.isVisible());
                check("Existing blocking file remains untouched", QFileInfo(blockedParent).size() == 7);
                if (!QFile::remove(blockedParent) || !QDir().mkpath(blockedParent))
                    throw std::runtime_error("History fixture could not be repaired");
                classroom->retryHistory_->click();
                PracticeHistory restored(historyPath);
                restored.load();
                check("Retry saves every pending attempt exactly once", classroom->pendingHistory_.isEmpty() &&
                                                                            restored.attempts().size() == 2 &&
                                                                            classroom->historyWritable_);
                check("Retry preserves attempt identity and completion time",
                      restored.attempts().first().toObject().value("attemptId").toString() == firstId &&
                          restored.attempts().first().toObject().value("time").toString() == firstTime);
                check("Successful retry removes the persistent failure banner",
                      !classroom->historySaveStatus_->isVisible() && !classroom->retryHistory_->isVisible());
                check("Repeated retry does not duplicate saved attempts", classroom->retryHistory());
                restored.load();
                check("History remains at two entries", restored.attempts().size() == 2);
                auto duplicate = restored.attempts().first().toObject();
                restored.append(duplicate);
                check("Atomic store rejects duplicate attempt identity", restored.attempts().size() == 2);
                restored.append(attempt);
                check("Legacy entries without identity remain compatible", restored.attempts().size() == 3);
                classroom->grab().save(folder + "/history-recovered.png");
            }
            catch (const std::exception &error)
            {
                check(QString::fromUtf8(error.what()), false);
            }
            QFile output(args.value("report"));
            if (!output.open(QIODevice::WriteOnly))
            {
                app.exit(4);
                return;
            }
            const QByteArray bytes =
                QJsonDocument(QJsonObject{{"checks", checks}, {"passed", passed}, {"microphoneRecorded", false}})
                    .toJson();
            if (output.write(bytes) != bytes.size())
                app.exit(4);
            else
                app.exit(passed ? 0 : 3);
        });
}
} // namespace singlilt

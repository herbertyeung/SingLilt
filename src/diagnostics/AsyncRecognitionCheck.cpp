// Asynchronous vision-request and cancellation regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AsyncRecognitionCheck.h"
#include "i18n/LanguageManager.h"
#include "recognition/CloudRecognitionTask.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <deque>

namespace singlilt
{
namespace
{
using TaskState = CloudRecognitionTask::State;

Project fixtureProject(const std::string &title, const QColor &color, const QSize &size)
{
    Project project;
    project.score.title = title;
    project.score.bpm = 60;
    project.image = QImage(size, QImage::Format_RGB32);
    project.image.fill(color);
    for (int index = 0; index < 8; ++index)
    {
        Note note;
        note.id = index;
        note.degree = index % 7 + 1;
        note.measure = index / 4;
        note.source = {double(8 + index % 4 * 20), double(8 + index / 4 * 30), 12, 20};
        note.verseLyrics = {"initial A", "initial B"};
        note.lyric = "initial A\ninitial B";
        project.score.notes.push_back(note);
    }
    return project;
}

QPushButton *translatedButton(QWidget &parent, const char *key)
{
    for (auto *button : parent.findChildren<QPushButton *>())
    {
        if (button->property("_ui_text").toString() == QLatin1String(key))
            return button;
    }
    return nullptr;
}

class AsyncRecognitionCheck final : public QObject
{
  public:
    AsyncRecognitionCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), app_(app), scenario_(args.value("async-recognition-check")),
          endpoint_(args.value("vision-endpoint")), reportPath_(args.value("report")),
          outputDirectory_(QFileInfo(reportPath_).absolutePath())
    {
        gate_.setInterval(15);
        dialogGate_.setInterval(10);
        deadline_.setSingleShot(true);
        QObject::connect(&gate_, &QTimer::timeout, this, &AsyncRecognitionCheck::advance);
        QObject::connect(&dialogGate_, &QTimer::timeout, this, &AsyncRecognitionCheck::answerModal);
        QObject::connect(&deadline_, &QTimer::timeout, this,
                         [this] { finish(false, "Diagnostic state deadline expired"); });
    }

    void start()
    {
        elapsed_.start();
        gate_.start();
        dialogGate_.start();
        deadline_.start(scenario_ == "timeout" ? 40000 : 15000);
        QTimer::singleShot(0, this, &AsyncRecognitionCheck::begin);
    }

  private:
    enum class Phase
    {
        Starting,
        AwaitRequest,
        AwaitResult,
        AwaitCancelled,
        AwaitLateResponse,
        AwaitRestart,
        Finished
    };

    struct ModalAnswer
    {
        QString identity;
        QMessageBox::StandardButton button;
    };

    bool check(bool condition, const char *name)
    {
        checks_[name] = condition;
        if (!condition)
            finish(false, QString::fromLatin1(name));
        return condition;
    }

    bool markerExists(const char *name) const
    {
        return QFileInfo::exists(outputDirectory_ + "/" + QString::fromLatin1(name));
    }

    bool currentProjectPreserved() const
    {
        return scoreToJson(window_.project().score) == protectedScore_ &&
               window_.project().image == protectedImage_ && window_.hasUnsavedChanges();
    }

    bool draftsPreserved() const
    {
        const auto *a = window_.findChild<QLineEdit *>("lyricAEditor");
        const auto *b = window_.findChild<QLineEdit *>("lyricBEditor");
        return a && b && a->text() == "Unsaved editor draft A" && b->text() == "Unsaved editor draft B";
    }

    bool requestJob()
    {
        VisionConfig config;
        config.endpoint = endpoint_;
        config.model = "gpt-6.1-sol";
        config.timeoutSeconds = 30;
        return window_.cloudRecognitionTask().start(requestImage_, "fixture-original.png", "Fixture source A",
                                                    config);
    }

    void begin()
    {
        if (!check(!reportPath_.isEmpty() && !endpoint_.isEmpty(), "diagnostic_arguments_present"))
            return;
        auto original = fixtureProject("Original request project", QColor(245, 228, 210), QSize(96, 96));
        requestImage_ = original.image;
        window_.setProject(original);
        if (!check(window_.player().setAudioBackend(AudioBackend::WindowsMidi), "system_audio_selected"))
            return;
        if (scenario_ == "success" && !check(window_.player().play(), "playback_started_before_request"))
            return;
        if (!check(requestJob(), "cloud_task_started"))
            return;
        phase_ = Phase::AwaitRequest;
    }

    bool createEditsAndDrafts()
    {
        auto current = window_.project();
        window_.setProject(current, true);
        auto *a = window_.findChild<QLineEdit *>("lyricAEditor");
        auto *b = window_.findChild<QLineEdit *>("lyricBEditor");
        auto *shared = window_.findChild<QCheckBox *>("sharedLyric");
        auto *apply = translatedButton(window_, "ui.inspector.apply");
        if (!check(a && b && shared && apply, "editing_controls_exist"))
            return false;
        shared->setChecked(false);
        if (!check(a->isEnabled() && b->isEnabled() && apply->isEnabled(), "editing_controls_enabled"))
            return false;
        if (!check(a->mapTo(&window_, QPoint(0, a->height())).y() <= b->mapTo(&window_, QPoint()).y(),
                   "editor_rows_do_not_overlap_with_task_banner"))
            return false;
        a->setText("Committed while cloud request runs");
        b->setText("Committed B");
        apply->click();
        if (!check(window_.project().score.notes.front().lyric.find("Committed while cloud request runs") !=
                       std::string::npos,
                   "committed_edit_during_request"))
            return false;
        a->setText("Unsaved editor draft A");
        b->setText("Unsaved editor draft B");
        protectedScore_ = scoreToJson(window_.project().score);
        protectedImage_ = window_.project().image;
        return true;
    }

    void receivedRequest()
    {
        if (!check(window_.cloudRecognitionTask().isRunning(), "task_running_after_upload") ||
            !check(window_.centralWidget()->isEnabled(), "main_ui_enabled_during_request"))
            return;
        if (scenario_ == "close")
        {
            closeAndFinish();
            return;
        }
        if (scenario_ == "cancel")
        {
            auto *cancel = window_.findChild<QPushButton *>("cloudTaskCancel");
            if (!check(cancel && cancel->isEnabled(), "cancel_button_enabled"))
                return;
            cancel->click();
            phase_ = Phase::AwaitCancelled;
            return;
        }
        if (scenario_ == "success")
        {
            if (!check(window_.player().isPlaying() && window_.player().positionTicks() > 0,
                       "playback_continues_during_request"))
                return;
            const auto other =
                fixtureProject("Other project opened during request", QColor(205, 240, 225), QSize(96, 112));
            const QString path = outputDirectory_ + "/other-project.jpp";
            saveProject(path, other);
            window_.openFile(path);
            if (!check(window_.project().score.title == other.score.title &&
                           window_.cloudRecognitionTask().isRunning(),
                       "another_project_opens_while_request_runs"))
                return;
        }
        if (createEditsAndDrafts())
            phase_ = Phase::AwaitResult;
    }

    QDialog *openPreview()
    {
        auto *button = window_.findChild<QPushButton *>("cloudTaskPreview");
        if (!check(button && button->isEnabled(), "preview_button_available"))
            return nullptr;
        button->click();
        QDialog *preview = nullptr;
        for (auto *candidate : window_.findChildren<QDialog *>("recognitionPreview"))
        {
            // A closed dialog may still be a child until its deferred deletion.
            if (candidate->isVisible())
            {
                preview = candidate;
                break;
            }
        }
        if (!check(preview && preview->isVisible() && !preview->isModal(), "preview_is_nonmodal"))
            return nullptr;
        return preview;
    }

    bool applyWithAnswers(QDialog &preview, bool acceptReplacement, QMessageBox::StandardButton dirtyAnswer)
    {
        auto *apply = preview.findChild<QPushButton *>("recognitionApply");
        if (!check(apply && apply->isEnabled(), "preview_apply_available"))
            return false;
        answers_.push_back({"cloudReplaceConfirmation", acceptReplacement ? QMessageBox::Yes : QMessageBox::No});
        if (acceptReplacement)
            answers_.push_back({"ui.dialog.unsaved_message", dirtyAnswer});
        apply->click();
        if (phase_ == Phase::Finished)
            return false;
        return check(answers_.empty(), "expected_confirmation_dialogs_answered");
    }

    void successReady()
    {
        auto &task = window_.cloudRecognitionTask();
        if (!check(currentProjectPreserved() && draftsPreserved(),
                   "completion_preserves_project_edits_and_drafts") ||
            !check(!QApplication::activeModalWidget(), "completion_has_no_modal_dialog") ||
            !check(window_.centralWidget()->isEnabled(), "completion_leaves_main_ui_enabled") ||
            !check(task.result() && task.image() == requestImage_ && task.sourceLabel() == "Fixture source A",
                   "result_keeps_original_request_snapshot"))
            return;
        auto *existing = window_.findChild<QDialog *>("recognitionPreview");
        if (!check(!existing || !existing->isVisible(), "completion_does_not_open_preview_automatically"))
            return;
        if (!check(window_.grab().save(outputDirectory_ + "/ready.png"), "ready_window_screenshot_saved"))
            return;
        QDialog *preview = openPreview();
        if (!preview)
            return;
        if (!check(preview->grab().save(outputDirectory_ + "/preview.png"), "preview_screenshot_saved"))
            return;
        auto *table = preview->findChild<QTableWidget *>("previewNotes");
        auto *summary = preview->findChild<QLabel *>("previewSummary");
        if (!check(table && table->rowCount() == int(task.result()->score.notes.size()) &&
                       preview->findChild<QWidget *>("previewScoreView") && summary && !summary->text().isEmpty(),
                   "preview_contains_image_notes_and_summary"))
            return;
        if (!check(table->columnCount() == 8 && table->item(0, 1) && table->item(0, 6) && table->item(0, 7) &&
                       table->item(0, 1)->text().contains(QChar(0x266f)) && table->item(0, 6)->text() == "D" &&
                       table->item(0, 7)->text() == QString(QChar(0x2713)),
                   "preview_shows_accidental_key_change_and_tie"))
            return;
        preview->close();
        if (!check(task.state() == TaskState::Ready && task.result(), "closing_preview_keeps_candidate"))
            return;
        preview = openPreview();
        if (!preview || !applyWithAnswers(*preview, false, QMessageBox::Cancel))
            return;
        if (!check(currentProjectPreserved() && draftsPreserved() && task.state() == TaskState::Ready,
                   "declining_replacement_changes_nothing"))
            return;
        preview = openPreview();
        if (!preview || !applyWithAnswers(*preview, true, QMessageBox::Cancel))
            return;
        if (!check(currentProjectPreserved() && draftsPreserved() && task.state() == TaskState::Ready,
                   "cancelling_dirty_guard_preserves_candidate_and_edits"))
            return;
        preview = openPreview();
        if (!preview || !applyWithAnswers(*preview, true, QMessageBox::Discard))
            return;
        if (!check(window_.project().score.title == "Async cloud fixture" &&
                       window_.project().image == requestImage_ && window_.hasUnsavedChanges(),
                   "explicit_apply_uses_original_image_and_marks_dirty"))
            return;
        check(task.state() == TaskState::Idle && !task.result(), "applied_candidate_is_consumed");
        if (phase_ != Phase::Finished)
            finish(true);
    }

    void failureReady()
    {
        auto &task = window_.cloudRecognitionTask();
        if (!check(currentProjectPreserved() && draftsPreserved(), "failure_preserves_project_edits_and_drafts") ||
            !check(!QApplication::activeModalWidget() && window_.centralWidget()->isEnabled(),
                   "failure_is_nonmodal_and_main_ui_remains_enabled") ||
            !check(!task.isRunning() && !task.result() && !task.errorString().isEmpty(),
                   "failure_has_no_candidate"))
            return;
        if (scenario_ == "timeout")
        {
            if (!check(localizeMessage(task.errorString()) ==
                           trText("messages.recognition.request_timeout").arg(30),
                       "deadline_reports_30_seconds"))
                return;
            auto clean = window_.project();
            window_.setProject(clean, false);
            closeAndFinish();
            return;
        }
        finish(true);
    }

    void answerModal()
    {
        auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!message || answers_.empty())
            return;
        const auto answer = answers_.front();
        if (message->objectName() != answer.identity &&
            message->property("_ui_text").toString() != answer.identity)
            return;
        auto *button = message->button(answer.button);
        if (!button)
            return;
        answers_.pop_front();
        button->click();
    }

    void advance()
    {
        if (phase_ == Phase::Finished || advancing_)
            return;
        QScopedValueRollback<bool> guard(advancing_, true);
        if (elapsed_.elapsed() > (scenario_ == "timeout" ? 45000 : 15000))
        {
            finish(false, "Diagnostic state deadline expired");
            return;
        }
        try
        {
            auto &task = window_.cloudRecognitionTask();
            switch (phase_)
            {
            case Phase::Starting:
                break;
            case Phase::AwaitRequest:
                if (markerExists("request-1.received"))
                    receivedRequest();
                break;
            case Phase::AwaitResult:
                if (task.isRunning())
                    break;
                if (scenario_ == "success")
                {
                    if (check(task.state() == TaskState::Ready, "request_succeeded"))
                        successReady();
                }
                else if (check(task.state() == TaskState::Failed, "request_failed"))
                    failureReady();
                break;
            case Phase::AwaitCancelled:
                if (task.isRunning())
                    break;
                if (check(task.state() == TaskState::Cancelled && !task.result(), "cancelled_without_candidate"))
                    phase_ = Phase::AwaitLateResponse;
                break;
            case Phase::AwaitLateResponse:
                if (!markerExists("request-1.responded"))
                    break;
                if (!check(task.state() == TaskState::Cancelled && !task.result(), "late_response_is_discarded") ||
                    !check(window_.project().score.title == "Original request project",
                           "cancel_keeps_current_project"))
                    break;
                if (check(requestJob(), "cancelled_task_can_restart"))
                    phase_ = Phase::AwaitRestart;
                break;
            case Phase::AwaitRestart:
                if (task.isRunning())
                    break;
                if (!check(task.state() == TaskState::Ready && task.result() &&
                               task.result()->score.title == "Async retry fixture",
                           "restart_returns_only_new_result"))
                    break;
                if (auto *discard = window_.findChild<QPushButton *>("cloudTaskDiscard"))
                    discard->click();
                if (check(task.state() == TaskState::Idle && !task.result() && task.image().isNull(),
                          "discard_clears_result_and_snapshot"))
                    finish(true);
                break;
            case Phase::Finished:
                break;
            }
        }
        catch (const std::exception &error)
        {
            finish(false, QString::fromUtf8(error.what()));
        }
    }

    bool writeReport(bool passed, const QString &error = {})
    {
        const QJsonObject report{{"scenario", scenario_},
                                 {"passed", passed},
                                 {"checks", checks_},
                                 {"error", error},
                                 {"elapsedMs", elapsed_.elapsed()}};
        QFile file(reportPath_);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        const auto bytes = QJsonDocument(report).toJson();
        const bool written = file.write(bytes) == bytes.size();
        QTextStream(stdout) << bytes;
        return written;
    }

    void closeAndFinish()
    {
        if (scenario_ == "close" && window_.cloudRecognitionTask().isRunning())
        {
            answers_.push_back({"cloudExitConfirmation", QMessageBox::No});
            const bool closed = window_.close();
            if (phase_ == Phase::Finished ||
                !check(!closed && window_.isVisible() &&
                           window_.cloudRecognitionTask().state() == TaskState::Running && answers_.empty(),
                       "declining_exit_keeps_window_and_request_running"))
                return;
            const auto current = window_.project();
            window_.setProject(current, true);
            const auto beforeClose = scoreToJson(window_.project().score);
            answers_.push_back({"cloudExitConfirmation", QMessageBox::Yes});
            answers_.push_back({"ui.dialog.unsaved_message", QMessageBox::Cancel});
            const bool dirtyClosed = window_.close();
            if (phase_ == Phase::Finished ||
                !check(!dirtyClosed && window_.isVisible() &&
                           window_.cloudRecognitionTask().state() == TaskState::Running &&
                           window_.hasUnsavedChanges() && scoreToJson(window_.project().score) == beforeClose &&
                           answers_.empty(),
                       "cancelling_unsaved_exit_keeps_project_and_request_running"))
                return;
            answers_.push_back({"cloudExitConfirmation", QMessageBox::Yes});
            answers_.push_back({"ui.dialog.unsaved_message", QMessageBox::Discard});
        }
        phase_ = Phase::Finished;
        gate_.stop();
        QFile marker(outputDirectory_ + "/close-requested");
        if (!marker.open(QIODevice::WriteOnly))
        {
            app_.exit(4);
            return;
        }
        marker.write("close\n");
        marker.close();
        QElapsedTimer closing;
        closing.start();
        const bool accepted = window_.close();
        dialogGate_.stop();
        deadline_.stop();
        checks_["close_accepted"] = accepted;
        checks_["close_event_returned_within_2_seconds"] = closing.elapsed() < 2000;
        const bool passed = accepted && closing.elapsed() < 2000;
        if (!writeReport(passed))
            app_.exit(4);
        else
            app_.exit(passed ? 0 : 3);
    }

    void finish(bool passed, const QString &error = {})
    {
        phase_ = Phase::Finished;
        gate_.stop();
        dialogGate_.stop();
        deadline_.stop();
        window_.player().stop();
        window_.cloudRecognitionTask().cancel();
        app_.exit(writeReport(passed, error) ? (passed ? 0 : 3) : 4);
    }

    MainWindow &window_;
    QApplication &app_;
    QString scenario_;
    QString endpoint_;
    QString reportPath_;
    QString outputDirectory_;
    QTimer gate_;
    QTimer dialogGate_;
    QTimer deadline_;
    QElapsedTimer elapsed_;
    QJsonObject checks_;
    QJsonObject protectedScore_;
    QImage requestImage_;
    QImage protectedImage_;
    std::deque<ModalAnswer> answers_;
    Phase phase_ = Phase::Starting;
    bool advancing_ = false;
};
} // namespace

void runAsyncRecognitionCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    auto *check = new AsyncRecognitionCheck(window, args, app);
    check->start();
}
} // namespace singlilt

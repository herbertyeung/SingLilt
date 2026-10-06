// Lesson playback, recording, and assessment integration checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ClassroomCheck.h"
#include "i18n/LanguageManager.h"
#include "practice/PracticeSession.h"
#include "storage/LessonStore.h"
#include "storage/PracticeHistory.h"
#include "storage/ProjectStore.h"
#include "ui/ClassroomDialog.h"
#include "ui/MainWindow.h"
#include "ui/NotationRenderer.h"
#include "ui/ScoreView.h"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <algorithm>
#include <stdexcept>

namespace singlilt
{
namespace
{
void write(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    const auto bytes = QJsonDocument(object).toJson();
    if (file.write(bytes) != bytes.size())
        throw std::runtime_error(file.errorString().toStdString());
}
class ClassroomProbe final : public QObject
{
  public:
    ClassroomProbe(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                   QApplication &app)
        : QObject(&window), window_(window), languages_(languages), args_(args), app_(app),
          folder_(QFileInfo(args.value("report")).absolutePath()), historyPath_(folder_ + "/practice-history.json")
    {
        QDir().mkpath(folder_);
        elapsed_.start();
        timer_.setInterval(50);
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        timer_.start();
    }

  private:
    template <typename T> T *control(const char *name)
    {
        auto *widget = classroom_->findChild<T *>(name);
        if (!widget)
            throw std::runtime_error(QString("Missing control: %1").arg(name).toStdString());
        return widget;
    }
    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ = passed_ && passed;
    }
    void fixtureChecks()
    {
        const QString original = QApplication::applicationDirPath() + "/assets/lessons";
        const auto lessons = loadSingingLessons(original, "zh_CN");
        check("Eight external course files", lessons.size() == 8);
        for (const auto &lesson : lessons)
        {
            Project project;
            project.score = lesson.score;
            project.image = renderNumberedScore(project.score);
            project.generatedNotation = true;
            const QString file = folder_ + QString("/lesson-%1.jpp").arg(lesson.id);
            saveProject(file, project);
            const auto opened = loadProject(file);
            check(QString("Lesson %1 render/save/reopen/timeline").arg(lesson.id),
                  !opened.image.isNull() && opened.score.notes.size() == project.score.notes.size() &&
                      buildTimeline(opened.score).valid() && !lesson.goal.empty() && !lesson.steps.empty());
        }
        const QString fixture = folder_ + "/editable-lessons";
        QDir().mkpath(fixture);
        for (const auto &file : QDir(original).entryList({"*.json"}, QDir::Files))
        {
            const QString destination = fixture + '/' + file;
            if (QFile::exists(destination))
                throw std::runtime_error("Diagnostic output folder must be fresh");
            if (!QFile::copy(original + '/' + file, destination))
                throw std::runtime_error("Fixture copy failed");
        }
        QFile first(original + "/01-scales.json");
        first.open(QIODevice::ReadOnly);
        auto custom = QJsonDocument::fromJson(first.readAll()).object();
        custom.insert("id", 9);
        custom.insert(
            "title", QJsonObject{{"zh_CN", "第九课 · 文件热加载验证"}, {"en_US", "Lesson 9 · File reload check"}});
        write(fixture + "/09-custom.json", custom);
        QFile indexFile(fixture + "/index.json");
        indexFile.open(QIODevice::ReadOnly);
        auto index = QJsonDocument::fromJson(indexFile.readAll()).object();
        indexFile.close();
        auto entries = index.value("lessons").toArray();
        entries.append("09-custom.json");
        index.insert("lessons", entries);
        write(fixture + "/index.json", index);
        classroom_ = new ClassroomDialog(languages_, AudioBackend::WindowsMidi, original, historyPath_, &window_);
        classroom_->show();
        classroom_->reloadLessons(fixture);
        check("Adding a lesson file needs no compilation", classroom_->lessons().size() == 9);
        const auto validIndex = index;
        entries.append("../outside.json");
        index.insert("lessons", entries);
        write(fixture + "/index.json", index);
        bool rejected = false;
        try
        {
            classroom_->reloadLessons(fixture);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check("Invalid catalog preserves the loaded courses", rejected && classroom_->lessons().size() == 9);
        write(fixture + "/index.json", validIndex);
        custom.insert("schema", 2);
        write(fixture + "/09-custom.json", custom);
        rejected = false;
        try
        {
            loadSingingLessons(fixture, "zh_CN");
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check("Unsupported lesson schemas are rejected", rejected);
        custom.insert("schema", 1);
        custom.insert("id", 1);
        write(fixture + "/09-custom.json", custom);
        rejected = false;
        try
        {
            loadSingingLessons(fixture, "zh_CN");
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check("Duplicate lesson IDs are rejected", rejected);
        classroom_->reloadLessons(original);
        auto *selector = control<QComboBox>("classroomLesson");
        for (int i = 0; i < 8; ++i)
        {
            selector->setCurrentIndex(i);
            check(QString("Course %1 selection shows teaching instructions").arg(i + 1),
                  control<QTextEdit>("classroomInstructions")->toPlainText().size() > 50);
        }
        QApplication::processEvents();
        classroom_->grab().save(folder_ + "/classroom-zh.png");
        const QString title = QString::fromStdString(window_.project().score.title);
        check("Classroom leaves the main score intact", !title.isEmpty() && !window_.hasUnsavedChanges());
        selector->setCurrentIndex(0);
        control<QTabWidget>("classroomTabs")->setCurrentIndex(1);
        check("Answer score hidden before submission", !control<QWidget>("earAnswerScore")->isVisible());
        auto *answerView = dynamic_cast<ScoreView *>(control<QGraphicsView>("earAnswerScore"));
        answerView->noteClicked(0);
        check("Hidden ear answer cannot emit a pitch hint", !classroom_->player().isPreviewLoading() &&
              classroom_->player().voiceState().previewNoteOns == 0);
        check("Answer submission requires actually listening", !control<QPushButton>("earSubmit")->isEnabled());
        check("Echo recording requires listening first", !control<QPushButton>("classroomRecord")->isEnabled());
        control<QPushButton>("classroomListen")->click();
        check("Exercise settings locked while sounding", !control<QComboBox>("classroomLesson")->isEnabled());
    }
    void finish(const QString &error = {})
    {
        timer_.stop();
        if (!error.isEmpty())
            check(error, false);
        if (classroom_)
            classroom_->close();
        microphone_.stop();
        write(args_.value("report"), {{"passed", passed_},
                                      {"checks", checks_},
                                      {"elapsedMilliseconds", elapsed_.elapsed()},
                                      {"microphone", micReport_}});
        app_.exit(passed_ ? 0 : 2);
    }
    void advance()
    {
        try
        {
            if (elapsed_.elapsed() > 50000)
            {
                finish("Classroom diagnostic timeout");
                return;
            }
            if (step_ == 0)
            {
                fixtureChecks();
                ++step_;
                return;
            }
            if (step_ == 1)
            {
                if (!control<QPushButton>("earSubmit")->isEnabled())
                    return;
                control<QTableWidget>("earOptions")->selectRow(classroom_->question().correctIndex);
                control<QPushButton>("earSubmit")->click();
                check("Answer revealed only after submission", control<QWidget>("earAnswerScore")->isVisible());
                check("One submitted answer creates one history entry",
                      control<QTableWidget>("practiceHistory")->rowCount() == 1);
                control<QPushButton>("earSubmit")->click();
                check("Repeated submission cannot duplicate credit",
                      control<QTableWidget>("practiceHistory")->rowCount() == 1);
                languages_.setLanguage("en_US");
                QApplication::processEvents();
                classroom_->layout()->activate();
                QApplication::processEvents();
                check("Language switching preserves submission and question",
                      !control<QPushButton>("earSubmit")->isEnabled() &&
                          control<QWidget>("earAnswerScore")->isVisible());
                classroom_->grab().save(folder_ + "/classroom-en.png");
                languages_.setLanguage("zh_CN");
                QApplication::processEvents();
                control<QPushButton>("earNext")->click();
                check("New question hides previous answer", !control<QWidget>("earAnswerScore")->isVisible());
                control<QPushButton>("classroomListen")->click();
                ++step_;
                return;
            }
            if (step_ == 2)
            {
                if (!control<QPushButton>("earSubmit")->isEnabled())
                    return;
                control<QPushButton>("classroomListen")->click();
                ++step_;
                return;
            }
            if (step_ == 3)
            {
                if (!control<QPushButton>("earSubmit")->isEnabled())
                    return;
                auto *answers = control<QTableWidget>("earOptions");
                answers->selectRow((classroom_->question().correctIndex + 1) % answers->rowCount());
                control<QPushButton>("earSubmit")->click();
                PracticeHistory history(historyPath_);
                history.load();
                check("History survives reopening", history.attempts().size() == 2);
                check("Replay counts are separate from first answers",
                      history.attempts()[0].toObject().value("replays").toInt() == 1 &&
                          history.attempts()[1].toObject().value("replays").toInt() == 2);
                check("Wrong answer is recorded rather than regraded",
                      !history.attempts()[1].toObject().value("correct").toBool());
                control<QPushButton>("earWeak")->click();
                check("Weak-area retry creates a new hidden question",
                      !control<QWidget>("earAnswerScore")->isVisible());
                const auto devices = microphone_.devices();
                micReport_.insert("deviceCount", static_cast<int>(devices.size()));
                micReport_.insert("acousticSingingAccuracyTested", false);
                if (!args_.isSet("microphone-check"))
                {
                    finish();
                    return;
                }
                if (devices.empty())
                {
                    finish("No microphone available for requested device check");
                    return;
                }
                micReport_.insert("deviceName", devices.front().name);
                deviceId_ = devices.front().id;
                microphone_.calibrateNoise(devices.front().id);
                ++step_;
                return;
            }
            if (step_ == 6)
            {
                if (!control<QPushButton>("classroomRecord")->isEnabled())
                    return;
                control<QPushButton>("classroomRecord")->click();
                auto *view = dynamic_cast<ScoreView *>(control<QGraphicsView>("classroomScore"));
                const auto previews = classroom_->player().voiceState().previewNoteOns;
                view->noteClicked(0);
                check("Recording clicks do not contaminate microphone audio", !classroom_->player().isPreviewLoading() &&
                      classroom_->player().voiceState().previewNoteOns == previews);
                check("Live echo capture locks exercise settings",
                      !control<QComboBox>("earExercise")->isEnabled());
                check("Live echo keeps target notation hidden", !control<QWidget>("earAnswerScore")->isVisible());
                control<QPushButton>("classroomStop")->click();
                check("Cancelled microphone attempt creates no grade",
                      control<QTableWidget>("practiceHistory")->rowCount() == 2);
                control<QPushButton>("classroomRecord")->click();
                ++step_;
                return;
            }
            if (step_ == 7)
            {
                if (control<QTableWidget>("singingResults")->rowCount() == 0)
                    return;
                check("Real mic echo produces per-note feedback",
                      control<QTableWidget>("singingResults")->rowCount() == 1);
                check("Echo answer appears only after recording completes",
                      control<QWidget>("earAnswerScore")->isVisible());
                check("Completed mic attempt writes one summary",
                      control<QTableWidget>("practiceHistory")->rowCount() == 3);
                check("Real recording becomes replayable", control<QPushButton>("classroomReplay")->isEnabled());
                classroom_->grab().save(folder_ + "/microphone-feedback.png");
                control<QPushButton>("classroomReplay")->click();
                check("Replay exposes an explicit stop action",
                      control<QPushButton>("classroomStop")->isEnabled());
                control<QPushButton>("classroomStop")->click();
                check("Replay does not create a second attempt",
                      control<QTableWidget>("practiceHistory")->rowCount() == 3);
                finish();
                return;
            }
            const auto capture = microphone_.snapshot();
            if (capture.state == PracticeSession::State::Failed)
            {
                micReport_.insert("error", capture.error);
                finish("Real microphone capture failed");
            }
            else if (capture.state == PracticeSession::State::Finished)
            {
                if (step_ == 4)
                {
                    micReport_.insert("captured", true);
                    micReport_.insert("noiseGate", capture.noiseGate);
                    micReport_.insert("latestRms", capture.latest.rms);
                    check("Real WASAPI microphone data arrived and stopped", true);
                    microphone_.record(deviceId_, MicrophoneCapture::clockSeconds() + 0.5, 0.5);
                    ++step_;
                }
                else
                {
                    const QString wave = folder_ + "/real-microphone.wav";
                    microphone_.exportWave(wave);
                    QFile file(wave);
                    file.open(QIODevice::ReadOnly);
                    check("Real recorded WAV saved and reopened", file.size() > 44 && file.read(4) == "RIFF");
                    micReport_.insert("recordedBytes", file.size());
                    control<QComboBox>("classroomLesson")->setCurrentIndex(2);
                    control<QDoubleSpinBox>("classroomSpeed")->setValue(1.5);
                    control<QPushButton>("classroomListen")->click();
                    ++step_;
                }
            }
        }
        catch (const std::exception &error)
        {
            finish(QString::fromUtf8(error.what()));
        }
    }
    MainWindow &window_;
    LanguageManager &languages_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QString folder_, historyPath_, deviceId_;
    QPointer<ClassroomDialog> classroom_;
    PracticeSession microphone_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QJsonArray checks_;
    QJsonObject micReport_;
    bool passed_ = true;
    int step_ = 0;
};
} // namespace
void runClassroomCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                       QApplication &app)
{
    new ClassroomProbe(window, languages, args, app);
}
} // namespace singlilt

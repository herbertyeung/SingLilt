// Settings validation and current-score option checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "OptionsCheck.h"
#include "settings/AppSettings.h"
#include "storage/LessonStore.h"
#include "storage/ProjectStore.h"
#include "ui/ClassroomDialog.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDate>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <memory>
#include <stdexcept>

namespace singlilt
{
namespace
{
void runOptionsRangeCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        100, &window,
        [&window, &args, &app]
        {
            QJsonArray checks;
            bool passed = true;
            const auto check = [&](const QString &name, bool valid)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", valid}});
                passed &= valid;
            };
            try
            {
                Project imported;
                imported.score.title = "Imported range fixture";
                imported.score.bpm = 420;
                imported.score.beatsPerBar = 3;
                imported.score.beatUnit = 1;
                Note note;
                note.durationTicks = 65 * TicksPerQuarter;
                note.hasImageAnchor = false;
                imported.score.notes.push_back(note);
                imported.generatedNotation = true;
                window.setProject(std::move(imported));
                const auto originalScore = scoreToJson(window.project().score);
                auto *tempo = window.findChild<QDoubleSpinBox *>("tempo");
                auto *meterBottom = window.findChild<QComboBox *>("meterBottom");
                auto *duration = window.findChild<QDoubleSpinBox *>("noteDuration");
                check("Imported BPM 420, denominator 1 and 65-quarter duration display without truncation",
                      tempo && tempo->value() == 420 && meterBottom && meterBottom->currentData().toInt() == 1 &&
                          duration && duration->value() == 65);

                const AppSettings settings = window.appSettings();
                OptionsContext context;
                context.score = window.project().score;
                OptionsDialog dialog(settings, context, &window);
                auto *currentTempo = dialog.findChild<QDoubleSpinBox *>("optionsCurrentTempo");
                auto *currentMeterTop = dialog.findChild<QSpinBox *>("optionsCurrentMeterTop");
                auto *currentMeterBottom = dialog.findChild<QSpinBox *>("optionsCurrentMeterBottom");
                auto *markers = dialog.findChild<QCheckBox *>("optionsMarkers");
                auto *buttons = dialog.findChild<QDialogButtonBox *>("optionsButtons");
                if (!currentTempo || !currentMeterTop || !currentMeterBottom || !markers || !buttons)
                    throw std::runtime_error("Missing options range check field");
                check("Options displays imported BPM 420 and meter 3/1", currentTempo->value() == 420 &&
                                                                             currentMeterTop->value() == 3 &&
                                                                             currentMeterBottom->value() == 1);
                int applications = 0;
                auto appliedContext = context;
                auto appliedSettings = settings;
                dialog.applyChanges = [&](const AppSettings &updatedSettings, const OptionsContext &updatedContext)
                {
                    ++applications;
                    appliedSettings = updatedSettings;
                    appliedContext = updatedContext;
                    return true;
                };
                dialog.show();
                markers->setChecked(!settings.showMarkers);
                buttons->button(QDialogButtonBox::Apply)->click();
                check("Unrelated Options Apply preserves every imported score value",
                      applications == 1 && dialog.isVisible() &&
                          appliedSettings.showMarkers != settings.showMarkers &&
                          scoreToJson(appliedContext.score) == originalScore &&
                          scoreToJson(window.project().score) == originalScore);
                dialog.reject();

                QTemporaryDir lessonDirectory;
                QFile lessonAsset(QApplication::applicationDirPath() + "/assets/lessons/01-scales.json");
                if (!lessonDirectory.isValid() || !lessonAsset.open(QIODevice::ReadOnly))
                    throw std::runtime_error("Cannot read lesson range fixture source");
                auto lesson = QJsonDocument::fromJson(lessonAsset.readAll()).object();
                auto lessonScore = lesson.value("score").toObject();
                const auto sourceNotes = lessonScore.value("notes").toArray();
                if (lesson.isEmpty() || sourceNotes.isEmpty())
                    throw std::runtime_error("Invalid lesson range fixture source");
                auto lessonNote = sourceNotes.first().toObject();
                lessonNote.remove("measure");
                lessonNote.insert("durationTicks", 65 * TicksPerQuarter);
                QJsonArray lessonNotes{lessonNote};
                lessonNote.insert("durationTicks", 191 * TicksPerQuarter);
                lessonNotes.append(lessonNote);
                lessonNote.insert("durationTicks", TicksPerQuarter);
                lessonNotes.append(lessonNote);
                lessonScore.insert("notes", lessonNotes);
                lessonScore.insert("bpm", 420);
                lessonScore.insert("beatsPerBar", 64);
                lessonScore.insert("beatUnit", 1);
                lesson.insert("score", lessonScore);
                const auto lessonBytes = QJsonDocument(lesson).toJson();
                QFile lessonFile(lessonDirectory.filePath("range.json"));
                if (!lessonFile.open(QIODevice::WriteOnly) || lessonFile.write(lessonBytes) != lessonBytes.size())
                    throw std::runtime_error("Cannot write lesson range fixture");
                lessonFile.close();
                const auto indexBytes =
                    QJsonDocument(QJsonObject{{"schema", 1}, {"lessons", QJsonArray{"range.json"}}}).toJson();
                QFile lessonIndex(lessonDirectory.filePath("index.json"));
                if (!lessonIndex.open(QIODevice::WriteOnly) || lessonIndex.write(indexBytes) != indexBytes.size())
                    throw std::runtime_error("Cannot write lesson range fixture index");
                lessonIndex.close();
                const auto lessons = loadSingingLessons(lessonDirectory.path(), "en_US");
                check("Lesson loader preserves 64/1 and long durations when inferring measures",
                      lessons.size() == 1 && lessons.front().score.bpm == 420 &&
                          lessons.front().score.beatsPerBar == 64 && lessons.front().score.beatUnit == 1 &&
                          lessons.front().score.notes.size() == 3 &&
                          lessons.front().score.notes[0].durationTicks == 65 * TicksPerQuarter &&
                          lessons.front().score.notes[1].durationTicks == 191 * TicksPerQuarter &&
                          lessons.front().score.notes[0].measure == 0 &&
                          lessons.front().score.notes[1].measure == 0 &&
                          lessons.front().score.notes[2].measure == 1);
            }
            catch (const std::exception &error)
            {
                check(QString::fromUtf8(error.what()), false);
            }
            QFile report(args.value("report"));
            const auto bytes = QJsonDocument(QJsonObject{{"passed", passed}, {"checks", checks}}).toJson();
            if (!QDir().mkpath(QFileInfo(report.fileName()).absolutePath()) ||
                !report.open(QIODevice::WriteOnly) || report.write(bytes) != bytes.size())
                passed = false;
            app.exit(passed ? 0 : 3);
        });
}

class OptionsProbe final : public QObject
{
  public:
    OptionsProbe(MainWindow &window, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), args_(args), app_(app),
          folder_(QFileInfo(args.value("report")).absolutePath())
    {
        connect(
            &server_, &QTcpServer::newConnection, this,
            [this]
            {
                ++connections_;
                auto *socket = server_.nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(
                    socket, &QTcpSocket::readyRead, this,
                    [socket, bytes]
                    {
                        bytes->append(socket->readAll());
                        const int headers = bytes->indexOf("\r\n\r\n");
                        if (headers < 0 || socket->property("responded").toBool())
                            return;
                        const QRegularExpression length("Content-Length: ([0-9]+)",
                                                        QRegularExpression::CaseInsensitiveOption);
                        const auto match = length.match(QString::fromLatin1(bytes->left(headers)));
                        if (!match.hasMatch() || bytes->size() < headers + 4 + match.captured(1).toInt())
                            return;
                        socket->setProperty("responded", true);
                        socket->write(
                            "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                    });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            });
        server_.listen(QHostAddress::LocalHost, 0);
        QTimer::singleShot(100, this, [this] { run(); });
    }

  private:
    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ &= passed;
    }
    template <typename T> T *field(QDialog &dialog, const char *name)
    {
        auto *control = dialog.findChild<T *>(name);
        if (!control)
            throw std::runtime_error(QString("Missing options field %1").arg(name).toStdString());
        return control;
    }
    void edit(const char *actionName, const std::function<void(QDialog &)> &callback)
    {
        auto *action = window_.findChild<QAction *>(actionName);
        if (!action)
            throw std::runtime_error("Missing menu action");
        QObject lifetime;
        QTimer::singleShot(100, &lifetime,
                           [&]
                           {
                               auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                               if (!dialog || dialog->objectName() != "applicationOptions")
                               {
                                   check("Options modal opened", false);
                                   return;
                               }
                               try
                               {
                                   callback(*dialog);
                               }
                               catch (const std::exception &error)
                               {
                                   check(QString::fromUtf8(error.what()), false);
                               }
                               if (dialog->isVisible())
                                   dialog->reject();
                           });
        action->trigger();
    }
    void checkAbout()
    {
        QObject lifetime;
        QTimer::singleShot(
            100, &lifetime,
            [this]
            {
                auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "applicationAbout")
                {
                    check("About dialog opens through Help", false);
                    if (dialog)
                        dialog->reject();
                    return;
                }
                const QString text = dialog->text();
                check("About introduces singing features and version",
                      text.contains(QApplication::applicationVersion()) &&
                          (text.contains("learning to sing") || text.contains(QString::fromUtf8("唱歌学习"))));
                check("About includes author and current copyright year",
                      text.contains("Herbert Yeung") && text.contains("Copyright") &&
                          text.contains(QChar(0x00a9)) &&
                          text.contains(QString::number(QDate::currentDate().year())) &&
                          (text.contains("Author:") || text.contains(QString::fromUtf8("作者："))));
                check("About omits program entry and unresolved placeholders",
                      dialog->textFormat() == Qt::PlainText &&
                          !text.contains(QApplication::applicationFilePath()) && !text.contains("Program entry") &&
                          !text.contains(QString::fromUtf8("程序入口")) && !text.contains("%1") &&
                          !text.contains("%2"));
                dialog->grab().save(folder_ + "/about.png");
                dialog->accept();
            });
        auto *action = window_.findChild<QAction *>("menuAbout");
        if (!action)
            throw std::runtime_error("Missing About action");
        action->trigger();
    }
    void run()
    {
        try
        {
            checkAbout();
            const auto helpActions = window_.findChild<QMenu *>("helpMenu")->actions();
            check("Help contains only About, with no program directory action",
                  helpActions.size() == 1 && helpActions.front()->objectName() == "menuAbout" &&
                      !window_.findChild<QAction *>("menuProgramFolder"));
            check("Audio import has one menu entry", window_.findChild<QAction *>("menuImportAudio") &&
                                                         !window_.findChild<QAction *>("menuAudioAnalysis"));
            check("Stable release entry name",
                  QFileInfo(QApplication::applicationFilePath()).fileName() == "SingLilt.exe");
            for (const char *name :
                 {"fileMenu", "editMenu", "practiceMenuBar", "aiMenu", "optionsMenu", "helpMenu"})
                check(QString("Visible menu %1").arg(name), window_.findChild<QMenu *>(name) != nullptr);
            check("Legacy settings no longer occupy main window",
                  !window_.findChild<QWidget *>("legacyScoreSettings")->isVisible() &&
                      !window_.findChild<QWidget *>("legacyAudioSettings")->isVisible() &&
                      !window_.findChild<QComboBox *>("audioBackend")->isVisible());
            check("Frequent controls visible on playback bar",
                  window_.findChild<QCheckBox *>("metronome")->isVisible() &&
                      window_.findChild<QComboBox *>("programA")->isVisible() &&
                      window_.findChild<QComboBox *>("programB")->isVisible());
            window_.grab().save(folder_ + "/main-window.png");
            auto precise = window_.project();
            precise.score.bpm = 58.333333;
            precise.practiceMix.melodyVolume = 0.88885;
            window_.setProject(std::move(precise));
            window_.findChild<QComboBox *>("programA")
                ->setCurrentIndex(window_.findChild<QComboBox *>("programA")->findData(4));
            window_.findChild<QComboBox *>("programB")
                ->setCurrentIndex(window_.findChild<QComboBox *>("programB")->findData(0));
            window_.findChild<QCheckBox *>("metronome")->setChecked(true);
            const auto before = scoreToJson(window_.project().score);
            const auto oldEndpoint = QSettings().value("vision/endpoint");
            const bool dirty = window_.hasUnsavedChanges();
            edit("menuOptions",
                 [&](QDialog &dialog)
                 {
                     check("Playback bar and options share current timbres",
                           field<QComboBox>(dialog, "optionsCurrentProgramA")->currentData().toInt() == 4 &&
                               field<QComboBox>(dialog, "optionsCurrentProgramB")->currentData().toInt() == 0 &&
                               field<QCheckBox>(dialog, "optionsMetronome")->isChecked());
                     check("Display marker preference has an initialized control",
                           field<QCheckBox>(dialog, "optionsMarkers") != nullptr);
                     check("Unified options has five scopes",
                           field<QTabWidget>(dialog, "optionsPages")->count() == 5);
                     check("API key is password masked",
                           field<QLineEdit>(dialog, "optionsVisionKey")->echoMode() == QLineEdit::Password);
                     field<QLineEdit>(dialog, "optionsVisionEndpoint")->setText("http://127.0.0.1:1/v1");
                     field<QDoubleSpinBox>(dialog, "optionsCurrentTempo")->setValue(123);
                     dialog.reject();
                 });
            check("Cancel preserves configuration", QSettings().value("vision/endpoint") == oldEndpoint);
            check("Cancel preserves score and dirty state",
                  scoreToJson(window_.project().score) == before && dirty == window_.hasUnsavedChanges());
            edit(
                "menuAiSettings",
                [&](QDialog &dialog)
                {
                    check("AI menu opens AI options page",
                          field<QTabWidget>(dialog, "optionsPages")->currentIndex() == 3);
                    field<QLineEdit>(dialog, "optionsVisionEndpoint")
                        ->setText(QString("http://127.0.0.1:%1/v1").arg(server_.serverPort()));
                    field<QLineEdit>(dialog, "optionsVisionModel")->setText("fixture-model");
                    field<QLineEdit>(dialog, "optionsVisionKey")->setText("test-fixture-key");
                    field<QSpinBox>(dialog, "visionTimeout")->setValue(90);
                    field<QDoubleSpinBox>(dialog, "optionsTolerance")->setValue(35);
                    field<QSpinBox>(dialog, "optionsLatency")->setValue(80);
                    field<QDialogButtonBox>(dialog, "optionsButtons")->button(QDialogButtonBox::Apply)->click();
                    check("Apply keeps the options window open", dialog.isVisible());
                    field<QTabWidget>(dialog, "optionsPages")->setCurrentIndex(3);
                    field<QLineEdit>(dialog, "optionsVisionKey")->clear(); // No credential content in screenshots.
                    dialog.grab().save(folder_ + "/options-ai.png");
                    dialog.reject();
                });
            check("AI configuration saved without starting recognition",
                  !window_.cloudRecognitionTask().isRunning() && connections_ == 0);
            check("Global defaults do not replace loaded score",
                  scoreToJson(window_.project().score) == before && dirty == window_.hasUnsavedChanges());
            check("Unedited decimal values preserve precision",
                  window_.project().score.bpm == 58.333333 &&
                      window_.project().practiceMix.melodyVolume == 0.88885);
            check("Legacy preference keys remain readable",
                  QSettings().value("vision/model").toString() == "fixture-model" &&
                      QSettings().value("vision/timeoutSeconds").toInt() == 90);
            check("Microphone defaults persist",
                  QSettings().value("practice/centsTolerance").toDouble() == 35 &&
                      QSettings().value("practice/latencyMilliseconds").toInt() == 80);
            check("API key never persisted",
                  !QSettings().contains("vision/apiKey") && !QSettings().contains("vision/key"));
            edit("menuOptions",
                 [&](QDialog &dialog)
                 {
                     field<QLineEdit>(dialog, "optionsVisionEndpoint")->setText("invalid-url");
                     field<QDialogButtonBox>(dialog, "optionsButtons")->button(QDialogButtonBox::Ok)->click();
                     check("Invalid endpoint keeps options open", dialog.isVisible());
                     dialog.reject();
                 });
            check("Rejected values preserve saved model",
                  QSettings().value("vision/model").toString() == "fixture-model");
            edit("menuOptions",
                 [&](QDialog &dialog)
                 {
                     field<QTabWidget>(dialog, "optionsPages")->setCurrentIndex(4);
                     field<QDoubleSpinBox>(dialog, "optionsCurrentTempo")->setValue(92);
                     field<QSpinBox>(dialog, "optionsCurrentTranspose")->setValue(2);
                     field<QDialogButtonBox>(dialog, "optionsButtons")->button(QDialogButtonBox::Ok)->click();
                 });
            check("Current score changes only on commit", window_.project().score.bpm == 92 &&
                                                              window_.hasUnsavedChanges() &&
                                                              window_.player().transpose() == 2);
            const QString projectPath = folder_ + "/settings-score.jpp";
            saveProject(projectPath, window_.project());
            check("Current score settings survive project reopening", loadProject(projectPath).score.bpm == 92);
            QProcess restart;
            restart.start(QApplication::applicationFilePath(),
                          {"--options-read-check", "--report", folder_ + "/restart.json"});
            check("Preferences checked in a fresh process",
                  restart.waitForFinished(10000) && restart.exitCode() == 0);
            QFile fresh(folder_ + "/restart.json");
            fresh.open(QIODevice::ReadOnly);
            const auto reboot = QJsonDocument::fromJson(fresh.readAll()).object();
            check("Preferences survive process restart", reboot.value("model").toString() == "fixture-model" &&
                                                             reboot.value("timeout").toInt() == 90 &&
                                                             reboot.value("tolerance").toDouble() == 35);
            savedConnections_ = connections_;
            window_.findChild<QAction *>("menuRecognize")->trigger();
            check("Explicit AI menu starts a request", window_.cloudRecognitionTask().isRunning());
            window_.findChild<QAction *>("menuClassroom")->trigger();
            QTimer::singleShot(
                150, this,
                [this]
                {
                    try
                    {
                        ClassroomDialog *classroom = nullptr;
                        for (auto *widget : window_.findChildren<QDialog *>("singingClassroom"))
                            classroom = dynamic_cast<ClassroomDialog *>(widget);
                        check("Classroom starts through menu without CMD", classroom && classroom->isVisible());
                        if (classroom)
                        {
                            check("Classroom uses saved microphone preferences",
                                  classroom->findChild<QSpinBox *>("classroomLatency")->value() == 80 &&
                                      classroom->findChild<QDoubleSpinBox *>("classroomTolerance")->value() == 35);
                            check("Classroom settings are consolidated",
                                  !classroom->findChild<QWidget *>("legacyClassroomSettings")->isVisible());
                            classroom->grab().save(folder_ + "/classroom-menu.png");
                            classroom->close();
                        }
                        check("Only explicit AI action made a connection", connections_ == 1);
                        finish();
                    }
                    catch (const std::exception &error)
                    {
                        check(QString::fromUtf8(error.what()), false);
                        finish();
                    }
                });
        }
        catch (const std::exception &error)
        {
            check(QString::fromUtf8(error.what()), false);
            finish();
        }
    }
    void finish()
    {
        QFile file(args_.value("report"));
        file.open(QIODevice::WriteOnly);
        file.write(QJsonDocument(QJsonObject{{"passed", passed_},
                                             {"checks", checks_},
                                             {"networkConnectionsOnSave", savedConnections_},
                                             {"explicitAiConnections", connections_}})
                       .toJson());
        app_.exit(passed_ ? 0 : 2);
    }
    MainWindow &window_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QString folder_;
    QTcpServer server_;
    int connections_ = 0, savedConnections_ = 0;
    QJsonArray checks_;
    bool passed_ = true;
};
} // namespace
void runOptionsCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    if (args.isSet("options-range-only"))
    {
        runOptionsRangeCheck(window, args, app);
        return;
    }
    new OptionsProbe(window, args, app);
}
} // namespace singlilt

// Workspace identity, editing modes, and draft-state checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ProductWorkspaceCheck.h"
#include "domain/NumberedPerformance.h"
#include "i18n/LanguageManager.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ScoreView.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <functional>
#include <stdexcept>

namespace singlilt
{
namespace
{
class WorkspaceProbe final : public QObject
{
  public:
    WorkspaceProbe(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                   QApplication &app)
        : QObject(&window), window_(window), languages_(languages), args_(args), app_(app),
          folder_(QFileInfo(args.value("report")).absolutePath() + "/run-" +
                  QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        QTimer::singleShot(100, this, [this] { run(); });
    }

  private:
    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        passed_ &= passed;
    }
    template <typename T> T *control(const char *name)
    {
        auto *result = window_.findChild<T *>(name);
        if (!result)
            throw std::runtime_error(QString("Missing workspace control %1").arg(name).toStdString());
        return result;
    }
    void action(const char *name)
    {
        control<QAction>(name)->trigger();
    }
    void modal(const char *name, const std::function<void(QDialog *)> &edit)
    {
        QTimer::singleShot(0, this,
                           [this, edit]
                           {
                               auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                               if (!dialog)
                               {
                                   check("Expected modal editor", false);
                                   return;
                               }
                               edit(dialog);
                           });
        action(name);
    }
    void saveAs(const QString &path)
    {
        modal("menuSaveAs",
              [this, path](QDialog *dialog)
              {
                  auto *file = qobject_cast<QFileDialog *>(dialog);
                  check("Save uses project file dialog", file != nullptr);
                  if (!file)
                  {
                      dialog->reject();
                      return;
                  }
                  file->setDirectory(QFileInfo(path).absolutePath());
                  file->selectFile(path);
                  if (auto *input = file->findChild<QLineEdit *>("fileNameEdit"))
                      input->setText(path);
                  QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
              });
    }
    void screenshot(const QString &name)
    {
        QApplication::processEvents();
        const QString path = folder_ + "/" + name + ".png";
        check(name + " screenshot saved", window_.grab().save(path));
        screenshots_.append(path);
        const QRect bounds = window_.centralWidget()->rect();
        for (const auto *controlName :
             {"workspaceMode", "practiceModeButton", "correctionModeButton", "songPracticeTask", "classroomTask",
              "earTrainingTask", "nextUncertainNote", "practiceSpeed", "transpose", "playbackSource",
              "outputVolume", "melodyEnabled", "accompanimentEnabled", "accompanimentVolume", "metronome",
              "programA", "programB", "practiceLoop"})
        {
            auto *widget = control<QWidget>(controlName);
            const QRect rectangle(widget->mapTo(window_.centralWidget(), QPoint()), widget->size());
            check(QString::fromLatin1(controlName) + " fits " + name_,
                  widget->isVisible() && bounds.contains(rectangle));
        }
        check("Window stays at 980 x 700 " + name_, window_.size() == QSize(980, 700));
        for (const auto *controlName : {"practiceModeButton", "correctionModeButton"})
        {
            auto *mode = control<QPushButton>(controlName);
            const int iconWidth = mode->icon().isNull() ? 0 : mode->iconSize().width() + 4;
            check(QString::fromLatin1(controlName) + " label is not clipped " + name_,
                  mode->width() >= mode->fontMetrics().horizontalAdvance(mode->text()) + iconWidth + 12);
        }
        auto *view = static_cast<ScoreView *>(control<QWidget>("scoreView"));
        check("Mode change fits score width " + name_, view->horizontalScrollBar()->maximum() == 0);
    }
    void checkLoopRestoration(Project sample)
    {
        sample.score.repeats.clear();
        for (auto &note : sample.score.notes)
            note.confidence = 1;
        sample.score.notes.back().confidence = .5;
        ProjectPracticeSettings practice;
        practice.loopEnabled = true;
        practice.loopEnd = buildTimeline(sample.score).durationTicks;
        sample.practiceSettings = practice;
        window_.setProject(sample);
        saveAs(folder_ + "/loop-restoration.jpp");
        const auto saved = window_.currentProjectPractice();
        check("Saved full-score loop is clean", saved.loopEnabled &&
                                                    saved.loopEnd == window_.timeline().durationTicks &&
                                                    !window_.hasUnsavedChanges());
        control<QPushButton>("nextUncertainNote")->click();
        control<QPushButton>("removeNote")->click();
        check("Deleting last note clamps effective loop",
              window_.currentProjectPractice().loopEnd < saved.loopEnd);
        action("menuUndo");
        const auto restored = window_.currentProjectPractice();
        check("Undo restores saved loop boundaries and clean state",
              restored.loopStart == saved.loopStart && restored.loopEnd == saved.loopEnd &&
                  restored.loopEnabled == saved.loopEnabled && !window_.hasUnsavedChanges());
        action("menuRedo");
        check("Redo reclamps loop after material deletion",
              window_.currentProjectPractice().loopEnd < saved.loopEnd);
        window_.player().seek(480);
        control<QPushButton>("practiceLoopStart")->click();
        window_.player().seek(960);
        control<QPushButton>("practiceLoopEnd")->click();
        const auto edited = window_.currentProjectPractice();
        action("menuUndo");
        const auto kept = window_.currentProjectPractice();
        check("Undo retains newer explicit loop boundaries",
              kept.loopStart == edited.loopStart && kept.loopEnd == edited.loopEnd && kept.loopStart == 480 &&
                  kept.loopEnd == 960 && window_.hasUnsavedChanges());
    }
    void checkOriginalVolume(Project sample)
    {
        const QString path = folder_ + "/original-volume.wav";
        const QByteArray pcm(16000 * 2, '\0');
        QByteArray wave;
        QDataStream stream(&wave, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4);
        stream << quint32(pcm.size() + 36);
        stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(16000) << quint32(32000) << quint16(2)
               << quint16(16);
        stream.writeRawData("data", 4);
        stream << quint32(pcm.size());
        wave += pcm;
        QFile audio(path);
        if (!audio.open(QIODevice::WriteOnly) || audio.write(wave) != wave.size())
            throw std::runtime_error("Original-volume PCM fixture write failed");
        audio.close();
        AudioSourceInfo source;
        source.path = path.toStdString();
        source.durationSeconds = 1;
        source.selectedEndSeconds = 1;
        sample.audioSource = source;
        sample.practiceSettings.reset();
        sample.practiceMix.melodyVolume = .9;
        window_.setProject(sample);
        auto *playback = control<QComboBox>("playbackSource");
        playback->setCurrentIndex(playback->findData(1));
        check("Original-volume fixture opens through the real source control",
              playback->currentData().toInt() == 1 && window_.originalAudioPlayer().isOpen());
        control<QSlider>("outputVolume")->setValue(40);
        check("Original volume is independent of synthesized melody",
              window_.currentProjectPractice().originalVolume == .4 &&
                  window_.project().practiceMix.melodyVolume == .9);
        auto *accompaniment = control<QCheckBox>("accompanimentEnabled");
        accompaniment->setChecked(!accompaniment->isChecked());
        control<QSlider>("accompanimentVolume")->setValue(35);
        check("Original-mode accompaniment controls preserve melody volume",
              window_.project().practiceMix.melodyVolume == .9 &&
                  window_.player().practiceMix().melodyVolume == .9 &&
                  window_.currentProjectPractice().originalVolume == .4);
        auto *melody = control<QCheckBox>("melodyEnabled");
        melody->setChecked(!melody->isChecked());
        check("Original-mode melody toggle preserves synthesized gain",
              window_.project().practiceMix.melodyVolume == .9);
        playback->setCurrentIndex(playback->findData(0));
        check("Returning to synthesized source restores its own volume",
              control<QSlider>("outputVolume")->value() == 90 && window_.project().practiceMix.melodyVolume == .9);
        window_.setProject(sample);
    }
    void run()
    {
        try
        {
            QDir().mkpath(folder_);
            action("menuIntro");
            check("Public practice example opens without a private runtime asset",
                  !window_.project().score.notes.empty() && window_.project().generatedNotation &&
                      !window_.project().image.isNull());
            Project sample = makePracticeScore();
            sample.score.notes.resize(8);
            sample.score.repeats = {{1, 4, 2, -1}, {5, 8, 2, -1}};
            sample.score.notes[0].confidence = .5;
            sample.score.notes[2].confidence = .5;
            sample.accompaniment = generateAccompaniment(sample.score, {});
            window_.setProject(sample);
            const auto original = scoreToJson(window_.project().score);
            check("SingLilt display brand and window icon are installed",
                  QApplication::applicationDisplayName().contains("SingLilt") && !window_.windowIcon().isNull());
            window_.resize(980, 700);
            auto *practiceMode = control<QPushButton>("practiceModeButton");
            auto *correctionMode = control<QPushButton>("correctionModeButton");
            check("Practice is default and inspector is hidden",
                  practiceMode->isChecked() && !correctionMode->isChecked() &&
                      !window_.findChild<QComboBox *>("workspaceMode") &&
                      !control<QWidget>("inspectorScroll")->isVisible());
            check("Three task entries are present", control<QPushButton>("songPracticeTask")->isVisible() &&
                                                        control<QPushButton>("classroomTask")->isVisible() &&
                                                        control<QPushButton>("earTrainingTask")->isVisible());
            correctionMode->click();
            check("One-click correction reveals inspector and synchronizes the mode buttons",
                  correctionMode->isChecked() && !practiceMode->isChecked() &&
                      control<QWidget>("inspectorScroll")->isVisible());
            control<QComboBox>("noteDegree")->setCurrentIndex(3);
            control<QPushButton>("applyNoteChanges")->click();
            check("Note edit marks dirty and enables undo",
                  window_.hasUnsavedChanges() && control<QAction>("menuUndo")->isEnabled());
            check("Material edit makes accompaniment stale",
                  !control<QCheckBox>("accompanimentEnabled")->isEnabled());
            action("menuUndo");
            check("Undo restores note and clean project",
                  scoreToJson(window_.project().score) == original && !window_.hasUnsavedChanges());
            check("Undo restores confirmed accompaniment eligibility",
                  control<QCheckBox>("accompanimentEnabled")->isEnabled());
            action("menuRedo");
            check("Redo reapplies note and dirty state",
                  window_.project().score.notes[0].degree == 3 && window_.hasUnsavedChanges());
            control<QPushButton>("removeNote")->click();
            const auto &repeats = window_.project().score.repeats;
            check("Deleting before repeats preserves and shifts both",
                  repeats.size() == 2 && repeats[0].firstNote == 0 && repeats[0].endNote == 3 &&
                      repeats[1].firstNote == 4 && repeats[1].endNote == 7);
            action("menuUndo");
            check("Undo delete restores all notes and repeats",
                  window_.project().score.notes.size() == 8 && window_.project().score.repeats[1].firstNote == 5);
            auto *view = static_cast<ScoreView *>(control<QWidget>("scoreView"));
            view->emptyDoubleClicked(QPointF(40, 40));
            check("Add note preserves later repeat indexes",
                  window_.project().score.notes.size() == 9 && window_.project().score.repeats[1].firstNote == 6);
            action("menuUndo");
            check("Undo add restores notes", window_.project().score.notes.size() == 8);
            const QString saved = folder_ + "/workspace-edited.jpp";
            saveAs(saved);
            check("Saving sets undo clean and displays filename",
                  QFileInfo::exists(saved) && !window_.hasUnsavedChanges() &&
                      control<QLabel>("projectIdentity")->text() == "workspace-edited.jpp");
            action("menuUndo");
            check("Undo across saved boundary is dirty", window_.hasUnsavedChanges());
            action("menuRedo");
            check("Redo to saved boundary is clean", !window_.hasUnsavedChanges());
            control<QDoubleSpinBox>("practiceSpeed")->setValue(.5);
            control<QSpinBox>("transpose")->setValue(2);
            check("Direct practice speed and transpose reach player",
                  window_.player().speed() == .5 && window_.player().transpose() == 2);
            control<QComboBox>("noteDegree")->setCurrentIndex(4);
            control<QPushButton>("applyNoteChanges")->click();
            action("menuUndo");
            check("Material undo preserves newer practice settings", window_.player().speed() == .5 &&
                                                                         window_.player().transpose() == 2 &&
                                                                         window_.hasUnsavedChanges());
            action("menuSave");
            const auto savedSettings = loadProject(saved).practiceSettings;
            check("Direct practice settings persist",
                  savedSettings && savedSettings->speed == .5 && savedSettings->transpose == 2);
            modal("menuLyrics",
                  [](QDialog *dialog)
                  {
                      dialog->findChild<QPlainTextEdit *>("songLyricsText")->setPlainText("do re mi");
                      dialog->accept();
                  });
            check("Lyrics edit is undoable", window_.hasUnsavedChanges());
            action("menuUndo");
            check("Lyrics undo returns to saved material", !window_.hasUnsavedChanges());
            modal("menuRepeats",
                  [](QDialog *dialog)
                  {
                      auto *table = dialog->findChild<QTableWidget *>();
                      table->item(0, 2)->setText("3");
                      dialog->accept();
                  });
            check("Repeat edit is undoable", window_.project().score.repeats[0].count == 3);
            action("menuUndo");
            check("Repeat undo restores clean material",
                  window_.project().score.repeats[0].count == 2 && !window_.hasUnsavedChanges());
            control<QLineEdit>("lyricAEditor")->setText("unapplied draft");
            QTimer::singleShot(0, this,
                               []
                               {
                                   if (auto *dialog =
                                           qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                                       dialog->button(QMessageBox::Cancel)->click();
                               });
            practiceMode->click();
            check("Cancel mode switch retains unapplied draft",
                  correctionMode->isChecked() && !practiceMode->isChecked() &&
                      control<QWidget>("inspectorScroll")->isVisible() &&
                      control<QLineEdit>("lyricAEditor")->text() == "unapplied draft");
            QTimer::singleShot(0, this,
                               []
                               {
                                   if (auto *dialog =
                                           qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                                       dialog->button(QMessageBox::Discard)->click();
                               });
            practiceMode->click();
            check("Explicit discard switches to practice",
                  practiceMode->isChecked() && !correctionMode->isChecked() &&
                      !control<QWidget>("inspectorScroll")->isVisible() && !window_.hasUnsavedChanges());
            control<QPushButton>("nextUncertainNote")->click();
            check("Next review selects uncertain note in correction",
                  correctionMode->isChecked() && !practiceMode->isChecked() && view->currentNoteIndex() == 2);
            auto recent = QSettings().value("projects/recent").toStringList();
            check("Successful save is remembered", recent.contains(QFileInfo(saved).absoluteFilePath()));
            const QString missing = folder_ + "/missing.jpp";
            recent.prepend(missing);
            QSettings().setValue("projects/recent", recent);
            languages_.setLanguage("en_US");
            QApplication::processEvents();
            auto *recentMenu = control<QMenu>("recentProjectsMenu");
            recentMenu->popup(window_.mapToGlobal(QPoint(20, 40)));
            QApplication::processEvents();
            for (auto *item : recentMenu->actions())
                if (item->data().toString() == missing)
                {
                    item->trigger();
                    break;
                }
            recentMenu->hide();
            check("Missing recent file is removed without losing project",
                  !QSettings().value("projects/recent").toStringList().contains(missing) &&
                      control<QLabel>("projectIdentity")->text() == "workspace-edited.jpp");
            for (const auto &locale : {QStringLiteral("en_US"), QStringLiteral("zh_CN")})
            {
                languages_.setLanguage(locale);
                QApplication::processEvents();
                practiceMode->click();
                name_ = locale + " practice";
                screenshot(locale + "-practice-980x700");
                correctionMode->click();
                name_ = locale + " correction";
                screenshot(locale + "-correction-980x700");
            }
            Project braced = sample;
            braced.image = QImage(420, 280, QImage::Format_RGB32);
            braced.image.fill(Qt::white);
            braced.generatedNotation = false;
            Note upper;
            upper.source = {120, 120, 18, 24};
            Note next = upper;
            next.degree = 4;
            next.source.x = 180;
            Note lower = upper;
            lower.degree = 3;
            lower.octave = -1;
            lower.line = 1;
            lower.durationTicks = 960;
            lower.source.y = 210;
            braced.score.notes = {upper, next, lower};
            braced.score.repeats.clear();
            braced.staffPerformance = buildNumberedPerformance(braced.score, {{0, 1, {}}});
            braced.practiceMix.accompanimentEnabled = true;
            window_.setProject(braced);
            correctionMode->click();
            check("Braced numbered guide keeps its correction fields enabled",
                  control<QComboBox>("noteDegree")->isEnabled() &&
                      control<QPushButton>("applyNoteChanges")->isEnabled());
            check("Numbered timed-column removal explains the alignment constraint",
                  !control<QPushButton>("removeNote")->isEnabled() &&
                      !control<QPushButton>("removeNote")->toolTip().isEmpty());
            auto *bracedView = control<QGraphicsView>("scoreView");
            const auto clickGuide = [&](int index)
            {
                const auto &box = window_.project().score.notes[std::size_t(index)].source;
                const auto point =
                    bracedView->mapFromScene(QPointF(box.x + box.width / 2, box.y + box.height / 2));
                const auto global = bracedView->viewport()->mapToGlobal(point);
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), QPointF(global), Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), QPointF(global), Qt::LeftButton,
                                    Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(bracedView->viewport(), &press);
                QApplication::sendEvent(bracedView->viewport(), &release);
            };
            clickGuide(1);
            check("Braced performance hit opens the matching numbered guide inspector",
                  control<QComboBox>("noteDegree")->currentData().toInt() == 4 &&
                      control<QPushButton>("applyNoteChanges")->isEnabled());
            clickGuide(0);
            control<QComboBox>("noteDegree")->setCurrentIndex(4);
            control<QDoubleSpinBox>("noteDuration")->setValue(.5);
            control<QLineEdit>("lyricAEditor")->setText("corrected numbered lyric");
            control<QCheckBox>("noteTie")->setChecked(true);
            control<QPushButton>("applyNoteChanges")->click();
            const auto numberedEdit = window_.project();
            const auto editedParts = staffPerformanceToJson(*numberedEdit.staffPerformance);
            check("Numbered inspector synchronizes pitch, duration, lyrics, ties and both-hand timing",
                  numberedEdit.score.notes[0].degree == 4 && numberedEdit.score.notes[0].durationTicks == 240 &&
                      numberedEdit.score.notes[0].lyric == "corrected numbered lyric" &&
                      numberedEdit.staffPerformance->notes[0].tieStart &&
                      numberedEdit.staffPerformance->notes[1].tieStop &&
                      numberedEdit.staffPerformance->notes[2].durationTicks == 960 &&
                      buildStaffPerformancePlan(numberedEdit.score, buildTimeline(numberedEdit.score),
                                                *numberedEdit.staffPerformance)
                          .valid());
            action("menuUndo");
            check("Numbered correction Undo restores the complete original performance",
                  window_.project().score.notes[0].degree == 1 &&
                      staffPerformanceToJson(*window_.project().staffPerformance) ==
                          staffPerformanceToJson(*braced.staffPerformance));
            action("menuRedo");
            check("Numbered correction Redo restores synchronized performance",
                  staffPerformanceToJson(*window_.project().staffPerformance) == editedParts);
            const QString numberedPath = folder_ + "/numbered-corrected.jpp";
            saveProject(numberedPath, window_.project());
            const auto reopenedNumbered = loadProject(numberedPath);
            check("Saved numbered correction retains the guide, both hands and numbered view",
                  reopenedNumbered.notationStyle == NotationStyle::Numbered &&
                      reopenedNumbered.score.notes[0].lyric == "corrected numbered lyric" &&
                      staffPerformanceToJson(*reopenedNumbered.staffPerformance) == editedParts);
            control<QComboBox>("programA")->setCurrentIndex(control<QComboBox>("programA")->findData(40));
            practiceMode->click();
            const auto auditions = window_.player().voiceState().previewNoteOns;
            clickGuide(1);
            QEventLoop previewLoop;
            QTimer previewPoll;
            QObject::connect(&previewPoll, &QTimer::timeout, &previewLoop,
                             [&]
                             {
                                 if (window_.player().voiceState().previewNoteOns > auditions)
                                     previewLoop.quit();
                             });
            QTimer::singleShot(2000, &previewLoop, &QEventLoop::quit);
            previewPoll.start(10);
            previewLoop.exec();
            const auto preview = window_.player().voiceState();
            check("Paused numbered-part audition uses the selected primary instrument and performed velocity",
                  preview.previewNoteOns > auditions && preview.previewProgram == 40 &&
                      preview.previewVelocity == window_.project().staffPerformance->notes[1].velocity &&
                      control<QComboBox>("noteDegree")->currentData().toInt() == 4);
            window_.setProject(sample);
            check("Replacing project clears history",
                  !control<QAction>("menuUndo")->isEnabled() && !control<QAction>("menuRedo")->isEnabled());
            checkLoopRestoration(sample);
            checkOriginalVolume(sample);
        }
        catch (const std::exception &error)
        {
            check(QString::fromUtf8(error.what()), false);
        }
        QFile report(args_.value("report"));
        if (!report.open(QIODevice::WriteOnly))
        {
            app_.exit(4);
            return;
        }
        report.write(
            QJsonDocument(QJsonObject{{"passed", passed_}, {"checks", checks_}, {"screenshots", screenshots_}})
                .toJson());
        app_.exit(passed_ ? 0 : 2);
    }
    MainWindow &window_;
    LanguageManager &languages_;
    const QCommandLineParser &args_;
    QApplication &app_;
    QString folder_, name_;
    QJsonArray checks_, screenshots_;
    bool passed_ = true;
};
} // namespace
void runProductWorkspaceCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                              QApplication &app)
{
    new WorkspaceProbe(window, languages, args, app);
}
} // namespace singlilt

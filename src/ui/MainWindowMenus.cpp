// Workspace menus, actions, and shortcuts.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ClassroomDialog.h"
#include "MainWindow.h"
#include "MenuIcons.h"
#include "OptionsDialog.h"
#include "ScoreView.h"
#include "ThemeManager.h"
#include "audio/WaveRenderer.h"
#include "i18n/LanguageManager.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
void MainWindow::createMenus()
{
    const auto menu = [this](const char *key, const char *name)
    {
        auto *result = menuBar()->addMenu(trText(key));
        result->setObjectName(name);
        bindText(result, key, "title");
        return result;
    };
    const auto action = [this](QMenu *menu, const char *key, const char *name, MenuIcon icon, const auto &callback)
    {
        auto *result = menu->addAction(trText(key));
        result->setObjectName(name);
        result->setIcon(menuIcon(icon));
        result->setIconVisibleInMenu(true);
        bindText(result, key);
        connect(result, &QAction::triggered, this, callback);
        return result;
    };
    const auto click = [this](const char *name)
    {
        if (busy_ || audioLoading_ || notePreviewLoading_)
            return;
        if (auto *button = findChild<QPushButton *>(name))
            button->click();
    };
    auto *file = menu("ui.menu.file", "fileMenu");
    action(file, "ui.menu.new_project", "menuNew", MenuIcon::NewProject, [this] { newProject(); })
        ->setShortcut(QKeySequence::New);
    action(file, "ui.menu.open_project", "menuOpenProject", MenuIcon::OpenProject,
           [this]
           {
               if (busy_ || audioLoading_ || notePreviewLoading_ || audioTask_.isRunning())
                   return;
               const auto path = chooseFile(false, true);
               if (!path.isEmpty())
                   openFile(path);
           })
        ->setShortcut(QKeySequence::Open);
    action(file, "ui.menu.open_score", "menuOpenScore", MenuIcon::ImportImage,
           [this]
           {
               const auto path = chooseFile(false);
               if (!path.isEmpty())
                   openFile(path);
           });
    action(file, "ui.audio_import.import", "menuImportAudio", MenuIcon::ImportAudio,
           [click] { click("importAudio"); });
    action(file, "ui.staff.import_xml", "menuImportMusicXml", MenuIcon::Score,
           [this]
           {
               const QString path = QFileDialog::getOpenFileName(this, trText("ui.staff.import_xml"), {},
                                                                 trText("ui.staff.xml_filter"));
               if (!path.isEmpty())
                   importMusicXml(path);
           });
    action(file, "ui.staff.import_image", "menuImportStaffImage", MenuIcon::ImportImage,
           [this]
           {
               const QStringList paths = QFileDialog::getOpenFileNames(this, trText("ui.staff.import_image"), {},
                                                                       trText("ui.staff.image_filter"));
               if (!paths.isEmpty())
                   importStaffImages(paths);
           });
    action(file, "ui.header.paste", "menuPaste", MenuIcon::Paste, [click] { click("pasteScore"); });
    file->addSeparator();
    action(file, "ui.button.save", "menuSave", MenuIcon::Save, [this] { save(); })
        ->setShortcut(QKeySequence::Save);
    action(file, "ui.menu.save_as", "menuSaveAs", MenuIcon::SaveAs, [this] { save(true); })
        ->setShortcut(QKeySequence::SaveAs);
    action(file, "ui.menu.export_wave", "menuExportWave", MenuIcon::ExportAudio, [this] { exportWaveFile(); });
    file->addSeparator();
    action(file, "ui.menu.exit", "menuExit", MenuIcon::Exit, [this] { close(); });
    recentProjects_ = file->addMenu(trText("ui.product.recent_projects"));
    recentProjects_->setObjectName("recentProjectsMenu");
    recentProjects_->setIcon(menuIcon(MenuIcon::Recent));
    recentProjects_->menuAction()->setIconVisibleInMenu(true);
    bindText(recentProjects_, "ui.product.recent_projects", "title");
    refreshRecentProjects();
    connect(recentProjects_, &QMenu::aboutToShow, this, &MainWindow::refreshRecentProjects);
    auto *edit = menu("ui.menu.edit", "editMenu");
    undoAction_ = action(edit, "ui.product.undo", "menuUndo", MenuIcon::Undo, [this] { undoMaterialEdit(); });
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = action(edit, "ui.product.redo", "menuRedo", MenuIcon::Redo, [this] { redoMaterialEdit(); });
    redoAction_->setShortcut(QKeySequence::Redo);
    edit->addSeparator();
    action(edit, "ui.staff_correction.open", "menuStaffCorrection", MenuIcon::Correction,
           [this] { openStaffCorrection(); });
    action(edit, "ui.original_playback.recover_edits", "menuRecoverStaffEdits", MenuIcon::OpenProject,
           [this] { recoverStaffEdits(); });
    action(edit, "ui.fidelity.confirm_tempo", "menuConfirmStaffTempo", MenuIcon::Calibration,
           [this] { confirmStaffTempo(); });
    action(edit, "ui.audio_import.edit_lyrics", "menuLyrics", MenuIcon::Lyrics, [this] { editLyrics(); });
    action(edit, "ui.menu.repeats", "menuRepeats", MenuIcon::Repeat, [this] { editRepeats(); });
    action(edit, "ui.option.fit", "menuFit", MenuIcon::FitWidth, [this] { displayedScoreView()->fitWidth(); });
    action(edit, "ui.staff.staff_view", "menuStaffView", MenuIcon::Score,
           [this] { setNotationStyle(NotationStyle::Staff); });
    action(edit, "ui.staff.numbered_view", "menuNumberedView", MenuIcon::Scale,
           [this] { setNotationStyle(NotationStyle::Numbered); });
    auto *practice = menu("ui.menu.practice", "practiceMenuBar");
    action(practice, "ui.product.practice_mode", "menuPracticeMode", MenuIcon::Practice,
           [this] { setCorrectionMode(false); });
    action(practice, "ui.product.correction_mode", "menuCorrectionMode", MenuIcon::Correction,
           [this] { setCorrectionMode(true); });
    action(practice, "ui.classroom.open", "menuClassroom", MenuIcon::Classroom, [this] { openClassroom(); });
    action(practice, "ui.menu.ear", "menuEarTraining", MenuIcon::EarTraining,
           [this]
           {
               openClassroom();
               if (classroom_)
                   classroom_->showEarTraining();
           });
    action(practice, "ui.accompaniment.generate", "menuGenerateAccompaniment", MenuIcon::Accompaniment,
           [this] { generatePracticeAccompaniment(); });
    auto *samples = practice->addMenu(trText("ui.header.samples"));
    samples->setObjectName("sampleScoresMenu");
    samples->setIcon(menuIcon(MenuIcon::Samples));
    samples->menuAction()->setIconVisibleInMenu(true);
    bindText(samples, "ui.header.samples", "title");
    action(samples, "ui.sample.scale", "menuScale", MenuIcon::Scale,
           [this]
           {
               if (!confirmDiscard())
                   return;
               try
               {
                   setProject(makePracticeScore());
               }
               catch (const std::exception &error)
               {
                   showError(QString::fromUtf8(error.what()));
               }
           });
    action(samples, "ui.sample.intro", "menuIntro", MenuIcon::Score, [this] { openPracticeExample(false); });
    action(samples, "ui.sample.original", "menuOriginal", MenuIcon::ImportImage,
           [this] { openPracticeExample(true); });
    auto *ai = menu("ui.menu.ai", "aiMenu");
    action(ai, "ui.menu.recognize", "menuRecognize", MenuIcon::Recognize, [this] { startCloudRecognition(); });
    action(ai, "ui.menu.ai_settings", "menuAiSettings", MenuIcon::Settings, [this] { openOptions(3); });
    auto *options = menu("ui.menu.options", "optionsMenu");
    action(options, "ui.options.title", "menuOptions", MenuIcon::Settings, [this] { openOptions(); });
    auto *help = menu("ui.menu.help", "helpMenu");
    action(help, "ui.menu.about", "menuAbout", MenuIcon::About,
           [this]
           {
               QMessageBox dialog(this);
               dialog.setObjectName("applicationAbout");
               dialog.setWindowTitle(trText("ui.menu.about"));
               dialog.setTextFormat(Qt::PlainText);
               dialog.setText(
                   trText("ui.options.about")
                       .arg(QApplication::applicationVersion(), QString::number(QDate::currentDate().year())));
               dialog.exec();
           });
}

OptionsContext MainWindow::optionsContext() const
{
    OptionsContext context;
    context.fullStaffPerformance = project_.staffPerformance.has_value();
    if (project_.staffPerformance)
    {
        context.primaryStaffProgram = project_.staffPerformance->primaryProgram;
        context.otherStaffProgram = project_.staffPerformance->otherProgram;
    }
    context.score = project_.score;
    context.mix = project_.practiceMix;
    context.transpose = transpose_->value();
    context.speed = player_.speed();
    context.playbackSource = playbackSource_->currentIndex();
    context.accompanimentPattern = accompanimentPattern_->currentIndex();
    context.originalSpeed = findChild<QDoubleSpinBox *>("originalSpeed")->value();
    context.originalVolume = findChild<QSlider *>("originalVolume")->value() / 100.0;
    return context;
}

void MainWindow::openOptions(int page)
{
    if (busy_ || audioLoading_ || notePreviewLoading_)
        return;
    settings_ = loadAppSettings();
    settings_.vision.apiKey = vision_.apiKey;
    settings_.language = languageManager_.language();
    settings_.audioBackend = player_.audioBackend() == AudioBackend::WindowsMidi ? 1 : 0;
    settings_.gmSoundFontPath = player_.gmSoundFontPath();
    settings_.metronome = metronome_->isChecked();
    OptionsDialog dialog(settings_, optionsContext(), this);
    dialog.selectPage(page);
    dialog.applyChanges = [this](const AppSettings &s, const OptionsContext &c) { return applyOptions(s, c); };
    dialog.exec();
}

bool MainWindow::applyOptions(const AppSettings &s, const OptionsContext &c)
{
    if (!c.classroom && project_.staffPerformance &&
        (c.score.tonic != project_.score.tonic || c.score.beatsPerBar != project_.score.beatsPerBar ||
         c.score.beatUnit != project_.score.beatUnit))
        throw SettingsValidationError("optionsCurrentKey", trText("ui.staff.guide_only"));
    if (!c.classroom && project_.staffPerformance)
    {
        if (c.primaryStaffProgram < 0 || c.primaryStaffProgram > 127)
            throw SettingsValidationError("optionsCurrentProgramA", trText("messages.domain.program_range"));
        if (c.otherStaffProgram < 0 || c.otherStaffProgram > 127)
            throw SettingsValidationError("optionsCurrentProgramB", trText("messages.domain.program_range"));
        const char *changedField = nullptr;
        if (c.score.versePrograms != project_.score.versePrograms)
            changedField = "optionsCurrentProgramA";
        else if (c.score.baseVelocity != project_.score.baseVelocity)
            changedField = "optionsCurrentVelocity";
        else if (c.score.accentBeats != project_.score.accentBeats)
            changedField = "optionsCurrentAccent";
        if (changedField)
            throw SettingsValidationError(changedField, trText("ui.staff.preserved_performance"));
    }
    validateAppSettings(s, &settings_);
    if (busy_ || audioLoading_ || notePreviewLoading_)
        throw std::runtime_error(trText("messages.options.busy").toStdString());
    if (!c.classroom && !c.score.notes.empty() && !buildTimeline(c.score, c.transpose).valid())
        throw std::runtime_error(trText("messages.options.score_invalid").toStdString());
    if (!c.classroom && c.playbackSource > 0)
    {
        if (!project_.audioSource || (c.playbackSource == 2 && project_.audioSource->vocalsPath.empty()) ||
            (c.playbackSource == 3 && project_.audioSource->instrumentalPath.empty()))
            throw std::runtime_error(trText("messages.options.source_missing").toStdString());
    }
    const auto old = settings_;
    const bool staffProgramsChanged = !c.classroom && project_.staffPerformance &&
                                      (c.primaryStaffProgram != project_.staffPerformance->primaryProgram ||
                                       c.otherStaffProgram != project_.staffPerformance->otherProgram);
    AccompanimentSettings oldStaffSettings;
    AccompanimentSettings newStaffSettings;
    if (staffProgramsChanged)
    {
        oldStaffSettings.originalStaff = newStaffSettings.originalStaff = true;
        oldStaffSettings.chordProgram = project_.staffPerformance->primaryProgram;
        oldStaffSettings.bassProgram = project_.staffPerformance->otherProgram;
        newStaffSettings.chordProgram = c.primaryStaffProgram;
        newStaffSettings.bassProgram = c.otherStaffProgram;
    }
    const auto backend = s.audioBackend == 1 ? AudioBackend::WindowsMidi : AudioBackend::SampledPiano;
    try
    {
        if (s.gmSoundFontPath != old.gmSoundFontPath && !player_.setGmSoundFontPath(s.gmSoundFontPath))
            throw SettingsValidationError("optionsGmPath", player_.errorString());
        if (!player_.setAudioBackend(backend))
            throw SettingsValidationError("optionsAudioBackend", player_.errorString());
        player_.setOutputBoost(s.outputBoost);
        if (staffProgramsChanged && !player_.setAccompanimentSettings(newStaffSettings))
            throw SettingsValidationError("optionsCurrentProgramA", player_.errorString());
        saveAppSettings(s, &old);
    }
    catch (...)
    {
        if (staffProgramsChanged)
            player_.setAccompanimentSettings(oldStaffSettings);
        if (s.gmSoundFontPath != old.gmSoundFontPath)
            player_.setGmSoundFontPath(old.gmSoundFontPath);
        player_.setAudioBackend(old.audioBackend == 1 ? AudioBackend::WindowsMidi : AudioBackend::SampledPiano);
        player_.setOutputBoost(old.outputBoost);
        throw;
    }
    settings_ = s;
    themes_.setMode(s.themeMode);
    vision_ = s.vision;
    view_->showUncertain(s.showMarkers);
    {
        const QSignalBlocker backendBlock(audioSource_);
        audioSource_->setCurrentIndex(s.audioBackend);
    }
    metronome_->setChecked(s.metronome);
    if (!c.classroom)
    {
        const bool scoreChanged = scoreToJson(project_.score) != scoreToJson(c.score);
        const bool mixChanged = project_.practiceMix.melodyEnabled != c.mix.melodyEnabled ||
                                project_.practiceMix.accompanimentEnabled != c.mix.accompanimentEnabled ||
                                project_.practiceMix.melodyVolume != c.mix.melodyVolume ||
                                project_.practiceMix.accompanimentVolume != c.mix.accompanimentVolume;
        loading_ = true;
        if (project_.staffPerformance && c.score.bpm != project_.score.bpm &&
            project_.processing.value("tempoNeedsConfirmation").toBool())
        {
            project_.processing.insert("tempoNeedsConfirmation", false);
            project_.processing.insert("tempoSource", "options-user-confirmed");
            project_.processing.insert("confirmedQuarterBpm", c.score.bpm);
        }
        project_.score = c.score;
        if (project_.staffPerformance)
        {
            project_.staffPerformance->primaryProgram = c.primaryStaffProgram;
            project_.staffPerformance->otherProgram = c.otherStaffProgram;
        }
        project_.practiceMix = c.mix;
        key_->setCurrentIndex(key_->findData(c.score.tonic));
        tempo_->setValue(c.score.bpm);
        meterTop_->setValue(c.score.beatsPerBar);
        meterBottom_->setCurrentIndex(meterBottom_->findData(c.score.beatUnit));
        velocity_->setValue(c.score.baseVelocity);
        accentBeats_->setChecked(c.score.accentBeats);
        for (auto *combo : {programA_, programB_})
        {
            const int program =
                project_.staffPerformance
                    ? (combo == programA_ ? c.primaryStaffProgram : c.otherStaffProgram)
                    : (combo == programA_ ? programForVerse(c.score, 0) : programForVerse(c.score, 1));
            if (combo->findData(program) < 0)
                addTranslatedItem(combo, "ui.instrument.gm", program, {QString::number(program + 1)});
            combo->setCurrentIndex(combo->findData(program));
        }
        loading_ = false;
        if (scoreChanged || mixChanged || staffProgramsChanged || c.transpose != player_.transpose() ||
            c.speed != player_.speed() || s.metronome != old.metronome ||
            c.playbackSource != playbackSource_->currentIndex() ||
            c.originalSpeed != findChild<QDoubleSpinBox *>("originalSpeed")->value() ||
            c.originalVolume != findChild<QSlider *>("originalVolume")->value() / 100.0)
            markModified();
        if (scoreChanged)
            rebuild(true);
        else
        {
            player_.setPracticeMix(c.mix);
            refreshPracticeControls();
        }
        transpose_->setValue(c.transpose);
        player_.setSpeed(c.speed);
        {
            const QSignalBlocker speedBlock(practiceSpeed_);
            practiceSpeed_->setValue(c.speed);
        }
        accompanimentPattern_->setCurrentIndex(c.accompanimentPattern);
        findChild<QDoubleSpinBox *>("originalSpeed")->setValue(c.originalSpeed);
        findChild<QSlider *>("originalVolume")->setValue(static_cast<int>(std::lround(c.originalVolume * 100)));
        playbackSource_->setCurrentIndex(c.playbackSource);
    }
    refreshSourceControls();
    languageManager_.setLanguage(s.language);
    return true;
}

} // namespace singlilt

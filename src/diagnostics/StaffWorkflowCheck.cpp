// Staff import, correction, persistence, and playback checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffWorkflowCheck.h"
#include "audio/WaveRenderer.h"
#include "domain/Timeline.h"
#include "i18n/LanguageManager.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/ScoreView.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace singlilt
{
void runStaffWorkflowCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                           QApplication &app)
{
    QTimer::singleShot(
        100, &window,
        [&window, &languages, &args, &app]
        {
            QJsonArray checks;
            QJsonObject metrics;
            QJsonArray artifacts;
            bool passed = true;
            const auto check = [&](const QString &name, bool valid)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", valid}});
                passed &= valid;
            };
            const QString folder = QFileInfo(args.value("report")).absolutePath();
            try
            {
                const QString fixture = folder + "/run-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
                if (!QDir().mkpath(fixture))
                    throw std::runtime_error("Staff workflow output could not be created");
                const QString xml =
                    QApplication::applicationDirPath() + "/assets/staff-samples/singlilt-grand-staff.musicxml";
                window.importMusicXml(xml, 0);
                check("Complete MusicXML import creates grand staff and performance",
                      window.project().staffPerformance && window.project().staffPerformance->notes.size() == 14 &&
                          window.project().notationStyle == NotationStyle::Staff &&
                          window.project().generatedNotation && window.timeline().valid());
                if (!window.project().staffPerformance)
                    throw std::runtime_error("Full staff import failed");
                check("Both written hands enabled by default",
                      window.project().practiceMix.melodyEnabled &&
                          window.project().practiceMix.accompanimentEnabled);
                check("Source hash has not been double-encoded",
                      window.project().processing.value("musicXmlSourceSHA256").toString().size() == 64);
                bool guideControlsDisabled = true;
                for (const char *name : {"noteVelocity", "accentBeats"})
                {
                    const auto *field = window.findChild<QWidget *>(name);
                    guideControlsDisabled &= field && !field->isEnabled();
                }
                check("Original performance preserves written velocities rather than changing its guide dynamics",
                      guideControlsDisabled);
                check("Both original-staff instrument selectors are enabled",
                      window.findChild<QComboBox *>("programA")->isEnabled() &&
                          window.findChild<QComboBox *>("programB")->isEnabled());
                const auto *subtitle = window.findChild<QLabel *>("subtitle");
                check("Full staff subtitle counts every written pitch",
                      subtitle && subtitle->text() == trText("ui.staff.full_subtitle").arg(14));
                {
                    OptionsDialog options(window.appSettings(), window.optionsContext());
                    bool retainedFieldsDisabled = true;
                    for (const char *name : {"optionsCurrentVelocity", "optionsCurrentAccent"})
                    {
                        const auto *field = options.findChild<QWidget *>(name);
                        retainedFieldsDisabled &= field && !field->isEnabled();
                    }
                    check("Current Options locks written velocities but allows original-staff GM instruments",
                          retainedFieldsDisabled &&
                              options.findChild<QComboBox *>("optionsCurrentProgramA")->isEnabled() &&
                              options.findChild<QComboBox *>("optionsCurrentProgramB")->isEnabled());
                }
                const auto originalScore = scoreToJson(window.project().score);
                for (int field = 0; field < 3; ++field)
                {
                    auto changed = window.optionsContext();
                    if (field == 0)
                        changed.score.versePrograms = {40, 41};
                    else if (field == 1)
                        changed.score.baseVelocity = changed.score.baseVelocity == 64 ? 65 : 64;
                    else
                        changed.score.accentBeats = !changed.score.accentBeats;
                    bool rejected = false;
                    try
                    {
                        window.applyOptions(window.appSettings(), changed);
                    }
                    catch (const SettingsValidationError &)
                    {
                        rejected = true;
                    }
                    check(QString("Original sound-field override %1 rejected without mutation").arg(field),
                          rejected && scoreToJson(window.project().score) == originalScore);
                }
                auto instrumentOptions = window.optionsContext();
                instrumentOptions.primaryStaffProgram = 40;
                instrumentOptions.otherStaffProgram = 35;
                check("Current Options applies independent staff instruments without altering guide timing",
                      window.applyOptions(window.appSettings(), instrumentOptions) &&
                          window.project().staffPerformance->primaryProgram == 40 &&
                          window.project().staffPerformance->otherProgram == 35 &&
                          scoreToJson(window.project().score) == originalScore);
                auto invalidInstrument = window.optionsContext();
                invalidInstrument.otherStaffProgram = 128;
                bool programRejected = false;
                try
                {
                    window.applyOptions(window.appSettings(), invalidInstrument);
                }
                catch (const SettingsValidationError &)
                {
                    programRejected = true;
                }
                check("Out-of-range current staff program is rejected without mutation",
                      programRejected && window.project().staffPerformance->otherProgram == 35);
                auto *primaryInstrument = window.findChild<QComboBox *>("programA");
                auto *otherInstrument = window.findChild<QComboBox *>("programB");
                check("Full-staff toolbar offers every GM program",
                      primaryInstrument->count() == 128 && otherInstrument->count() == 128);
                const auto instrumentTick = window.player().positionTicks();
                primaryInstrument->setCurrentIndex(primaryInstrument->findData(41));
                check("Toolbar changes only the selected staff's instrument without retiming the score",
                      window.project().staffPerformance->primaryProgram == 41 &&
                          window.project().staffPerformance->otherProgram == 35 &&
                          window.player().positionTicks() == instrumentTick &&
                          scoreToJson(window.project().score) == originalScore);
                primaryInstrument->setCurrentIndex(primaryInstrument->findData(40));
                window.resize(980, 700);
                QApplication::processEvents();
                window.grab().save(fixture + "/musicxml-grand.png");
                artifacts.append(fixture + "/musicxml-grand.png");
                auto snapshot = window.project();
                snapshot.practiceSettings = window.currentProjectPractice();
                const QString path = fixture + "/grand-staff.jpp";
                saveProject(path, snapshot);
                const auto reopened = loadProject(path);
                check("JPP retains complete events, spelling, staff view and image",
                      reopened.staffPerformance &&
                          staffPerformanceToJson(*reopened.staffPerformance) ==
                              staffPerformanceToJson(*snapshot.staffPerformance) &&
                          scoreToJson(reopened.score) == scoreToJson(snapshot.score) &&
                          reopened.image == snapshot.image && reopened.notationStyle == NotationStyle::Staff);
                check("JPP retains both chosen original-staff GM instruments",
                      reopened.staffPerformance->primaryProgram == 40 &&
                          reopened.staffPerformance->otherProgram == 35);
                auto legacyStaffJson = staffPerformanceToJson(*reopened.staffPerformance);
                legacyStaffJson.remove("primaryProgram");
                legacyStaffJson.remove("otherProgram");
                const auto legacyStaff = staffPerformanceFromJson(legacyStaffJson, reopened.score);
                check("Older staff projects without instrument fields default to piano",
                      legacyStaff.primaryProgram == 0 && legacyStaff.otherProgram == 0);
                for (const auto &invalid :
                     {QJsonValue(-1), QJsonValue(128), QJsonValue(40.5), QJsonValue(QStringLiteral("piano"))})
                {
                    auto invalidJson = legacyStaffJson;
                    invalidJson.insert("primaryProgram", invalid);
                    bool rejectedProgram = false;
                    try
                    {
                        staffPerformanceFromJson(invalidJson, reopened.score);
                    }
                    catch (const std::exception &)
                    {
                        rejectedProgram = true;
                    }
                    check("Invalid persisted primary instrument is rejected", rejectedProgram);
                }
                const auto plan =
                    buildStaffPerformancePlan(snapshot.score, window.timeline(), *snapshot.staffPerformance);
                check("Written ties are merged into 11 simultaneous sounding events",
                      plan.valid() && plan.events.size() == 11);
                window.setProject(reopened);
                window.player().setAudioBackend(AudioBackend::WindowsMidi);
                window.player().seek(0);
                check("Real transport starts full-staff playback", window.player().play());
                QElapsedTimer elapsed;
                elapsed.start();
                PlaybackVoiceState voices;
                while (elapsed.elapsed() < 800)
                {
                    QApplication::processEvents();
                    voices = window.player().voiceState();
                    if (voices.chordPitches.size() >= 2 && voices.bassPitches.size() >= 3)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                check("Both staves sound together without duplicated guide channel",
                      voices.chordPitches.size() == 2 && voices.bassPitches.size() == 3 &&
                          voices.melodyPitch < 0 && voices.melodyNoteOns == 0);
                auto *left = window.findChild<QCheckBox *>("accompanimentEnabled");
                auto *right = window.findChild<QCheckBox *>("melodyEnabled");
                check("Hand controls available in playback bar", left && right && left->isEnabled());
                if (left && right)
                {
                    right->setChecked(false);
                    check("Muting primary through UI does not mute the other hand",
                          !window.player().practiceMix().melodyEnabled &&
                              window.player().practiceMix().accompanimentEnabled);
                    right->setChecked(true);
                    left->setChecked(false);
                    check("Muting other staff through UI preserves primary hand",
                          window.player().practiceMix().melodyEnabled &&
                              !window.player().practiceMix().accompanimentEnabled);
                    left->setChecked(true);
                }
                window.player().pause();
                const auto paused = window.player().positionTicks();
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                check("Pause preserves staff position", window.player().positionTicks() == paused);
                window.player().seek(240);
                check("Staff transport resumes after seek", window.player().play());
                window.player().stop();
                check("Stop clears all staff voices", !window.player().isPlaying() &&
                                                          window.player().voiceState().chordPitches.empty() &&
                                                          window.player().voiceState().bassPitches.empty());
                for (int hand = 0; hand < 2; ++hand)
                {
                    const auto &performance = *window.project().staffPerformance;
                    const auto found =
                        std::find_if(performance.notes.begin(), performance.notes.end(),
                                     [&performance, hand](const StaffPerformanceNote &note)
                                     { return (note.staff == performance.primaryStaff) == (hand == 0); });
                    const auto beforePreview = window.player().voiceState().previewNoteOns;
                    auto *graphicsView = window.findChild<QGraphicsView *>("scoreView");
                    if (!graphicsView)
                        throw std::runtime_error("The production score view is missing");
                    auto *scoreView = static_cast<ScoreView *>(graphicsView);
                    if (!scoreView->staffNoteClicked)
                        throw std::runtime_error("The production staff-note click callback is missing");
                    scoreView->staffNoteClicked(static_cast<int>(found - performance.notes.begin()));
                    elapsed.restart();
                    while (elapsed.elapsed() < 800 && window.player().voiceState().previewNoteOns == beforePreview)
                    {
                        QApplication::processEvents();
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    check(QString("Staff %1 note audition uses its selected GM program").arg(hand),
                          window.player().voiceState().previewNoteOns > beforePreview &&
                              window.player().voiceState().previewProgram == (hand == 0 ? 40 : 35));
                    window.player().pause();
                }
                auto json = projectToJson(reopened);
                json["notationView"] = QJsonObject{{"style", "invalid"}};
                bool rejected = false;
                try
                {
                    projectFromJson(json, reopened.image);
                }
                catch (const std::exception &)
                {
                    rejected = true;
                }
                check("Invalid notation persistence is rejected", rejected);

                if (args.isSet("staff-fixture"))
                {
                    QFile input(args.value("staff-fixture"));
                    if (!input.open(QIODevice::ReadOnly) || input.size() > 16 * 1024 * 1024)
                        throw std::runtime_error("Staff fixture could not be read");
                    const auto document = QJsonDocument::fromJson(input.readAll());
                    const QImage image(args.value("staff-source-image"));
                    if (!document.isObject() || image.isNull())
                        throw std::runtime_error("Staff fixture image or JSON is invalid");
                    auto userProject = projectFromJson(document.object(), image);
                    userProject.practiceSettings = ProjectPracticeSettings{};
                    userProject.practiceSettings->metronome = false;
                    const QString userPath = folder + "/大鱼-五线谱.jpp";
                    saveProject(userPath, userProject);
                    window.setProject(reopened);
                    window.openFile(userPath);
                    check("User original grand-staff image retained", window.project().image == image &&
                                                                          !window.project().generatedNotation &&
                                                                          window.project().staffPerformance);
                    const auto userTimeline = window.timeline();
                    const auto userPlan = buildStaffPerformancePlan(window.project().score, userTimeline,
                                                                    *window.project().staffPerformance);
                    check("User full score has 28 measures, both hands and 112-second clock",
                          userPlan.valid() && window.project().staffPerformance->notes.size() == 304 &&
                              userTimeline.durationTicks == 53760 && userTimeline.durationSeconds() == 112);
                    WaveRenderOptions options;
                    options.maxSeconds = 120;
                    options.settings.originalStaff = true;
                    options.mix = window.project().practiceMix;
                    options.metronome = false;
                    const QString wave = folder + "/大鱼-双手钢琴.wav";
                    const auto rendered =
                        renderWave(window.project().score, userTimeline, userPlan, wave, options);
                    check("User-score WAV is complete piano performance",
                          rendered.musicFrames == 112 * 48000 && rendered.frames == qint64(113.5 * 48000) &&
                              rendered.rms > 0 && rendered.clippedSamples == 0);
                    metrics.insert("userNotes", int(window.project().staffPerformance->notes.size()));
                    metrics.insert("userMusicSeconds", double(rendered.musicFrames) / rendered.sampleRate);
                    metrics.insert("userWaveSeconds", double(rendered.frames) / rendered.sampleRate);
                    metrics.insert("userClippedSamples", double(rendered.clippedSamples));
                    artifacts.append(userPath);
                    artifacts.append(wave);
                    QApplication::processEvents();
                    const QString screenshot = folder + "/大鱼-五线谱演奏.png";
                    window.grab().save(screenshot);
                    artifacts.append(screenshot);
                }
                languages.setLanguage("en_US");
                QApplication::processEvents();
                window.grab().save(fixture + "/staff-english.png");
            }
            catch (const std::exception &error)
            {
                check(QString::fromUtf8(error.what()), false);
            }
            QSaveFile report(args.value("report"));
            const QByteArray bytes = QJsonDocument(QJsonObject{{"passed", passed},
                                                               {"checks", checks},
                                                               {"metrics", metrics},
                                                               {"artifacts", artifacts}})
                                         .toJson();
            if (!report.open(QIODevice::WriteOnly) || report.write(bytes) != bytes.size() || !report.commit())
                app.exit(4);
            else
                app.exit(passed ? 0 : 3);
        });
}
} // namespace singlilt

// GUI transport, screenshot, and project round-trip smoke checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "SmokeRunner.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ScoreView.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <algorithm>
#include <memory>

namespace singlilt
{
namespace
{
void finish(QApplication &app, const QCommandLineParser &args, QJsonObject &report, bool passed)
{
    report["passed"] = passed;
    if (args.isSet("report"))
    {
        QFile file(args.value("report"));
        if (!file.open(QIODevice::WriteOnly))
        {
            app.exit(4);
            return;
        }
        file.write(QJsonDocument(report).toJson());
    }
    app.exit(passed ? 0 : 3);
}
} // namespace
void runGuiSmoke(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    auto report = std::make_shared<QJsonObject>();
    auto stage = std::make_shared<int>(0);
    auto loadingTimer = std::make_shared<QElapsedTimer>();
    auto *gate = new QTimer(&window);
    auto *deadline = new QTimer(&window);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, &window,
                     [report, &args, &app]
                     {
                         (*report)["error"] = "GUI smoke timed out";
                         finish(app, args, *report, false);
                     });
    deadline->start(60000);
    QObject::connect(
        gate, &QTimer::timeout, &window,
        [&window, &args, &app, report, stage, loadingTimer, gate, deadline]
        {
            if (window.isRecognizing() || window.isAudioLoading())
                return;
            if (*stage == 0)
            {
                *stage = 1;
                (*report)["noteCount"] = int(window.project().score.notes.size());
                (*report)["timelineValid"] = window.timeline().valid();
                (*report)["title"] = QString::fromStdString(window.project().score.title);
                if (args.isSet("smoke-verse"))
                {
                    const int verse = args.value("smoke-verse").toInt();
                    const auto &events = window.timeline().events;
                    const auto it = std::find_if(events.begin(), events.end(), [verse](const auto &event)
                                                 { return event.verseIndex == verse && !event.lyric.empty(); });
                    bool matched = false;
                    if (it != events.end())
                    {
                        auto *view = dynamic_cast<ScoreView *>(window.findChild<QGraphicsView *>("scoreView"));
                        if (view && view->noteClicked)
                            view->noteClicked(int(it->sourceNoteIndex));
                        auto *selector = window.findChild<QComboBox *>("verseSelector");
                        if (selector)
                        {
                            if (selector->currentIndex() == verse)
                                selector->currentIndexChanged(verse);
                            else
                                selector->setCurrentIndex(verse);
                        }
                        const auto *lyric = window.findChild<QLabel *>("lyric");
                        matched = lyric && lyric->text() == QString::fromStdString(it->lyric) &&
                                  window.player().currentVerseIndex() == verse &&
                                  window.player().currentProgram() == it->program;
                        (*report)["verseAtStart"] = window.player().currentVerseIndex();
                        (*report)["programAtStart"] = window.player().currentProgram();
                        (*report)["expectedLyric"] = QString::fromStdString(it->lyric);
                        (*report)["displayedLyric"] = lyric ? lyric->text() : QString();
                    }
                    (*report)["verseMatched"] = matched;
                }
                try
                {
                    const auto file = QFileInfo(args.value("report")).absolutePath() + "/smoke-roundtrip.jpp";
                    saveProject(file, window.project());
                    const auto restored = loadProject(file);
                    (*report)["roundtrip"] = scoreToJson(restored.score) == scoreToJson(window.project().score) &&
                                             restored.image == window.project().image;
                }
                catch (const std::exception &e)
                {
                    (*report)["roundtripError"] = QString::fromUtf8(e.what());
                }
                auto *metronome = window.findChild<QCheckBox *>("metronome");
                if (metronome)
                    metronome->setChecked(true);
                auto *play = window.findChild<QPushButton *>("play");
                (*report)["startTick"] = qint64(window.player().positionTicks());
                loadingTimer->start();
                if (play)
                    play->click();
                return;
            }
            gate->stop();
            (*report)["audioLoadMilliseconds"] = loadingTimer->elapsed();
            (*report)["playStarted"] = window.player().isPlaying();
            (*report)["audioDevice"] = window.player().deviceName();
            (*report)["sampledBackend"] = window.player().audioBackend() == AudioBackend::SampledPiano;
            QTimer::singleShot(700, &window,
                               [&window, &args, report]
                               {
                                   if (args.isSet("screenshot"))
                                       (*report)["screenshotSaved"] = window.grab().save(args.value("screenshot"));
                               });
            QTimer::singleShot(1000, &window,
                               [&window, report]
                               {
                                   (*report)["progressTicks"] = qint64(window.player().positionTicks());
                                   window.player().pause();
                                   (*report)["paused"] = !window.player().isPlaying();
                                   const auto resumeTick =
                                       std::min<std::int64_t>(480, window.player().durationTicks() / 2);
                                   (*report)["resumeStartTick"] = qint64(resumeTick);
                                   window.player().seek(resumeTick);
                                   window.player().setTranspose(2);
                                   (*report)["resumeStarted"] = window.player().play();
                               });
            QTimer::singleShot(
                1600, &window,
                [&window, &args, &app, report, deadline]
                {
                    (*report)["resumedPosition"] = qint64(window.player().positionTicks());
                    (*report)["audioError"] = window.player().errorString();
                    window.player().stop();
                    (*report)["stopped"] = !window.player().isPlaying() && window.player().positionTicks() == 0;
                    bool ok = (*report)["roundtrip"].toBool() && (*report)["timelineValid"].toBool() &&
                              (*report)["playStarted"].toBool() &&
                              (*report)["progressTicks"].toDouble() > (*report)["startTick"].toDouble() &&
                              (*report)["paused"].toBool() && (*report)["resumeStarted"].toBool() &&
                              (*report)["resumedPosition"].toDouble() > (*report)["resumeStartTick"].toDouble() &&
                              (*report)["stopped"].toBool() && (*report)["audioError"].toString().isEmpty();
                    if (args.isSet("smoke-verse"))
                        ok = ok && (*report)["verseMatched"].toBool();
                    deadline->stop();
                    finish(app, args, *report, ok);
                });
        });
    gate->start(50);
}
} // namespace singlilt

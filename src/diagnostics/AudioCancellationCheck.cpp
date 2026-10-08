// Asynchronous original-audio cancellation regressions without physical audio devices.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AudioCancellationCheck.h"
#include "cli/JsonReport.h"
#include "ui/MainWindow.h"
#include "ui/PreviewAudioSession.h"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDataStream>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace singlilt
{
namespace
{
void writeWave(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error("Could not create audio cancellation fixture");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(32036);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(16000) << quint32(32000) << quint16(2)
           << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(32000);
    for (int frame = 0; frame < 16000; ++frame)
        stream << qint16(0);
    if (stream.status() != QDataStream::Ok)
        throw std::runtime_error("Could not write audio cancellation fixture");
}

bool waitUntil(const std::function<bool()> &ready)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 5000)
    {
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(5);
    }
    return ready();
}
} // namespace

void runAudioCancellationCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        0, &window,
        [&window, &args, &app]
        {
            QJsonArray checks;
            bool passed = true;
            const auto check = [&](const char *name, bool ok)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", ok}});
                passed = passed && ok;
            };
            QString error;
            auto &original = window.originalAudioPlayer();
            try
            {
                QTemporaryDir temporary;
                if (!temporary.isValid())
                    throw std::runtime_error("Could not create audio cancellation directory");
                const auto vocals = temporary.filePath("vocals.wav");
                const auto instrumental = temporary.filePath("instrumental.wav");
                const auto invalid = temporary.filePath("invalid.wav");
                writeWave(vocals);
                writeWave(instrumental);
                QFile invalidFile(invalid);
                if (!invalidFile.open(QIODevice::WriteOnly) || invalidFile.write("invalid media") < 0)
                    throw std::runtime_error("Could not create invalid audio fixture");
                invalidFile.close();

                Project project;
                project.score.notes.push_back(Note{});
                project.score.notes.front().durationTicks = TicksPerQuarter * 2;
                AudioSourceInfo source;
                source.path = vocals.toStdString();
                source.vocalsPath = source.path;
                source.instrumentalPath = instrumental.toStdString();
                source.vocalsSeparated = true;
                source.durationSeconds = source.selectedEndSeconds = 1;
                source.timings.push_back({0, 0, 1, 0, TicksPerQuarter * 2});
                source.timingFingerprint = audioTimingFingerprint(project.score);
                project.audioSource = source;
                auto *selection = window.findChild<QComboBox *>("playbackSource");
                auto *stop = window.findChild<QPushButton *>("stop");

                // Same-turn clicks occur before the backend's queued completion, making the race deterministic.
                for (const auto &replacement : {instrumental, invalid})
                {
                    project.audioSource->instrumentalPath = replacement.toStdString();
                    window.setProject(project);
                    selection->setCurrentIndex(selection->findData(1));
                    if (!waitUntil([&] { return !original.isLoading(); }) || !original.isOpen())
                        throw std::runtime_error("Could not load original fixture");
                    if (!original.seek(0.4))
                        throw std::runtime_error("Could not seek original fixture");
                    selection->setCurrentIndex(selection->findData(3));
                    check("replacement-load-started", original.isLoading());
                    stop->click();
                    check("stop-cancels-pending-source", !original.isLoading());
                    waitUntil([&] { return !original.isLoading(); });
                    check("stop-keeps-zero-position-and-paused", !original.isPlaying() &&
                                                                     !window.player().isPlaying() &&
                                                                     original.positionSeconds() < 0.03);
                    check("stop-keeps-accepted-source",
                          original.sourcePath() == vocals && selection->currentData().toInt() == 1);
                }

                {
                    QDialog dialog(&window);
                    PreviewAudioSession audition(
                        window, [] { return false; }, [](bool) {},
                        [&](const QString &message) { error = message; }, &dialog);
                    audition.requestPlay(vocals, 0);
                    if (!waitUntil([&] { return !audition.audio().isLoading(); }) || !audition.audio().isOpen())
                        throw std::runtime_error("Could not load cached preview fixture");
                    audition.requestPlay(instrumental, 0);
                    check("replacement-preview-started", audition.audio().isLoading());
                    audition.requestPlay(vocals, 0);
                    check("cached-preview-cancels-pending-source",
                          !audition.audio().isLoading() && !audition.property("auditionLoading").toBool());
                    waitUntil([&] { return !audition.audio().isLoading(); });
                    check("cached-preview-retains-latest-selection",
                          audition.audio().sourcePath() == vocals &&
                              audition.property("auditionSourcePath").toString() == vocals);
                    audition.finish();
                }

                // Initial import loading is not an audition and must survive unrelated transport actions.
                for (bool changeSource : {false, true})
                {
                    window.openAudioImport(instrumental);
                    const QPointer<QDialog> dialog = window.findChild<QDialog *>("audioImportDialog");
                    if (!dialog)
                        throw std::runtime_error("Could not open import dialog");
                    auto *audition =
                        dynamic_cast<PreviewAudioSession *>(dialog->findChild<QObject *>("audioImportAudition"));
                    check("initial-import-load-started", audition && audition->audio().isLoading());
                    if (changeSource)
                        selection->setCurrentIndex(selection->findData(0));
                    else
                        stop->click();
                    check("transport-keeps-initial-import-load", audition && audition->audio().isLoading());
                    check("import-dialog-finishes-after-transport-action",
                          waitUntil([&] { return dialog && dialog->findChild<QDoubleSpinBox *>() != nullptr; }));
                    if (dialog)
                        dialog->reject();
                    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                }
                original.close();
            }
            catch (const std::exception &exception)
            {
                original.close();
                error = QString::fromUtf8(exception.what());
                check("exception", false);
            }
            try
            {
                writeJsonReport(args.value("report"), {{"passed", passed}, {"checks", checks}, {"error", error}});
                QTextStream(stdout) << "Audio cancellation: " << checks.size() << " checks, "
                                    << (passed ? "PASS" : "FAIL") << Qt::endl;
                app.exit(passed ? 0 : 3);
            }
            catch (const std::exception &exception)
            {
                QTextStream(stderr) << exception.what() << Qt::endl;
                app.exit(4);
            }
        });
}
} // namespace singlilt

// Numbered-score brace recognition, cloud decoding and JPP round trips.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "domain/NumberedPerformance.h"
#include "recognition/CloudRecognizer.h"
#include "recognition/WindowsOcr.h"
#include "storage/ProjectStore.h"
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

// Supply deterministic digit OCR boxes; grouping still reads the actual raster.
namespace singlilt
{
OcrText recognizeWindowsText(const QImage &image, const QString &)
{
    OcrText result;
    for (const int baseline : {130, 240, 400, 510})
        for (int i = 0; i < 8; ++i)
        {
            int left = image.width(), right = -1, top = image.height(), bottom = -1;
            for (int y = baseline - 29; y <= baseline; ++y)
                for (int x = 110 + i * 65; x < 138 + i * 65; ++x)
                    if (qGray(image.pixel(x, y)) < 128)
                    {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
            if (right >= left)
                result.words.push_back({QString::number(i % 7 + 1),
                                        QRectF(QPoint(left, top), QSize(right - left + 1, bottom - top + 1)), 0});
        }
    return result;
}
} // namespace singlilt

namespace
{
void check(bool passed, const char *message)
{
    if (!passed)
        throw std::runtime_error(message);
}

QImage scoreImage(bool brace, bool splitBrace = false)
{
    QImage image(700, 600, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(QPen(Qt::black, 2));
    QFont font("Times New Roman");
    font.setPixelSize(26);
    font.setBold(true);
    painter.setFont(font);
    for (const int baseline : {130, 240, 400, 510})
    {
        for (int i = 0; i < 8; ++i)
            painter.drawText(110 + i * 65, baseline, QString::number(i % 7 + 1));
        for (const int x : {80, 350, 640})
            painter.drawLine(x, baseline - 30, x, baseline + 12);
    }
    if (brace)
        for (const int top : {100, 370})
        {
            QPainterPath upper;
            upper.moveTo(67, top);
            upper.cubicTo(48, top + 12, 70, top + 55, 55, top + 65);
            painter.drawPath(upper);
            QPainterPath lower;
            lower.moveTo(55, top + (splitBrace ? 74 : 65));
            lower.cubicTo(70, top + 85, 48, top + 130, 67, top + 142);
            painter.drawPath(lower);
        }
    // A vertically stacked left-hand pitch shares the lower digit's onset.
    if (brace)
        painter.drawText(110, 210, "5");
    return image;
}

void servePayload(QTcpServer &server, const QJsonObject &payload)
{
    const auto body =
        QJsonDocument(
            QJsonObject{{"choices",
                         QJsonArray{QJsonObject{
                             {"finish_reason", "stop"},
                             {"message", QJsonObject{{"content", QString::fromUtf8(QJsonDocument(payload).toJson(
                                                                     QJsonDocument::Compact))}}}}}}})
            .toJson(QJsonDocument::Compact);
    QObject::connect(&server, &QTcpServer::newConnection, &server,
                     [&server, body]
                     {
                         auto *socket = server.nextPendingConnection();
                         QObject::connect(
                             socket, &QTcpSocket::readyRead, socket,
                             [socket, body]
                             {
                                 auto request = socket->property("request").toByteArray();
                                 request += socket->readAll();
                                 socket->setProperty("request", request);
                                 const int headerEnd = request.indexOf("\r\n\r\n");
                                 if (headerEnd < 0)
                                     return;
                                 int length = 0;
                                 for (const auto &line : request.left(headerEnd).split('\n'))
                                     if (line.toLower().startsWith("content-length:"))
                                         length = line.mid(15).trimmed().toInt();
                                 if (request.size() < headerEnd + 4 + length)
                                     return;
                                 socket->write(
                                     "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                     QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                                 socket->disconnectFromHost();
                             });
                     });
}

singlilt::RecognitionResult cloudResult(const QImage &image, const QJsonObject &payload)
{
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost), "Mock vision endpoint must start");
    servePayload(server, payload);
    singlilt::VisionConfig config;
    config.endpoint = QString("http://127.0.0.1:%1/v1/chat/completions").arg(server.serverPort());
    config.model = "fixture";
    config.apiKey = "fixture";
    config.timeoutSeconds = 30;
    return singlilt::recognizeCloud(image, "synthetic.png", config);
}

singlilt::Project cliResult(const QString &executable, const QImage &image, const QJsonObject &payload)
{
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost), "CLI vision endpoint must start");
    servePayload(server, payload);
    QTemporaryDir temporary;
    check(temporary.isValid(), "CLI fixture directory must exist");
    const auto input = temporary.filePath("single-hand.png");
    const auto output = temporary.filePath("single-hand.jpp");
    check(image.save(input), "CLI fixture image must save");
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("OPENAI_API_KEY", "fixture");
    environment.insert("QT_PLUGIN_PATH", QCoreApplication::libraryPaths().join(QDir::listSeparator()));
    process.setProcessEnvironment(environment);
    QEventLoop loop;
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop,
                     [&]
                     {
                         process.kill();
                         loop.quit();
                     });
    process.start(executable, {"--recognize", input, "--vision-endpoint",
                               QString("http://127.0.0.1:%1/v1/chat/completions").arg(server.serverPort()),
                               "--vision-model", "fixture", "--out", output, "--language", "en_US"});
    deadline.start(30000);
    loop.exec();
    if (process.state() != QProcess::NotRunning)
    {
        process.kill();
        process.waitForFinished(5000);
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        std::cerr << "CLI stderr: " << process.readAllStandardError().toStdString()
                  << " CLI stdout: " << process.readAllStandardOutput().toStdString() << '\n';
    check(process.state() == QProcess::NotRunning && process.exitStatus() == QProcess::NormalExit &&
              process.exitCode() == 0,
          "CLI recognition must accept the single-hand fixture");
    return singlilt::loadProject(output);
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    using namespace singlilt;
    try
    {
        const auto plain = LocalRecognizer::recognize(scoreImage(false), "plain.png");
        std::cout << "Plain notes=" << plain.score.notes.size() << " polyphony=" << bool(plain.staffPerformance)
                  << '\n';
        check(!plain.staffPerformance && plain.score.notes.size() == 32,
              "Unbraced lines and straight barlines must remain sequential");
        const auto image = scoreImage(true);
        const auto braced = LocalRecognizer::recognize(image, "braced.png");
        check(braced.staffPerformance.has_value(), "A curved brace must produce a polyphonic performance");
        const auto &performance = *braced.staffPerformance;
        check(performance.durationTicks == 7680 && braced.score.notes.size() == 16 &&
                  performance.notes.size() == 33,
              "Two braced systems must play in parallel and retain a stacked chord pitch");
        const auto lower = std::find_if(performance.notes.begin(), performance.notes.end(),
                                        [](const StaffPerformanceNote &note) { return note.staff == 2; });
        check(lower != performance.notes.end() && performance.notes[0].startTick == lower->startTick,
              "The two hands must start together");
        const auto split = LocalRecognizer::recognize(scoreImage(true, true), "split.png");
        check(split.staffPerformance && split.staffPerformance->durationTicks == performance.durationTicks,
              "A small raster gap must not disconnect a brace's two halves");
        Project project{braced.score, image, braced.warnings};
        project.staffPerformance = performance;
        project.practiceMix.accompanimentEnabled = true;
        QTemporaryDir temporary;
        check(temporary.isValid(), "Temporary JPP directory must exist");
        const auto path = temporary.filePath("two-hands.jpp");
        saveProject(path, project);
        const auto reopened = loadProject(path);
        check(reopened.notationStyle == NotationStyle::Numbered && reopened.practiceMix.accompanimentEnabled &&
                  reopened.staffPerformance &&
                  staffPerformanceToJson(*reopened.staffPerformance) == staffPerformanceToJson(performance),
              "JPP round trip must retain both hands and the numbered view");
        auto replacement = project.score.notes[0];
        replacement.degree = 6;
        replacement.durationTicks /= 2;
        replacement.verseLyrics = {"corrected"};
        auto correction = correctedNumberedGuide(project.score, *project.staffPerformance, 0, replacement);
        project.score = std::move(correction.score);
        project.staffPerformance = std::move(correction.performance);
        saveProject(path, project);
        const auto reopenedEdit = loadProject(path);
        check(reopenedEdit.notationStyle == NotationStyle::Numbered && reopenedEdit.score.notes[0].degree == 6 &&
                  reopenedEdit.score.notes[0].verseLyrics[0] == "corrected" && reopenedEdit.staffPerformance &&
                  std::any_of(reopenedEdit.staffPerformance->notes.begin(),
                              reopenedEdit.staffPerformance->notes.end(),
                              [](const StaffPerformanceNote &note) { return note.staff == 2; }) &&
                  buildStaffPerformancePlan(reopenedEdit.score, buildTimeline(reopenedEdit.score),
                                            *reopenedEdit.staffPerformance)
                      .valid(),
              "Edited numbered guide and synchronized performance survive JPP save/reload");
        auto timedProject = project;
        timedProject.score.keyChanges = {{480, 2, -1}};
        for (auto &note : timedProject.staffPerformance->notes)
            if (note.startTick >= 480)
                note.midiPitch += 2;
        timedProject.staffPerformance->timingFingerprint = staffTimingFingerprint(timedProject.score);
        saveProject(path, timedProject);
        const auto reopenedKey = loadProject(path);
        check(reopenedKey.score.keyChanges.size() == 1 && reopenedKey.score.keyChanges[0].startTick == 480 &&
                  reopenedKey.score.keyChanges[0].tonic == 2 &&
                  reopenedKey.score.keyChanges[0].sourceNoteIndex == -1 &&
                  buildStaffPerformancePlan(reopenedKey.score, buildTimeline(reopenedKey.score),
                                            *reopenedKey.staffPerformance)
                      .valid(),
              "JPP preserves independently timed key changes and their performed pitches");
        auto payload = scoreToJson(braced.score);
        payload.insert("numberedLayout", "braced");
        payload.insert("staffPerformance", staffPerformanceToJson(performance));
        const auto cloud = cloudResult(image, payload);
        check(!cloud.staffNotation && cloud.staffPerformance &&
                  cloud.staffPerformance->notes.size() == performance.notes.size(),
              "Numbered cloud recognition must not discard a valid performance");
        auto unlinked = staffPerformanceToJson(performance);
        auto unlinkedNotes = unlinked.value("notes").toArray();
        auto unlinkedNote = unlinkedNotes[0].toObject();
        unlinkedNote.insert("sourceNoteIndex", -1);
        unlinkedNotes[0] = unlinkedNote;
        unlinked.insert("notes", unlinkedNotes);
        auto unlinkedPayload = payload;
        unlinkedPayload.insert("staffPerformance", unlinked);
        bool rejectedGuideLink = false;
        try
        {
            cloudResult(image, unlinkedPayload);
        }
        catch (const std::runtime_error &)
        {
            rejectedGuideLink = true;
        }
        check(rejectedGuideLink,
              "Numbered cloud parts require one correctly linked primary event per sounding guide note");
        for (const QString layout : {QString("braced"), QString("polyphonic"), QString("missing")})
        {
            auto incomplete = scoreToJson(braced.score);
            if (layout != "missing")
                incomplete.insert("numberedLayout", layout);
            bool rejectedMissingParts = false;
            try
            {
                cloudResult(image, incomplete);
            }
            catch (const std::runtime_error &)
            {
                rejectedMissingParts = true;
            }
            check(rejectedMissingParts,
                  "Undeclared layout or declared polyphony without performance must be rejected");
        }
        auto single = performance;
        single.staffCount = 1;
        std::erase_if(single.notes, [](const StaffPerformanceNote &note) { return note.staff != 1; });
        auto singlePayload = scoreToJson(braced.score);
        singlePayload.insert("numberedLayout", "single");
        singlePayload.insert("staffPerformance", staffPerformanceToJson(single));
        check(argc > 1, "The recognition regression requires the application executable argument");
        const auto cli = cliResult(QString::fromLocal8Bit(argv[1]), image, singlePayload);
        check(cli.staffPerformance && cli.staffPerformance->staffCount == 1 &&
                  !cli.practiceMix.accompanimentEnabled,
              "CLI single-hand recognition must keep the other-hand mix disabled");
        auto invalid = staffPerformanceToJson(performance);
        auto notes = invalid.value("notes").toArray();
        auto note = notes[0].toObject();
        note.insert("bbox", QJsonArray{10000, 0, 20, 20});
        notes[0] = note;
        invalid.insert("notes", notes);
        payload.insert("staffPerformance", invalid);
        bool rejected = false;
        try
        {
            cloudResult(image, payload);
        }
        catch (const std::runtime_error &)
        {
            rejected = true;
        }
        check(rejected, "Cloud performance anchors outside the image must be rejected");
        std::cout << "PASS numbered braces, chords, JPP round trip and cloud decoding\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL numbered recognition: " << error.what() << '\n';
        return 1;
    }
}

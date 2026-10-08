// Numbered-score brace recognition, cloud decoding and JPP round trips.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "recognition/CloudRecognizer.h"
#include "recognition/WindowsOcr.h"
#include "storage/ProjectStore.h"
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
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

singlilt::RecognitionResult cloudResult(const QImage &image, const QJsonObject &payload)
{
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost), "Mock vision endpoint must start");
    const auto body =
        QJsonDocument(
            QJsonObject{{"choices",
                         QJsonArray{QJsonObject{
                             {"finish_reason", "stop"},
                             {"message", QJsonObject{{"content", QString::fromUtf8(QJsonDocument(payload).toJson(
                                                                     QJsonDocument::Compact))}}}}}}})
            .toJson(QJsonDocument::Compact);
    QObject::connect(&server, &QTcpServer::newConnection, &server,
                     [&]
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
    singlilt::VisionConfig config;
    config.endpoint = QString("http://127.0.0.1:%1/v1/chat/completions").arg(server.serverPort());
    config.model = "fixture";
    config.apiKey = "fixture";
    config.timeoutSeconds = 30;
    return singlilt::recognizeCloud(image, "synthetic.png", config);
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
        auto payload = scoreToJson(braced.score);
        payload.insert("staffPerformance", staffPerformanceToJson(performance));
        const auto cloud = cloudResult(image, payload);
        check(!cloud.staffNotation && cloud.staffPerformance &&
                  cloud.staffPerformance->notes.size() == performance.notes.size(),
              "Numbered cloud recognition must not discard a valid performance");
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

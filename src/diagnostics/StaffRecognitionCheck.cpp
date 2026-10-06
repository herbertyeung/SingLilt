// Staff-recognition candidate and review checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffRecognitionCheck.h"
#include "domain/Timeline.h"
#include "i18n/LanguageManager.h"
#include "recognition/CloudRecognitionTask.h"
#include "storage/ProjectStore.h"
#include "ui/MainWindow.h"
#include "ui/ScoreView.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <tuple>

namespace singlilt
{
namespace
{
using TaskState = CloudRecognitionTask::State;

class MockVisionServer final : public QObject
{
  public:
    MockVisionServer()
    {
        if (!server_.listen(QHostAddress::LocalHost, 0))
            throw std::runtime_error("Local staff recognition mock server did not start");
        connect(&server_, &QTcpServer::newConnection, this,
                [this]
                {
                    while (auto *socket = server_.nextPendingConnection())
                    {
                        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
                    }
                });
    }

    QString endpoint() const
    {
        return QStringLiteral("http://127.0.0.1:%1/v1/chat/completions").arg(server_.serverPort());
    }

    void respondWith(const QJsonObject &score, int delayMilliseconds = 0)
    {
        const auto content = QString::fromUtf8(QJsonDocument(score).toJson(QJsonDocument::Compact));
        response_ =
            QJsonDocument(
                QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"},
                                                               {"message", QJsonObject{{"content", content}}}}}}})
                .toJson(QJsonDocument::Compact);
        delayMilliseconds_ = delayMilliseconds;
    }

    const QList<QJsonObject> &requests() const
    {
        return requests_;
    }

    int responseAttempts() const
    {
        return responseAttempts_;
    }

    bool authorizationSeen() const
    {
        return authorizationSeen_;
    }

  private:
    void readRequest(QTcpSocket *socket)
    {
        if (socket->property("requestComplete").toBool())
            return;
        const QByteArray bytes = socket->property("requestBytes").toByteArray() + socket->readAll();
        socket->setProperty("requestBytes", bytes);
        const int headerEnd = bytes.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        qint64 contentLength = -1;
        for (const auto &line : bytes.left(headerEnd).split('\n'))
        {
            const auto lower = line.trimmed().toLower();
            if (lower.startsWith("content-length:"))
                contentLength = lower.mid(15).trimmed().toLongLong();
            if (lower.startsWith("authorization:"))
                authorizationSeen_ = true;
        }
        if (contentLength < 0 || contentLength > 8 * 1024 * 1024 || bytes.size() < headerEnd + 4 + contentLength)
            return;
        socket->setProperty("requestComplete", true);
        requests_.append(QJsonDocument::fromJson(bytes.mid(headerEnd + 4, contentLength)).object());
        const auto response = response_;
        const QPointer<QTcpSocket> guardedSocket(socket);
        QTimer::singleShot(delayMilliseconds_, this,
                           [this, guardedSocket, response]
                           {
                               ++responseAttempts_;
                               if (!guardedSocket || guardedSocket->state() != QAbstractSocket::ConnectedState)
                                   return;
                               guardedSocket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                                    "Connection: close\r\nContent-Length: " +
                                                    QByteArray::number(response.size()) + "\r\n\r\n" + response);
                               guardedSocket->disconnectFromHost();
                           });
    }

    QTcpServer server_;
    QByteArray response_;
    QList<QJsonObject> requests_;
    int delayMilliseconds_ = 0;
    int responseAttempts_ = 0;
    bool authorizationSeen_ = false;
};

bool waitUntil(const std::function<bool()> &condition, int milliseconds = 5000)
{
    if (condition())
        return true;
    QEventLoop loop;
    QTimer poll;
    QTimer deadline;
    poll.setInterval(10);
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop,
                     [&]
                     {
                         if (condition())
                             loop.quit();
                     });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    deadline.start(milliseconds);
    loop.exec();
    return condition();
}

QImage staffImage()
{
    QImage image(640, 240, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(QPen(Qt::black, 1));
    for (int line = 0; line < 5; ++line)
        painter.drawLine(20, 60 + line * 12, 620, 60 + line * 12);
    painter.setBrush(Qt::black);
    for (int index = 0; index < 9; ++index)
    {
        const QPoint center(55 + 50 * index, 100 - 6 * (index % 5));
        painter.drawEllipse(center, 6, 4);
        painter.drawLine(center + QPoint(6, 0), center + QPoint(6, -35));
    }
    return image;
}

QJsonObject staffFixture()
{
    Score score;
    score.title = "Staff recognition mock melody";
    score.tonic = 7;
    score.beatsPerBar = 3;
    const int degrees[]{7, 7, 1, 0, 2, 3, 4, 5, 5};
    const int octaves[]{-1, -1, 0, 0, 0, 0, 0, 0, 0};
    const int durations[]{240, 240, 720, 240, 160, 160, 160, 480, 480};
    const char steps[]{'F', 'F', 'G', 'C', 'A', 'B', 'C', 'D', 'D'};
    for (int index = 0; index < 9; ++index)
    {
        Note note;
        note.id = index;
        note.degree = degrees[index];
        note.octave = octaves[index];
        note.accidental = index == 1 ? -1 : 0;
        note.durationTicks = durations[index];
        note.measure = index < 4 ? 0 : 1;
        note.tieToNext = index == 7;
        note.source = {double(49 + index * 50), double(96 - 6 * (index % 5)), 12, 8};
        if (note.degree)
            note.staffSpelling = StaffSpelling{steps[index], index == 0 ? 1 : 0, index >= 6 ? 5 : 4};
        score.notes.push_back(note);
    }
    score.repeats = {{0, score.notes.size(), 2, -1}};
    auto object = scoreToJson(score);
    object.insert(
        "notation",
        QJsonObject{
            {"type", "staff"}, {"clef", "treble"}, {"keyFifths", 1}, {"minor", false}, {"monophonic", true}});
    return object;
}

QJsonObject withFirstNote(QJsonObject score, const std::function<void(QJsonObject &)> &modify)
{
    auto notes = score.value("notes").toArray();
    auto first = notes[0].toObject();
    modify(first);
    notes[0] = first;
    score.insert("notes", notes);
    return score;
}

QJsonObject grandStaffFixture()
{
    auto object = staffFixture();
    const auto score = scoreFromJson(object);
    StaffPerformance performance;
    performance.staffCount = 2;
    performance.primaryStaff = 1;
    performance.sourceTonic = score.tonic;
    performance.timingFingerprint = staffTimingFingerprint(score);
    std::int64_t tick = 0;
    for (std::size_t index = 0; index < score.notes.size(); ++index)
    {
        const auto &lead = score.notes[index];
        if (lead.degree != 0)
        {
            StaffPerformanceNote note;
            note.startTick = tick;
            note.durationTicks = lead.durationTicks;
            note.midiPitch = midiPitch(lead, score.tonic);
            note.staff = 1;
            note.voice = "1";
            note.sourceNoteIndex = int(index);
            note.tieStart = lead.tieToNext;
            note.tieStop = index > 0 && score.notes[index - 1].tieToNext;
            note.source = lead.source;
            note.staffSpelling = lead.staffSpelling;
            performance.notes.push_back(note);
        }
        tick += lead.durationTicks;
    }
    performance.durationTicks = tick;
    const int bassPitches[]{50, 57, 55, 59};
    for (int index = 0; index < 4; ++index)
    {
        StaffPerformanceNote note;
        note.startTick = index < 2 ? 0 : 1440;
        note.durationTicks = 1440;
        note.midiPitch = bassPitches[index];
        note.staff = 2;
        note.voice = "2";
        note.source = {double(49 + index / 2 * 250), double(170 - index % 2 * 20), 12, 8};
        performance.notes.push_back(note);
    }
    StaffPerformanceNote inner;
    inner.startTick = 0;
    inner.durationTicks = 960;
    inner.midiPitch = 59;
    inner.staff = 1;
    inner.voice = "3";
    inner.source = {49, 125, 12, 8};
    performance.notes.push_back(inner);
    std::stable_sort(performance.notes.begin(), performance.notes.end(),
                     [](const StaffPerformanceNote &left, const StaffPerformanceNote &right)
                     {
                         return std::tie(left.startTick, left.staff, left.voice, left.midiPitch) <
                                std::tie(right.startTick, right.staff, right.voice, right.midiPitch);
                     });
    auto full = staffPerformanceToJson(performance);
    full.insert("noteCount", int(performance.notes.size()));
    full.insert("voiceCount", 3);
    object.insert("staffPerformance", full);
    auto notation = object.value("notation").toObject();
    notation.insert("clef", "grand");
    notation.insert("monophonic", false);
    object.insert("notation", notation);
    return object;
}

QJsonObject withPerformance(QJsonObject object, const std::function<void(QJsonObject &)> &modify)
{
    auto performance = object.value("staffPerformance").toObject();
    modify(performance);
    object.insert("staffPerformance", performance);
    return object;
}

class StaffRecognitionProbe final : public QObject
{
  public:
    StaffRecognitionProbe(MainWindow &window, const QCommandLineParser &args, QApplication &app)
        : QObject(&window), window_(window), reportPath_(args.value("report")), app_(app)
    {
        QDir().mkpath(QFileInfo(reportPath_).absolutePath());
        QTimer::singleShot(0, this, [this] { run(); });
    }

  private:
    void check(const QString &name, bool condition)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", condition}});
        if (!condition)
            throw std::runtime_error(name.toStdString());
    }

    bool projectPreserved() const
    {
        return scoreToJson(window_.project().score) == protectedScore_ &&
               window_.project().image == protectedImage_ && window_.hasUnsavedChanges();
    }

    void startRequest(MockVisionServer &server, RecognitionNotation notation = RecognitionNotation::Staff)
    {
        auto &task = window_.cloudRecognitionTask();
        task.discard();
        VisionConfig config;
        config.endpoint = server.endpoint();
        config.model = "local-staff-protocol-fixture";
        config.timeoutSeconds = VisionConfig::MinTimeoutSeconds;
        check("Local recognition task starts",
              task.start(requestImage_, "staff-fixture.png", "Staff fixture", config, notation));
    }

    void expectFailure(MockVisionServer &server, const QJsonObject &response, const QString &name,
                       const char *message = nullptr)
    {
        server.respondWith(response);
        startRequest(server);
        auto &task = window_.cloudRecognitionTask();
        check(name + " completes", waitUntil([&] { return !task.isRunning(); }));
        check(name + " rejects without a candidate", task.state() == TaskState::Failed && !task.result());
        check(name + " preserves current score", projectPreserved());
        if (message)
            check(name + " reports the expected diagnostic",
                  localizeMessage(task.errorString()) == trText(message));
    }

    void run()
    {
        QString error;
        bool passed = false;
        try
        {
            MockVisionServer server;
            requestImage_ = staffImage();
            Project protectedProject;
            protectedProject.score = scoreFromJson(staffFixture());
            protectedProject.score.title = "Current score must remain unchanged";
            protectedProject.image = requestImage_;
            window_.setProject(protectedProject, true);
            protectedScore_ = scoreToJson(window_.project().score);
            protectedImage_ = window_.project().image;
            const auto fixture = staffFixture();
            server.respondWith(fixture);
            startRequest(server);
            auto &task = window_.cloudRecognitionTask();
            const auto original = requestImage_;
            requestImage_.fill(QColor(220, 242, 250));
            check("Staff recognition completes", waitUntil([&] { return !task.isRunning(); }));
            check("Staff result remains a review candidate", task.state() == TaskState::Ready && task.result());
            check("Original request image is an immutable snapshot", task.image() == original);
            check("Source path and label are retained",
                  task.sourceLabel() == "Staff fixture" && task.result()->score.imagePath == "staff-fixture.png");
            check("Successful completion does not replace current score", projectPreserved());
            check("Task does not auto-open a modal or preview",
                  !QApplication::activeModalWidget() && !window_.findChild<QDialog *>("recognitionPreview"));
            const auto *result = task.result();
            check("Treble key signature metadata is retained", result->staffNotation && !result->staffBass &&
                                                                   result->staffKeyFifths == 1 &&
                                                                   !result->staffMinor);
            check("Recognized accidentals match sounding pitches",
                  midiPitch(result->score.notes[0], 7) == 66 && midiPitch(result->score.notes[1], 7) == 65 &&
                      result->score.notes[0].staffSpelling && result->score.notes[0].staffSpelling->alter == 1 &&
                      result->score.notes[1].staffSpelling->alter == 0);
            check("Dotted durations, triplets and rests are retained",
                  result->score.notes[2].durationTicks == 720 && result->score.notes[3].degree == 0 &&
                      result->score.notes[4].durationTicks == 160 && !result->score.notes[3].staffSpelling);
            const auto timeline = buildTimeline(result->score);
            check("Ties and repeats reach the existing playback timeline",
                  timeline.valid() && timeline.events.size() == 18 && !timeline.events[8].attack &&
                      !timeline.events[17].attack && timeline.durationTicks == 5760);
            check("Staff mode sends a staff-specific prompt",
                  server.requests().size() == 1 && requestPrompt(server.requests().last()) ==
                                                       recognitionPrompt(640, 240, RecognitionNotation::Staff));
            const auto content =
                server.requests().last().value("messages").toArray()[0].toObject().value("content").toArray();
            const auto imageUrl = content[1].toObject().value("image_url").toObject().value("url").toString();
            const auto decoded =
                QByteArray::fromBase64(imageUrl.mid(QStringLiteral("data:image/png;base64,").size()).toLatin1());
            check("Request sends the original source image",
                  QImage::fromData(decoded, "PNG").convertToFormat(QImage::Format_RGB32) == original);
            check("Diagnostic never sends authorization", !server.authorizationSeen());
            auto *preview = window_.findChild<QPushButton *>("cloudTaskPreview");
            check("Preview requires an explicit user action", preview && preview->isEnabled());
            preview->click();
            auto *dialog = window_.findChild<QDialog *>("recognitionPreview");
            check("Explicit preview opens without replacing current score",
                  dialog && dialog->isVisible() && !dialog->isModal() && projectPreserved());
            dialog->close();
            check("Closing preview leaves the candidate unapplied",
                  task.state() == TaskState::Ready && task.result() && projectPreserved());
            requestImage_ = original;
            testValidation(server, fixture);
            testBassAndDefaults(server, fixture);
            const auto grand = grandStaffFixture();
            testGrandStaff(server, grand);
            testCancellation(server, grand);
            check("All fixture requests remain local and credential-free", !server.authorizationSeen());
            task.discard();
            passed = true;
        }
        catch (const std::exception &exception)
        {
            error = QString::fromUtf8(exception.what());
            window_.cloudRecognitionTask().cancel();
        }
        const QJsonObject report{
            {"passed", passed},
            {"validationScope", "Local mock protocol, parsing, preview and task lifecycle only"},
            {"realModelAccuracyTested", false},
            {"checks", checks_},
            {"error", error}};
        const auto bytes = QJsonDocument(report).toJson();
        QTextStream(stdout) << bytes;
        QDir().mkpath(QFileInfo(reportPath_).absolutePath());
        QFile file(reportPath_);
        const bool written =
            !reportPath_.isEmpty() && file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        app_.exit(written ? (passed ? 0 : 3) : 4);
    }

    QString requestPrompt(const QJsonObject &request) const
    {
        return request.value("messages")
            .toArray()[0]
            .toObject()
            .value("content")
            .toArray()[0]
            .toObject()
            .value("text")
            .toString();
    }

    void testValidation(MockVisionServer &server, const QJsonObject &fixture)
    {
        expectFailure(
            server,
            {{"notation", QJsonObject{{"type", "staff"}, {"monophonic", false}}}, {"diagnostic", "polyphony"}},
            "Legacy polyphonic result without events", "messages.staff_recognition.incomplete_performance");
        expectFailure(server,
                      {{"notation", QJsonObject{{"type", "staff"}, {"monophonic", true}}},
                       {"diagnostic", "unsupported_notation"}},
                      "Unsupported notation", "messages.staff_recognition.unsupported_notation");
        auto missing = fixture;
        missing.remove("notation");
        expectFailure(server, missing, "Missing staff metadata", "messages.staff_recognition.invalid_result");
        auto malformed = fixture;
        auto notation = malformed.value("notation").toObject();
        notation.insert("keyFifths", "1");
        malformed.insert("notation", notation);
        expectFailure(server, malformed, "Non-numeric key signature", "messages.staff_recognition.invalid_result");
        notation.insert("keyFifths", 8);
        malformed.insert("notation", notation);
        expectFailure(server, malformed, "Out-of-range key signature",
                      "messages.staff_recognition.invalid_result");
        expectFailure(server, withFirstNote(fixture, [](QJsonObject &note) { note.remove("staffSpelling"); }),
                      "Missing note spelling", "messages.staff_recognition.invalid_result");
        expectFailure(server,
                      withFirstNote(fixture, [](QJsonObject &note) { note.insert("staffSpelling", "F#4"); }),
                      "Non-object note spelling", "messages.staff_recognition.invalid_result");
        expectFailure(server,
                      withFirstNote(fixture,
                                    [](QJsonObject &note)
                                    {
                                        auto spelling = note.value("staffSpelling").toObject();
                                        spelling.insert("alter", 3);
                                        note.insert("staffSpelling", spelling);
                                    }),
                      "Out-of-range accidental", "messages.staff_recognition.invalid_result");
        expectFailure(server, withFirstNote(fixture, [](QJsonObject &note) { note.insert("degree", 1); }),
                      "Written and sounding pitch mismatch", "messages.staff_recognition.pitch_mismatch");
        expectFailure(server,
                      withFirstNote(fixture, [](QJsonObject &note) { note.insert("durationTicks", 1500); }),
                      "Measure duration overflow", "messages.staff_recognition.invalid_result");
        expectFailure(server,
                      withFirstNote(fixture, [](QJsonObject &note) { note.insert("durationTicks", 160.5); }),
                      "Fractional duration");
        expectFailure(
            server,
            withFirstNote(fixture, [](QJsonObject &note) { note.insert("bbox", QJsonArray{639, 60, 20, 8}); }),
            "Anchor outside original image", "messages.recognition.rectangle_outside");
        expectFailure(server, withFirstNote(fixture, [](QJsonObject &note) { note.insert("keyOverride", 0); }),
                      "Mid-score key change", "messages.staff_recognition.unsupported_notation");
    }

    void testBassAndDefaults(MockVisionServer &server, const QJsonObject &fixture)
    {
        auto bass = fixture;
        bass.insert("tonic", 0);
        bass.insert(
            "notation",
            QJsonObject{
                {"type", "staff"}, {"clef", "bass"}, {"keyFifths", -3}, {"minor", true}, {"monophonic", true}});
        auto note = bass.value("notes").toArray()[0].toObject();
        note.insert("degree", 1);
        note.insert("octave", -1);
        note.insert("durationTicks", 480);
        note.insert("staffSpelling", QJsonObject{{"step", "C"}, {"alter", 0}, {"octave", 3}});
        bass.insert("notes", QJsonArray{note});
        bass.insert("repeats", QJsonArray{});
        server.respondWith(bass);
        startRequest(server);
        auto &task = window_.cloudRecognitionTask();
        check("Bass minor recognition completes", waitUntil([&] { return !task.isRunning(); }));
        check("Bass clef and minor key are retained", task.result() && task.result()->staffNotation &&
                                                          task.result()->staffBass && task.result()->staffMinor &&
                                                          task.result()->staffKeyFifths == -3 &&
                                                          midiPitch(task.result()->score.notes[0], 0) == 48);
        auto numbered = fixture;
        numbered.remove("notation");
        auto notes = numbered.value("notes").toArray();
        for (int index = 0; index < notes.size(); ++index)
        {
            auto object = notes[index].toObject();
            object.remove("staffSpelling");
            notes[index] = object;
        }
        numbered.insert("notes", notes);
        server.respondWith(numbered);
        task.discard();
        VisionConfig config;
        config.endpoint = server.endpoint();
        config.model = "local-numbered-protocol-fixture";
        check("Existing task API defaults to numbered notation",
              task.start(requestImage_, "numbered.png", "Numbered fixture", config));
        check("Default numbered recognition completes", waitUntil([&] { return !task.isRunning(); }));
        check("Default result retains legacy metadata defaults",
              task.result() && !task.result()->staffNotation && !task.result()->staffBass &&
                  task.result()->staffKeyFifths == 0 && !task.result()->staffMinor);
        check("Default request retains numbered notation prompt",
              requestPrompt(server.requests().last()) == recognitionPrompt(640, 240) &&
                  recognitionPrompt(640, 240).startsWith("Read the attached numbered musical notation"));
        check("Default recognition also does not replace current score", projectPreserved());
    }

    void testGrandStaff(MockVisionServer &server, const QJsonObject &fixture)
    {
        server.respondWith(fixture);
        startRequest(server);
        auto &task = window_.cloudRecognitionTask();
        check("Grand-staff recognition completes", waitUntil([&] { return !task.isRunning(); }));
        check("Grand-staff result retains the complete performance",
              task.state() == TaskState::Ready && task.result() && task.result()->staffNotation &&
                  task.result()->staffPerformance && task.result()->staffPerformance->staffCount == 2 &&
                  task.result()->staffPerformance->notes.size() == 13);
        const auto &performance = *task.result()->staffPerformance;
        const auto soundingAtStart =
            std::count_if(performance.notes.begin(), performance.notes.end(),
                          [](const StaffPerformanceNote &note) { return note.startTick == 0; });
        check("Both hands and chord tones retain simultaneous starts", soundingAtStart == 4);
        check("Bass held notes overlap faster right-hand notes",
              std::any_of(performance.notes.begin(), performance.notes.end(), [](const StaffPerformanceNote &note)
                          { return note.staff == 2 && note.startTick == 0 && note.durationTicks == 1440; }) &&
                  std::any_of(performance.notes.begin(), performance.notes.end(),
                              [](const StaffPerformanceNote &note)
                              { return note.staff == 1 && note.startTick == 240 && note.durationTicks == 240; }));
        const auto timeline = buildTimeline(task.result()->score);
        const auto plan = buildStaffPerformancePlan(task.result()->score, timeline, performance);
        check("Full performance uses the existing repeat traversal",
              plan.valid() && plan.durationTicks == 5760 &&
                  std::count_if(plan.events.begin(), plan.events.end(),
                                [](const AccompanimentEvent &note) { return note.startTick == 0; }) == 4 &&
                  std::count_if(plan.events.begin(), plan.events.end(),
                                [](const AccompanimentEvent &note) { return note.startTick == 2880; }) == 4);
        check("Full-performance recognition remains an unapplied candidate", projectPreserved());
        auto *preview = window_.findChild<QPushButton *>("cloudTaskPreview");
        check("Grand-staff preview is explicitly requested", preview && preview->isEnabled());
        preview->click();
        QDialog *dialog = nullptr;
        for (auto *candidate : window_.findChildren<QDialog *>("recognitionPreview"))
            if (candidate->isVisible())
                dialog = candidate;
        check("Grand-staff preview is nonmodal and preserves current score",
              dialog && !dialog->isModal() && projectPreserved());
        auto *table = dialog->findChild<QTableWidget *>("previewNotes");
        auto *summary = dialog->findChild<QLabel *>("previewSummary");
        auto *view = static_cast<ScoreView *>(dialog->findChild<QGraphicsView *>("previewScoreView"));
        check("Grand-staff preview lists every full-performance event",
              table && table->rowCount() == int(performance.notes.size()) && table->columnCount() == 8 &&
                  table->horizontalHeaderItem(1)->text() == trText("ui.preview.performance.column_staff") &&
                  table->horizontalHeaderItem(3)->text() == trText("ui.preview.performance.column_onset"));
        check("Grand-staff preview reports complete staff, voice and note counts",
              summary && summary->text().contains(trText("ui.preview.performance.summary").arg(2).arg(3).arg(13)));
        int bassRow = -1;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            const auto &note = performance.notes[std::size_t(row)];
            QStringList ties;
            if (note.tieStart)
                ties.append(trText("ui.preview.performance.tie_start"));
            if (note.tieStop)
                ties.append(trText("ui.preview.performance.tie_stop"));
            check("Full-performance table retains staff, voice, onset, duration and pitch",
                  table->item(row, 1)->text() == QString::number(note.staff) &&
                      table->item(row, 2)->text() == QString::fromStdString(note.voice) &&
                      table->item(row, 3)->text() ==
                          QString::number(double(note.startTick) / TicksPerQuarter, 'g', 6) &&
                      table->item(row, 4)->text() ==
                          QString::number(double(note.durationTicks) / TicksPerQuarter, 'g', 6) &&
                      table->item(row, 5)->text() == QString::number(note.midiPitch) &&
                      table->item(row, 6)->text() == QString::number(note.velocity) &&
                      table->item(row, 7)->text() == (ties.isEmpty() ? QString(QChar(0x2014)) : ties.join(" / ")));
            if (note.staff == 2 && bassRow == -1)
                bassRow = row;
        }
        check("Grand-staff preview exposes bass rows", bassRow >= 0 && view);
        table->setCurrentCell(bassRow, 0);
        QGraphicsRectItem *selection = nullptr;
        for (auto *item : view->scene()->items())
            if (item->data(0).toString() == "staffPreviewSelection")
                selection = qgraphicsitem_cast<QGraphicsRectItem *>(item);
        const auto &bassBox = performance.notes[std::size_t(bassRow)].source;
        check("Selecting a bass row locates its own original-image notehead",
              selection && selection->isVisible() &&
                  selection->rect() ==
                      QRectF(bassBox.x - 4, bassBox.y - 6, bassBox.width + 8, bassBox.height + 12));
        check("Preview row selection does not play or replace the current score",
              !window_.player().isPlaying() && projectPreserved());
        QApplication::processEvents();
        check("Grand-staff preview screenshot is saved",
              dialog->grab().save(QFileInfo(reportPath_).absolutePath() + "/staff-polyphonic-preview.png"));
        dialog->close();

        auto missing = fixture;
        missing.remove("staffPerformance");
        expectFailure(server, missing, "Grand staff without complete events",
                      "messages.staff_recognition.incomplete_performance");
        expectFailure(server, withPerformance(fixture, [](QJsonObject &full) { full.insert("noteCount", 14); }),
                      "Incorrect full note count");
        expectFailure(server, withPerformance(fixture, [](QJsonObject &full) { full.insert("voiceCount", 4); }),
                      "Incorrect voice count");
        expectFailure(server, withPerformance(fixture, [](QJsonObject &full) { full.insert("primaryStaff", 3); }),
                      "Primary staff outside staff count");
        expectFailure(server,
                      withPerformance(fixture,
                                      [](QJsonObject &full)
                                      {
                                          auto notes = full.value("notes").toArray();
                                          notes.removeAt(0);
                                          full.insert("notes", notes);
                                          full.insert("noteCount", notes.size());
                                      }),
                      "Omitted primary melody event");
        expectFailure(server,
                      withPerformance(fixture,
                                      [](QJsonObject &full)
                                      {
                                          auto notes = full.value("notes").toArray();
                                          auto first = notes[0].toObject();
                                          first.insert("startTick", -1);
                                          notes[0] = first;
                                          full.insert("notes", notes);
                                      }),
                      "Negative performance start tick");
        expectFailure(server,
                      withPerformance(fixture,
                                      [](QJsonObject &full)
                                      {
                                          auto notes = full.value("notes").toArray();
                                          auto first = notes[0].toObject();
                                          first.insert("midiPitch", 128);
                                          notes[0] = first;
                                          full.insert("notes", notes);
                                      }),
                      "Out-of-range performance pitch");
        expectFailure(server,
                      withPerformance(fixture,
                                      [](QJsonObject &full)
                                      {
                                          auto notes = full.value("notes").toArray();
                                          auto first = notes[0].toObject();
                                          first.insert("bbox", QJsonArray{639, 60, 20, 8});
                                          notes[0] = first;
                                          full.insert("notes", notes);
                                      }),
                      "Performance anchor outside original image", "messages.recognition.rectangle_outside");
    }

    void testCancellation(MockVisionServer &server, const QJsonObject &fixture)
    {
        server.respondWith(fixture, 300);
        const auto previousRequests = server.requests().size();
        const int previousResponses = server.responseAttempts();
        startRequest(server);
        auto &task = window_.cloudRecognitionTask();
        check("Cancellation fixture reaches the local server",
              waitUntil([&] { return server.requests().size() > previousRequests; }));
        task.cancel();
        check("Cancellation completes", waitUntil([&] { return !task.isRunning(); }));
        check("Cancelled request has no candidate", task.state() == TaskState::Cancelled && !task.result());
        check("Cancellation preserves current score", projectPreserved());
        check("Late server reply is observed",
              waitUntil([&] { return server.responseAttempts() > previousResponses; }));
        check("Late reply does not resurrect the candidate",
              task.state() == TaskState::Cancelled && !task.result() && projectPreserved());
        server.respondWith(fixture);
        startRequest(server);
        check("Staff task restarts after cancellation",
              waitUntil([&] { return !task.isRunning(); }) && task.state() == TaskState::Ready && task.result() &&
                  task.result()->staffNotation && task.result()->staffPerformance &&
                  task.result()->staffPerformance->notes.size() == 13 && projectPreserved());
    }

    MainWindow &window_;
    QString reportPath_;
    QApplication &app_;
    QJsonArray checks_;
    QJsonObject protectedScore_;
    QImage protectedImage_;
    QImage requestImage_;
};
} // namespace

void runStaffRecognitionCheck(MainWindow &window, const QCommandLineParser &args, QApplication &app)
{
    new StaffRecognitionProbe(window, args, app);
}
} // namespace singlilt

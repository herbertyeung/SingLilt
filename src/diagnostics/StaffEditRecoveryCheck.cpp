// Staff edit snapshot and recovery-package checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffEditRecoveryCheck.h"

#include "application/StaffAnchorCorrection.h"
#include "application/StaffScoreCorrection.h"
#include "domain/Timeline.h"
#include "storage/ProjectPackage.h"
#include "storage/ProjectStore.h"
#include "storage/StaffEditRecovery.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace singlilt
{
namespace
{
QByteArray fileHash(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        throw std::runtime_error("Cannot hash the recovery test artifact");
    return hash.result().toHex();
}

bool rejects(const std::function<void()> &operation)
{
    try
    {
        operation();
    }
    catch (const std::exception &)
    {
        return true;
    }
    return false;
}

QJsonArray soundEvents(const Project &project)
{
    const auto plan =
        buildStaffPerformancePlan(project.score, buildTimeline(project.score), *project.staffPerformance);
    if (!plan.valid())
        throw std::runtime_error("Recovery fixture sound is invalid");
    QJsonArray events;
    for (const auto &event : plan.events)
        events.append(
            QJsonArray{double(event.startTick), double(event.durationTicks), event.midiPitch, event.velocity});
    return events;
}

Project fixtureProject()
{
    Project project;
    project.staffImagePlayback = true;
    project.notationStyle = NotationStyle::Staff;
    project.processing = {{"local", true}, {"fixture", "staff-edit-recovery"}};
    project.score.title = "Independent staff edit recovery fixture";
    project.score.baseVelocity = 72;
    project.score.accentBeats = true;
    project.score.versePrograms = {40, 35};
    project.score.writtenMeasures = {{0, 1920, 0, 4, 4, 0}};
    project.practiceMix.melodyVolume = .37;
    project.practiceMix.accompanimentVolume = .63;
    ProjectPracticeSettings practice;
    practice.transpose = 3;
    practice.speed = 1.25;
    practice.metronome = false;
    practice.originalVolume = .44;
    project.practiceSettings = practice;
    StaffPerformance performance;
    performance.staffCount = 1;
    performance.durationTicks = 1920;
    performance.primaryProgram = 40;
    performance.otherProgram = 35;
    performance.clefChanges = {{0, 1, false}};
    for (int index = 0; index < 4; ++index)
    {
        Note guide;
        guide.id = index;
        guide.degree = 1;
        guide.durationTicks = 480;
        guide.source = {100.0 + index * 80, 150, 24, 16};
        guide.hasImageAnchor = true;
        project.score.notes.push_back(guide);
        StaffPerformanceNote note;
        note.midiPitch = 60;
        note.startTick = index * 480;
        note.durationTicks = 480;
        note.staff = 1;
        note.voice = "manual";
        note.sourceNoteIndex = index;
        note.source = guide.source;
        note.hasImageAnchor = true;
        performance.notes.push_back(note);
    }
    performance.timingFingerprint = staffTimingFingerprint(project.score);
    project.staffPerformance = performance;
    project.image = QImage(640, 480, QImage::Format_RGB32);
    project.image.fill(Qt::white);
    project.staffPages.push_back({"Original fixture", project.image, project.image, 0, 1920});
    return project;
}
} // namespace

QJsonObject checkStaffEditRecovery()
{
    QJsonArray checks;
    QString error;
    bool passed = false;
    const auto check = [&](const QString &name, bool valid)
    {
        checks.append(QJsonObject{{"name", name}, {"passed", valid}});
        if (!valid)
            throw std::runtime_error(name.toStdString());
    };
    try
    {
        QTemporaryDir temporary;
        check("Recovery fixtures have an isolated temporary directory", temporary.isValid());
        const auto sourcePath = temporary.filePath("user-original.jpp");
        const auto folder = temporary.filePath("recovery");
        const auto original = fixtureProject();
        saveProject(sourcePath, original);
        const auto originalHash = fileHash(sourcePath);
        const auto positioned = correctedStaffAnchor(original, 0, {230, 180, 24, 16});
        auto notes = positioned.staffPerformance->notes;
        notes.front().midiPitch = 67;
        notes.front().durationTicks = 240;
        const auto changed = correctedStaffProject(positioned, std::move(notes), positioned.score.writtenMeasures);
        const auto changedJson = projectToJson(changed);
        check("An absent recovery directory has no latest snapshot", latestStaffEditRecovery(folder).isEmpty());
        const auto recovery = saveStaffEditRecovery(changed, sourcePath, "session-a", folder);
        check("A recovery package is independent and contained in its dedicated directory",
              QFileInfo(recovery).absolutePath() == folder && recovery != sourcePath &&
                  fileHash(sourcePath) == originalHash);
        const auto restored = loadProject(recovery);
        auto withoutRecord = restored;
        withoutRecord.processing.remove("staffEditRecovery");
        check("Atomic package roundtrip preserves edited music, all source pixels and UI boxes",
              projectToJson(withoutRecord) == changedJson && restored.image == changed.image &&
                  restored.staffPages[0].sourceImage == changed.staffPages[0].sourceImage &&
                  soundEvents(restored) == soundEvents(changed) &&
                  restored.staffPerformance->notes.front().midiPitch == 67 &&
                  restored.staffPerformance->notes.front().durationTicks == 240);
        check("Recovery retains explicit mix, instruments, velocity and practice settings instead of defaults",
              restored.practiceMix.melodyVolume == .37 && restored.practiceMix.accompanimentVolume == .63 &&
                  restored.score.baseVelocity == 72 && restored.score.accentBeats &&
                  restored.score.versePrograms == std::vector<int>{40, 35} &&
                  restored.staffPerformance->primaryProgram == 40 &&
                  restored.staffPerformance->otherProgram == 35 && restored.practiceSettings &&
                  restored.practiceSettings->transpose == 3 && restored.practiceSettings->speed == 1.25 &&
                  !restored.practiceSettings->metronome && restored.practiceSettings->originalVolume == .44);
        const auto record = restored.processing.value("staffEditRecovery").toObject();
        check("Recovery records actual source path/hash, destination, session and timestamp",
              record.value("sourcePath").toString() == sourcePath &&
                  record.value("sourceSHA256").toString().toLatin1() == originalHash &&
                  record.value("sourceHashStatus").toString() == "available" &&
                  record.value("recoveryPath").toString() == recovery &&
                  record.value("sessionId").toString() == "session-a" &&
                  !record.value("updatedAtUtc").toString().isEmpty());
        check("Recovery save does not mutate the supplied editor project", projectToJson(changed) == changedJson);
        check("Latest lookup reads the committed recovery package", latestStaffEditRecovery(folder) == recovery);
        const auto same = saveStaffEditRecovery(original, sourcePath, "session-a", folder);
        check("Undo-like saves refresh the same session snapshot instead of leaving the later edit",
              same == recovery && soundEvents(loadProject(same)) == soundEvents(original) &&
                  loadProject(same).staffPerformance->notes[0].source.x ==
                      original.staffPerformance->notes[0].source.x);
        const auto redone = saveStaffEditRecovery(changed, sourcePath, "session-a", folder);
        check("Redo-like saves restore edited boxes and exact sound without changing the original",
              redone == recovery && loadProject(redone).staffPerformance->notes[0].source.x == 230 &&
                  soundEvents(loadProject(redone)) == soundEvents(changed) &&
                  fileHash(sourcePath) == originalHash);
        QThread::msleep(20);
        const auto second = saveStaffEditRecovery(original, sourcePath, "session-b", folder);
        check("Separate application sessions cannot overwrite one another's recovery",
              second != recovery && QFileInfo::exists(recovery) && latestStaffEditRecovery(folder) == second);
        const auto recoveryHash = fileHash(recovery);
        auto oversized = changed;
        oversized.processing.insert("oversized", QString(1024 * 1024, 'x'));
        check("Oversized metadata failure preserves the previously committed package and original bytes",
              rejects([&] { saveStaffEditRecovery(oversized, sourcePath, "session-a", folder); }) &&
                  fileHash(recovery) == recoveryHash && fileHash(sourcePath) == originalHash);
        const auto filesBefore = QDir(folder).entryList(QDir::Files);
        check("A failed new snapshot leaves no partial recovery package",
              rejects([&] { saveStaffEditRecovery(oversized, sourcePath, "failed-new-session", folder); }) &&
                  QDir(folder).entryList(QDir::Files) == filesBefore);
        const auto blockerPath = temporary.filePath("blocked-parent");
        QFile blocker(blockerPath);
        check("The write-failure fixture exists",
              blocker.open(QIODevice::WriteOnly) && blocker.write("unchanged") == 9);
        blocker.close();
        const auto blockerHash = fileHash(blockerPath);
        check("Directory write failure is reported without modifying the blocker or source",
              rejects([&] { saveStaffEditRecovery(changed, sourcePath, "blocked", blockerPath + "/recovery"); }) &&
                  fileHash(blockerPath) == blockerHash && fileHash(sourcePath) == originalHash);
        check("Invalid session and relative-source inputs are rejected before writing",
              rejects([&] { saveStaffEditRecovery(changed, sourcePath, "../escape", folder); }) &&
                  rejects([&] { saveStaffEditRecovery(changed, "relative.jpp", "valid", folder); }));
        const auto unsaved = saveStaffEditRecovery(changed, {}, "unsaved-session", folder);
        const auto unsavedRecord = loadProject(unsaved).processing.value("staffEditRecovery").toObject();
        check("Unsaved projects have explicit missing-source identity, not an invented checksum",
              unsavedRecord.value("sourcePath").toString().isEmpty() &&
                  unsavedRecord.value("sourceSHA256").toString().isEmpty() &&
                  unsavedRecord.value("sourceHashStatus").toString() == "unsaved");
        const auto missing =
            saveStaffEditRecovery(changed, temporary.filePath("missing-original.jpp"), "missing", folder);
        check("A missing original still permits recovery while recording the checksum as unavailable",
              loadProject(missing)
                      .processing.value("staffEditRecovery")
                      .toObject()
                      .value("sourceHashStatus")
                      .toString() == "missing");
        const auto malformedFolder = temporary.filePath("malformed");
        check("The malformed-header fixture directory exists", QDir().mkpath(malformedFolder));
        QFile malformed(QDir(malformedFolder).filePath("staff-edit-" + QString(64, 'a') + ".jpp"));
        const auto excessive = qToBigEndian(quint32(16 * 1024 * 1024 + 1));
        check("The malformed-header fixture is written",
              malformed.open(QIODevice::WriteOnly) && malformed.write(projectPackageMagic()) == 8 &&
                  malformed.write(reinterpret_cast<const char *>(&excessive), 4) == 4);
        malformed.close();
        check("Latest lookup reports an oversized manifest instead of reading an unbounded payload",
              rejects([&] { latestStaffEditRecovery(malformedFolder); }));
        passed = true;
    }
    catch (const std::exception &exception)
    {
        error = QString::fromUtf8(exception.what());
    }
    return {{"passed", passed},
            {"checks", checks},
            {"error", error},
            {"fixtureDirectoryTemporary", true},
            {"recognitionRerun", false},
            {"originalProjectPreservationVerified", passed}};
}

void runStaffEditRecoveryCheck(const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(0, &app,
                       [&]
                       {
                           const auto result = checkStaffEditRecovery();
                           const auto bytes = QJsonDocument(result).toJson();
                           const auto path = args.value("report");
                           const bool directoryReady = QDir().mkpath(QFileInfo(path).absolutePath());
                           QSaveFile report(path);
                           const bool saved = directoryReady && report.open(QIODevice::WriteOnly) &&
                                              report.write(bytes) == bytes.size() && report.commit();
                           app.exit(saved ? result.value("passed").toBool() ? 0 : 3 : 4);
                       });
}
} // namespace singlilt

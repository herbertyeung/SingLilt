// Isolated regressions for application-identity migration and retained user state.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "settings/LegacyMigration.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace
{
void writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        throw std::runtime_error("Cannot create the fixture directory");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("Cannot write the fixture");
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot read the fixture");
    return file.readAll();
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    application.setOrganizationName("SingLilt");
    application.setApplicationName("SingLilt");
    int checks = 0;
    const auto check = [&checks](bool passed, const char *name)
    {
        if (!passed)
            throw std::runtime_error(name);
        ++checks;
    };
    try
    {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary directory");
        const auto root = temporary.path();
        QSettings legacy(root + "/old.ini", QSettings::IniFormat);
        QSettings current(root + "/new.ini", QSettings::IniFormat);
        legacy.setValue("ui/language", "fr_FR");
        legacy.setValue("audio/defaultVelocity", 77);
        legacy.setValue("projects/recent", QStringList{"score.jpp", "another.jpp"});
        legacy.setValue("vision/apiKey", "fixture-only");
        legacy.setValue("vision/key", "fixture-only");
        legacy.sync();
        const auto oldPreferences = readFile(legacy.fileName());
        current.setValue("audio/defaultVelocity", 99);
        current.sync();
        const auto oldData = root + "/old-data";
        const auto newData = root + "/new-data";
        const QByteArray history = "{\"schema\":1,\"attempts\":[]}";
        const QByteArray recovery("JPP\0fixture", 11);
        writeFile(oldData + "/practice-history.json", history);
        writeFile(oldData + "/staff-edit-recovery/staff-edit-fixture.jpp", recovery);
        writeFile(oldData + "/unrelated.txt", "not an application record");
        singlilt::migrateLegacyState(legacy, current, oldData, newData);
        check(current.value("ui/language").toString() == "fr_FR", "language imported");
        check(current.value("audio/defaultVelocity").toInt() == 99, "new preference wins");
        check(current.value("projects/recent").toStringList().size() == 2, "recent projects imported");
        check(!current.contains("vision/apiKey") && !current.contains("vision/key"), "credentials not imported");
        check(readFile(legacy.fileName()) == oldPreferences, "old preferences unchanged");
        check(readFile(newData + "/practice-history.json") == history, "history copied exactly");
        check(readFile(newData + "/staff-edit-recovery/staff-edit-fixture.jpp") == recovery,
              "recovery copied exactly");
        check(readFile(oldData + "/practice-history.json") == history &&
                  readFile(oldData + "/staff-edit-recovery/staff-edit-fixture.jpp") == recovery,
              "old records retained");
        check(!QFileInfo::exists(newData + "/unrelated.txt"), "unrelated files not imported");
        check(current.value("migration/legacyIdentityImported").toBool(), "completion persisted");
        current.remove("ui/language");
        current.sync();
        legacy.setValue("anotherPreference", true);
        legacy.sync();
        singlilt::migrateLegacyState(legacy, current, oldData, newData);
        check(!current.contains("ui/language") && !current.contains("anotherPreference"), "one-time import");
        QSettings reopened(current.fileName(), QSettings::IniFormat);
        check(reopened.value("audio/defaultVelocity").toInt() == 99 &&
                  reopened.value("migration/legacyIdentityImported").toBool(),
              "restart reads imported state");

        QSettings existing(root + "/existing.ini", QSettings::IniFormat);
        writeFile(root + "/existing-data/practice-history.json", "new record");
        writeFile(root + "/existing-data/staff-edit-recovery/staff-edit-fixture.jpp", "new recovery");
        singlilt::migrateLegacyState(legacy, existing, oldData, root + "/existing-data");
        check(readFile(root + "/existing-data/practice-history.json") == "new record", "new history wins");
        check(readFile(root + "/existing-data/staff-edit-recovery/staff-edit-fixture.jpp") == "new recovery",
              "new recovery wins");

        QSettings partial(root + "/partial.ini", QSettings::IniFormat);
        writeFile(root + "/partial-data/staff-edit-recovery", "not a directory");
        bool failed = false;
        try
        {
            singlilt::migrateLegacyState(legacy, partial, oldData, root + "/partial-data");
        }
        catch (const std::runtime_error &)
        {
            failed = true;
        }
        check(failed && !partial.contains("migration/legacyIdentityImported") && !partial.contains("ui/language"),
              "partial record import leaves preferences uncommitted");
        check(readFile(oldData + "/practice-history.json") == history &&
                  readFile(root + "/partial-data/practice-history.json") == history,
              "partial import keeps both completed copy and original");
        check(QFile::remove(root + "/partial-data/staff-edit-recovery"), "partial fixture repaired");
        singlilt::migrateLegacyState(legacy, partial, oldData, root + "/partial-data");
        check(partial.value("migration/legacyIdentityImported").toBool() &&
                  readFile(root + "/partial-data/staff-edit-recovery/staff-edit-fixture.jpp") == recovery,
              "partial record import resumes");

        QSettings blocked(root + "/blocked.ini", QSettings::IniFormat);
        writeFile(root + "/blocked-data", "not a directory");
        failed = false;
        try
        {
            singlilt::migrateLegacyState(legacy, blocked, oldData, root + "/blocked-data");
        }
        catch (const std::runtime_error &)
        {
            failed = true;
        }
        check(failed && !blocked.contains("migration/legacyIdentityImported"), "failed import not marked");
        check(QFile::remove(root + "/blocked-data"), "blocked fixture repaired");
        singlilt::migrateLegacyState(legacy, blocked, oldData, root + "/blocked-data");
        check(blocked.value("migration/legacyIdentityImported").toBool(), "failed import can retry");

        QSettings locked(root + "/locked.ini", QSettings::IniFormat);
        check(QDir().mkpath(root + "/locked-data"), "lock fixture directory");
        QLockFile lock(root + "/locked-data/identity-import.lock");
        check(lock.tryLock(0), "lock fixture acquired");
        failed = false;
        try
        {
            singlilt::migrateLegacyState(legacy, locked, oldData, root + "/locked-data");
        }
        catch (const std::runtime_error &)
        {
            failed = true;
        }
        check(failed && !locked.contains("ui/language"), "concurrent import is rejected");

        QSettings empty(root + "/empty.ini", QSettings::IniFormat);
        QSettings fresh(root + "/fresh.ini", QSettings::IniFormat);
        singlilt::migrateLegacyState(empty, fresh, root + "/missing", root + "/fresh-data");
        check(fresh.allKeys().isEmpty() && !QFileInfo::exists(root + "/fresh-data"), "fresh install unchanged");
        const auto applicationData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        check(applicationData.endsWith("/SingLilt/SingLilt"), "new application data identity");
        application.setOrganizationName("JianpuPlayer");
        application.setApplicationName("JianpuPlayer");
        const auto legacyApplicationData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        application.setOrganizationName("SingLilt");
        application.setApplicationName("SingLilt");
        check(legacyApplicationData == QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                                           .filePath("JianpuPlayer/JianpuPlayer"),
              "legacy data path matches Qt");
        std::cout << "Migration tests: " << checks << '/' << checks << " passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

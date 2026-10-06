// One-time import of preferences and records from the previous application identity.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "LegacyMigration.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <stdexcept>

namespace singlilt
{
namespace
{
const char *ImportComplete = "migration/legacyIdentityImported";

void copyRecord(const QString &sourcePath, const QString &targetPath)
{
    const QFileInfo sourceInfo(sourcePath);
    const QFileInfo targetInfo(targetPath);
    if (!sourceInfo.isFile() || sourceInfo.isSymLink() || targetInfo.isSymLink() ||
        QFileInfo(targetInfo.absolutePath()).isSymLink())
        throw std::runtime_error("Invalid record import path");
    if (targetInfo.exists() && !targetInfo.isFile())
        throw std::runtime_error("The record import destination is not a file");
    if (targetInfo.exists())
        return;
    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath()))
        throw std::runtime_error("Cannot create the record import directory");
    QFile source(sourcePath);
    QSaveFile target(targetPath);
    if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot open a record for import");
    while (!source.atEnd())
    {
        const auto bytes = source.read(64 * 1024);
        if (source.error() != QFileDevice::NoError || target.write(bytes) != bytes.size())
            throw std::runtime_error("Cannot copy a record during import");
    }
    if (!target.commit())
        throw std::runtime_error("Cannot commit an imported record");
}
} // namespace

void migrateLegacyState(QSettings &legacySettings, QSettings &settings, const QString &legacyDataDirectory,
                        const QString &dataDirectory)
{
    legacySettings.setFallbacksEnabled(false);
    settings.setFallbacksEnabled(false);
    legacySettings.sync();
    settings.sync();
    if (legacySettings.status() != QSettings::NoError || settings.status() != QSettings::NoError)
        throw std::runtime_error("Cannot read preferences for import");
    if (settings.value(ImportComplete, false).toBool())
        return;

    const auto keys = legacySettings.allKeys();
    const QDir legacyDirectory(legacyDataDirectory);
    QStringList records;
    if (QFileInfo::exists(legacyDirectory.filePath("practice-history.json")))
        records.append("practice-history.json");
    const QDir recovery(legacyDirectory.filePath("staff-edit-recovery"));
    const QFileInfo recoveryInfo(recovery.absolutePath());
    if (recoveryInfo.exists() && (!recoveryInfo.isDir() || !recoveryInfo.isReadable() || recoveryInfo.isSymLink()))
        throw std::runtime_error("Cannot read existing staff-edit recovery records");
    for (const auto &file : recovery.entryList({"staff-edit-*.jpp"}, QDir::Files | QDir::NoSymLinks))
        records.append("staff-edit-recovery/" + file);
    if (keys.isEmpty() && records.isEmpty())
        return;

    if (QFileInfo(legacyDataDirectory).isSymLink() || QFileInfo(dataDirectory).isSymLink() ||
        !QDir().mkpath(dataDirectory))
        throw std::runtime_error("Cannot create the application import directory");
    QLockFile lock(QDir(dataDirectory).filePath("identity-import.lock"));
    if (!lock.tryLock(0))
        throw std::runtime_error("Another application instance is importing existing records");
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw std::runtime_error("Cannot refresh preferences for import");
    if (settings.value(ImportComplete, false).toBool())
        return;

    // Keep the old state intact so previous versions and failed imports remain recoverable.
    for (const auto &record : records)
        copyRecord(legacyDirectory.filePath(record), QDir(dataDirectory).filePath(record));
    for (const auto &key : keys)
    {
        if (!settings.contains(key) && key != ImportComplete && key != "vision/apiKey" && key != "vision/key")
            settings.setValue(key, legacySettings.value(key));
    }
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw std::runtime_error("Cannot save imported preferences");
    settings.setValue(ImportComplete, true);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw std::runtime_error("Cannot finish the preference import");
}

void migrateLegacyUserState()
{
    QSettings legacySettings(QSettings::NativeFormat, QSettings::UserScope, "JianpuPlayer", "JianpuPlayer");
    QSettings settings;
    const auto legacyDataDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                                         .filePath("JianpuPlayer/JianpuPlayer");
    const auto dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    migrateLegacyState(legacySettings, settings, legacyDataDirectory, dataDirectory);
}
} // namespace singlilt

// Independent recovery snapshots for staff-image edits.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffEditRecovery.h"

#include "ProjectPackage.h"
#include "ProjectStore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtEndian>
#include <stdexcept>

namespace singlilt
{
namespace
{
constexpr qint64 ManifestLimit = 16 * 1024 * 1024;
constexpr qint64 ProcessingLimit = 1024 * 1024;
constexpr int RecoveryCandidates = 16;
const char *RecoveryKey = "staffEditRecovery";

void requireRecovery(bool valid, const QString &message)
{
    if (!valid)
        throw std::runtime_error(message.toStdString());
}

bool validSession(const QString &sessionId)
{
    static const QRegularExpression pattern("^[A-Za-z0-9_-]{1,64}$");
    return pattern.match(sessionId).hasMatch();
}

QString recoveryDirectory(const QString &directory)
{
    QString path = directory;
    if (path.isEmpty())
    {
        const auto data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        requireRecovery(!data.isEmpty(), "The application has no writable data location for staff edit recovery");
        path = QDir(data).filePath("staff-edit-recovery");
    }
    requireRecovery(QDir::isAbsolutePath(path), "Staff edit recovery directory must be absolute");
    return QDir::cleanPath(path);
}

QString existingIdentity(const QString &path)
{
    const QFileInfo file(path);
    const auto canonical = file.canonicalFilePath();
    return canonical.isEmpty() ? file.absoluteFilePath() : canonical;
}

QJsonObject sourceMetadata(const QString &sourcePath)
{
    QJsonObject result{{"sourcePath", sourcePath}, {"sourceSHA256", QString()}};
    if (sourcePath.isEmpty())
    {
        result.insert("sourceHashStatus", "unsaved");
        return result;
    }
    QFile source(sourcePath);
    if (!source.exists())
    {
        result.insert("sourceHashStatus", "missing");
        return result;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!source.open(QIODevice::ReadOnly) || !hash.addData(&source))
    {
        result.insert("sourceHashStatus", "unreadable");
        return result;
    }
    result.insert("sourceSHA256", QString::fromLatin1(hash.result().toHex()));
    result.insert("sourceHashStatus", "available");
    return result;
}

QJsonObject recoveryMetadata(const QString &path)
{
    QFile file(path);
    requireRecovery(file.open(QIODevice::ReadOnly), "Cannot read staff edit recovery: " + path);
    requireRecovery(file.read(8) == projectPackageMagic(), "Invalid staff edit recovery package: " + path);
    const auto length = file.read(4);
    requireRecovery(length.size() == 4, "Incomplete staff edit recovery header: " + path);
    const auto size = qFromBigEndian<quint32>(length.constData());
    requireRecovery(size > 0 && size <= ManifestLimit,
                    "Staff edit recovery manifest exceeds its size limit: " + path);
    const auto bytes = file.read(size);
    requireRecovery(bytes.size() == size, "Incomplete staff edit recovery manifest: " + path);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    requireRecovery(error.error == QJsonParseError::NoError && document.isObject(),
                    "Invalid staff edit recovery manifest: " + path);
    const auto processing = document.object().value("processing");
    requireRecovery(processing.isObject() &&
                        QJsonDocument(processing.toObject()).toJson(QJsonDocument::Compact).size() <=
                            ProcessingLimit,
                    "Invalid staff edit recovery processing metadata: " + path);
    const auto value = processing.toObject().value(RecoveryKey);
    requireRecovery(value.isObject(), "Missing staff edit recovery metadata: " + path);
    const auto record = value.toObject();
    const auto source = record.value("sourcePath").toString();
    const auto status = record.value("sourceHashStatus").toString();
    const auto hash = record.value("sourceSHA256").toString();
    static const QRegularExpression checksum("^[0-9a-f]{64}$");
    requireRecovery(
        record.value("version").toDouble() == 1 && validSession(record.value("sessionId").toString()) &&
            record.value("sourcePath").isString() && (source.isEmpty() || QDir::isAbsolutePath(source)) &&
            record.value("recoveryPath").isString() &&
            QDir::isAbsolutePath(record.value("recoveryPath").toString()) &&
            record.value("sourceSHA256").isString() &&
            ((status == "available" && checksum.match(hash).hasMatch() && !source.isEmpty()) ||
             (status == "unsaved" && hash.isEmpty() && source.isEmpty()) ||
             ((status == "missing" || status == "unreadable") && hash.isEmpty() && !source.isEmpty())) &&
            QDateTime::fromString(record.value("updatedAtUtc").toString(), Qt::ISODateWithMs).isValid(),
        "Invalid staff edit recovery record: " + path);
    return record;
}
} // namespace

QString saveStaffEditRecovery(const Project &project, const QString &sourcePath, const QString &sessionId,
                              const QString &directory)
{
    requireRecovery(validSession(sessionId),
                    "Staff edit recovery session ID must be a 1-64 character ASCII token");
    requireRecovery(sourcePath.isEmpty() || QDir::isAbsolutePath(sourcePath),
                    "Staff edit recovery source path must be absolute or empty");
    const auto source =
        sourcePath.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(sourcePath).absoluteFilePath());
    const auto folder = recoveryDirectory(directory);
    QByteArray identity = source.toCaseFolded().toUtf8();
    identity.append('\n');
    identity.append(sessionId.toUtf8());
    const auto key = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    const auto path = QDir(folder).filePath("staff-edit-" + key + ".jpp");
    requireRecovery(!QFileInfo(path).isSymLink(), "Staff edit recovery destination must not be a symbolic link");
    requireRecovery(source.isEmpty() ||
                        existingIdentity(source).compare(existingIdentity(path), Qt::CaseInsensitive) != 0,
                    "Staff edit recovery must not overwrite its source project");
    auto snapshot = project;
    auto record = sourceMetadata(source);
    record.insert("version", 1);
    record.insert("sessionId", sessionId);
    record.insert("recoveryPath", path);
    record.insert("updatedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    snapshot.processing.insert(RecoveryKey, record);
    requireRecovery(QJsonDocument(snapshot.processing).toJson(QJsonDocument::Compact).size() <= ProcessingLimit,
                    "Staff edit recovery processing metadata exceeds its size limit");
    requireRecovery(QDir().mkpath(folder), "Cannot create staff edit recovery directory: " + folder);
    // Package resources and recovery metadata commit together, never into the source file.
    saveProject(path, snapshot);
    return path;
}

QString latestStaffEditRecovery(const QString &directory)
{
    const auto folder = recoveryDirectory(directory);
    const QFileInfo info(folder);
    if (!info.exists())
        return {};
    requireRecovery(info.isDir() && info.isReadable(), "Cannot read staff edit recovery directory: " + folder);
    const auto candidates =
        QDir(folder).entryInfoList({"staff-edit-*.jpp"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
    QString latest;
    QDateTime newest;
    static const QRegularExpression filename("^staff-edit-[0-9a-f]{64}\\.jpp$");
    int inspected = 0;
    for (const auto &file : candidates)
    {
        if (!filename.match(file.fileName()).hasMatch())
            continue;
        if (++inspected > RecoveryCandidates)
            break;
        const auto record = recoveryMetadata(file.absoluteFilePath());
        const auto updated = QDateTime::fromString(record.value("updatedAtUtc").toString(), Qt::ISODateWithMs);
        if (latest.isEmpty() || updated > newest)
        {
            newest = updated;
            latest = file.absoluteFilePath();
        }
    }
    return latest;
}
} // namespace singlilt

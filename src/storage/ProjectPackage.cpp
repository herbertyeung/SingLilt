// Self-contained JPP containers and checked resource hashes.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ProjectPackage.h"
#include "ProjectStore.h"
#include "StaffPagesStore.h"
#include "i18n/LanguageManager.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace singlilt
{
namespace
{
constexpr qint64 ManifestLimit = 16 * 1024 * 1024;
constexpr qint64 ImageLimit = 64 * 1024 * 1024;
constexpr qint64 MediaLimit = 2LL * 1024 * 1024 * 1024;
constexpr qint64 PackageLimit = 7LL * 1024 * 1024 * 1024;
const std::array<const char *, 4> Roles{"image", "original", "vocals", "instrumental"};
const std::array<const char *, 3> AudioFields{"path", "vocalsPath", "instrumentalPath"};

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(trText(message).toStdString());
}
QByteArray hashFile(QFile &file)
{
    require(file.seek(0), "messages.package.read_resource");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "messages.package.read_resource");
    require(file.seek(0), "messages.package.read_resource");
    return hash.result().toHex();
}
bool validResourceName(const QString &name)
{
    return !name.isEmpty() && name.size() <= 255 && !name.contains('/') && !name.contains('\\') &&
           !name.contains(':') && !name.contains(QChar(0));
}
struct Resource
{
    QString role, name, extension;
    qint64 size = 0;
    QByteArray hash, image;
    std::shared_ptr<QFile> file;
};
bool isImageRole(const QString &role)
{
    return role == "image" || role.startsWith("page-source-") || role.startsWith("page-score-");
}
bool validRole(const QString &role, int schema)
{
    if (std::find(Roles.begin(), Roles.end(), role) != Roles.end())
        return true;
    static const QRegularExpression pageRole(
        "^page-(source-(?:[0-9]|[12][0-9]|3[01])|score-(?:[1-9]|[12][0-9]|3[01]))$");
    return (schema == 5 || schema == 6) && pageRole.match(role).hasMatch();
}
Resource imageResource(const QString &role, const QImage &picture)
{
    Resource resource;
    resource.role = role;
    resource.name = role + ".png";
    resource.extension = "png";
    QBuffer buffer(&resource.image);
    require(buffer.open(QIODevice::WriteOnly) && picture.save(&buffer, "PNG"), "messages.storage.encode_image");
    resource.size = resource.image.size();
    require(resource.size > 0 && resource.size <= ImageLimit, "messages.storage.invalid_image");
    resource.hash = QCryptographicHash::hash(resource.image, QCryptographicHash::Sha256).toHex();
    return resource;
}
QJsonObject resourceJson(const Resource &resource)
{
    return {{"role", resource.role},
            {"name", resource.name},
            {"extension", resource.extension},
            {"size", QString::number(resource.size)},
            {"sha256", QString::fromLatin1(resource.hash)}};
}
void copyResource(QIODevice &source, QIODevice &destination, qint64 size, const QByteArray &expectedHash)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 remaining = size;
    while (remaining > 0)
    {
        const auto bytes = source.read(std::min<qint64>(remaining, 1024 * 1024));
        require(!bytes.isEmpty(), "messages.package.read_resource");
        require(destination.write(bytes) == bytes.size(), "messages.package.write_resource");
        hash.addData(bytes);
        remaining -= bytes.size();
    }
    require(hash.result().toHex() == expectedHash, "messages.package.checksum");
}
void preserveLegacy(const QString &path)
{
    QFile source(path);
    if (!source.exists())
        return;
    require(source.open(QIODevice::ReadOnly), "messages.storage.open_project");
    if (source.peek(8) == projectPackageMagic() || source.size() > ImageLimit)
        return;
    const auto document = QJsonDocument::fromJson(source.readAll());
    const int schema = document.object().value("schemaVersion").toInt();
    if (!document.isObject() || schema < 1 || schema > 3)
        return;
    const auto hash = hashFile(source);
    for (int suffix = 0; suffix < 1000; ++suffix)
    {
        const QString backupPath =
            path + ".legacy" + (suffix ? "." + QString::number(suffix) : QString()) + ".bak";
        QFile existing(backupPath);
        if (existing.exists())
        {
            require(existing.open(QIODevice::ReadOnly), "messages.package.backup");
            if (hashFile(existing) == hash)
                return;
            continue;
        }
        QSaveFile backup(backupPath);
        require(backup.open(QIODevice::WriteOnly), "messages.package.backup");
        copyResource(source, backup, source.size(), hash);
        require(backup.commit(), "messages.package.backup");
        return;
    }
    require(false, "messages.package.backup");
}
} // namespace

QByteArray projectPackageMagic()
{
    return QByteArray("JPP4\r\n\x1a\n", 8);
}
void saveProjectPackage(const QString &path, const Project &project)
{
    auto manifest = projectToJson(project);
    std::vector<Resource> resources;
    Resource image;
    image.role = "image";
    image.name = "score.png";
    image.extension = "png";
    QBuffer imageBuffer(&image.image);
    require(imageBuffer.open(QIODevice::WriteOnly) && project.image.save(&imageBuffer, "PNG"),
            "messages.storage.encode_image");
    image.size = image.image.size();
    require(image.size > 0 && image.size <= ImageLimit, "messages.storage.invalid_image");
    image.hash = QCryptographicHash::hash(image.image, QCryptographicHash::Sha256).toHex();
    resources.push_back(std::move(image));
    for (std::size_t index = 0; index < project.staffPages.size(); ++index)
    {
        const auto &page = project.staffPages[index];
        if (!page.sourceImage.isNull())
            resources.push_back(imageResource(staffSourceRole(int(index)), page.sourceImage));
        if (index != 0)
            resources.push_back(imageResource(staffRenderedRole(int(index)), page.renderedImage));
    }
    auto audio = manifest.value("audioSource").toObject();
    for (size_t i = 0; i < AudioFields.size(); ++i)
    {
        const QString sourcePath = audio.value(AudioFields[i]).toString();
        if (sourcePath.isEmpty())
            continue;
        Resource resource;
        resource.role = Roles[i + 1];
        resource.extension = QFileInfo(sourcePath).suffix().toLower();
        require(resource.extension == "mp3" || resource.extension == "wav", "messages.package.media_format");
        resource.name = project.resourceNames.value(resource.role, QFileInfo(sourcePath).fileName());
        require(validResourceName(resource.name), "messages.package.invalid_metadata");
        resource.file = std::make_shared<QFile>(sourcePath);
        require(resource.file->open(QIODevice::ReadOnly), "messages.package.missing_media");
        resource.size = resource.file->size();
        require(resource.size > 0 && resource.size <= MediaLimit, "messages.package.resource_limit");
        resource.hash = hashFile(*resource.file);
        audio.insert(AudioFields[i], "asset:" + resource.role);
        resources.push_back(std::move(resource));
    }
    if (project.audioSource)
        manifest.insert("audioSource", audio);
    manifest.insert("imageAsset", "asset:image");
    QJsonArray table;
    for (const auto &resource : resources)
        table.append(resourceJson(resource));
    manifest.insert("resources", table);
    const auto bytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    require(bytes.size() <= ManifestLimit, "messages.package.manifest_limit");
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), "messages.storage.create_project");
    const quint32 size = qToBigEndian(quint32(bytes.size()));
    require(file.write(projectPackageMagic()) == 8 && file.write(reinterpret_cast<const char *>(&size), 4) == 4 &&
                file.write(bytes) == bytes.size(),
            "messages.storage.save_project");
    for (auto &resource : resources)
    {
        if (resource.file)
        {
            require(resource.file->size() == resource.size, "messages.package.checksum");
            copyResource(*resource.file, file, resource.size, resource.hash);
        }
        else
            require(file.write(resource.image) == resource.size, "messages.package.write_resource");
    }
    preserveLegacy(path);
    require(file.commit(), "messages.storage.save_project");
}
Project loadProjectPackage(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "messages.storage.open_project");
    require(file.size() >= 12 && file.size() <= PackageLimit && file.read(8) == projectPackageMagic(),
            "messages.package.invalid_container");
    const auto lengthBytes = file.read(4);
    const quint32 length = qFromBigEndian<quint32>(lengthBytes.constData());
    require(length > 0 && length <= ManifestLimit && length <= file.size() - 12,
            "messages.package.manifest_limit");
    QJsonParseError error;
    auto manifest = QJsonDocument::fromJson(file.read(length), &error).object();
    const int schema = manifest.value("schemaVersion").toInt();
    require(error.error == QJsonParseError::NoError && (schema == 4 || schema == 5 || schema == 6) &&
                manifest.value("schemaVersion").toDouble() == schema &&
                manifest.value("imageAsset").toString() == "asset:image" && manifest.value("resources").isArray(),
            "messages.package.invalid_container");
    const auto table = manifest.value("resources").toArray();
    require(table.size() >= 1 && table.size() <= (schema >= 5 ? 67 : 4), "messages.package.resource_limit");
    std::vector<Resource> resources;
    QMap<QString, QString> paths, names;
    qint64 totalSize = 12 + length;
    const QRegularExpression hashPattern("^[0-9a-f]{64}$");
    const QRegularExpression sizePattern("^[1-9][0-9]{0,10}$");
    for (const auto &entry : table)
    {
        require(entry.isObject(), "messages.package.invalid_container");
        const auto object = entry.toObject();
        Resource resource;
        resource.role = object.value("role").toString();
        resource.name = object.value("name").toString();
        resource.extension = object.value("extension").toString();
        const auto sizeText = object.value("size").toString();
        resource.hash = object.value("sha256").toString().toLatin1();
        require(validRole(resource.role, schema) && !names.contains(resource.role) &&
                    validResourceName(resource.name) && sizePattern.match(sizeText).hasMatch() &&
                    hashPattern.match(QString::fromLatin1(resource.hash)).hasMatch(),
                "messages.package.invalid_container");
        resource.size = sizeText.toLongLong();
        const bool image = isImageRole(resource.role);
        require(
            resource.size > 0 && resource.size <= (image ? ImageLimit : MediaLimit) &&
                (image ? resource.extension == "png" : resource.extension == "mp3" || resource.extension == "wav"),
            "messages.package.resource_limit");
        totalSize += resource.size;
        names.insert(resource.role, resource.name);
        resources.push_back(std::move(resource));
    }
    require(names.contains("image") && totalSize == file.size(), "messages.package.invalid_container");
    const auto pageMetadata = manifest.value("staffPages").toArray();
    QMap<QString, bool> referencedPageRoles;
    if (schema >= 5)
    {
        require(manifest.value("staffPages").isArray() && !pageMetadata.isEmpty() && pageMetadata.size() <= 32,
                "messages.pages.invalid_project");
        for (int index = 0; index < pageMetadata.size(); ++index)
        {
            require(pageMetadata[index].isObject(), "messages.pages.invalid_project");
            const auto page = pageMetadata[index].toObject();
            const auto source = page.value("sourceAsset").toString();
            const auto rendered = page.value("renderedAsset").toString();
            const QString sourceRole = staffSourceRole(index);
            const QString renderedRole = staffRenderedRole(index);
            require((source.isEmpty() && !names.contains(sourceRole)) ||
                        (source == "asset:" + sourceRole && names.contains(sourceRole)),
                    "messages.package.invalid_reference");
            require(rendered == "asset:" + renderedRole && names.contains(renderedRole),
                    "messages.package.invalid_reference");
            if (!source.isEmpty())
                referencedPageRoles.insert(sourceRole, true);
            if (index != 0)
                referencedPageRoles.insert(renderedRole, true);
        }
        for (auto it = names.cbegin(); it != names.cend(); ++it)
            if (it.key().startsWith("page-"))
                require(referencedPageRoles.contains(it.key()), "messages.package.invalid_reference");
    }
    else
        require(!manifest.contains("staffPages"), "messages.pages.invalid_project");
    auto audio = manifest.value("audioSource").toObject();
    for (size_t i = 0; i < AudioFields.size(); ++i)
    {
        const auto reference = audio.value(AudioFields[i]).toString();
        const QString role = Roles[i + 1];
        require(names.contains(role) ? reference == "asset:" + role : reference.isEmpty(),
                "messages.package.invalid_reference");
    }
    auto directory = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/SingLilt-media-XXXXXX");
    require(directory->isValid(), "messages.package.cache");
    QImage image;
    QMap<QString, QImage> pageImages;
    qint64 totalPixels = 0;
    for (const auto &resource : resources)
    {
        // Only fixed roles/extensions enter a path; user filenames are display metadata.
        const auto extractedPath = directory->filePath(resource.role + "." + resource.extension);
        QFile extracted(extractedPath);
        require(extracted.open(QIODevice::WriteOnly), "messages.package.cache");
        copyResource(file, extracted, resource.size, resource.hash);
        extracted.close();
        paths.insert(resource.role, extractedPath);
        if (isImageRole(resource.role))
        {
            QImageReader reader(extractedPath, "PNG");
            const auto size = reader.size();
            require(size.width() > 0 && size.height() > 0 && size.width() <= 12000 && size.height() <= 20000 &&
                        qint64(size.width()) * size.height() <= 50000000,
                    "messages.storage.invalid_image");
            totalPixels += qint64(size.width()) * size.height();
            require(totalPixels <= 400000000, "messages.pages.invalid_project");
            const auto picture = reader.read();
            require(!picture.isNull(), "messages.storage.invalid_image");
            if (resource.role == "image")
                image = picture;
            pageImages.insert(resource.role, picture);
        }
    }
    for (size_t i = 0; i < AudioFields.size(); ++i)
        audio.insert(AudioFields[i], paths.value(Roles[i + 1]));
    if (manifest.contains("audioSource"))
        manifest.insert("audioSource", audio);
    std::vector<StaffPage> pages;
    for (int index = 0; index < pageMetadata.size(); ++index)
    {
        const auto object = pageMetadata[index].toObject();
        const auto start = object.value("startTick");
        const auto end = object.value("endTick");
        require(start.isDouble() && end.isDouble() && std::isfinite(start.toDouble()) &&
                    std::isfinite(end.toDouble()) && std::floor(start.toDouble()) == start.toDouble() &&
                    std::floor(end.toDouble()) == end.toDouble() && start.toDouble() >= 0 &&
                    end.toDouble() > start.toDouble() && end.toDouble() <= 1000000000,
                "messages.pages.invalid_project");
        pages.push_back({object.value("label").toString(), pageImages.value(staffSourceRole(index)),
                         pageImages.value(staffRenderedRole(index)), std::int64_t(start.toDouble()),
                         std::int64_t(end.toDouble())});
    }
    auto project = projectFromJson(manifest, image, pages);
    project.score.imagePath = path.toStdString();
    project.resourceNames = names;
    project.mediaDirectory = std::move(directory);
    return project;
}
} // namespace singlilt

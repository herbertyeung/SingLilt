// Staff-page resource and time-range serialization.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffPagesStore.h"
#include "i18n/LanguageManager.h"
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
void requirePages(bool condition)
{
    if (!condition)
        throw std::runtime_error(trText("messages.pages.invalid_project").toStdString());
}
bool validImage(const QImage &image)
{
    return !image.isNull() && image.width() > 0 && image.height() > 0 && image.width() <= 12000 &&
           image.height() <= 20000 && qint64(image.width()) * image.height() <= 50000000;
}
bool insideImage(const SourceRect &rect, const QImage &image, bool originalImage)
{
    const int tolerance = originalImage ? 0 : 2;
    return rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 && std::isfinite(rect.x) &&
           std::isfinite(rect.y) && std::isfinite(rect.width) && std::isfinite(rect.height) &&
           rect.x + rect.width <= image.width() + tolerance && rect.y + rect.height <= image.height() + tolerance;
}
bool emptyAnchor(const SourceRect &rect)
{
    return rect.x == 0 && rect.y == 0 && rect.width == 0 && rect.height == 0;
}
} // namespace

QString staffSourceRole(int pageIndex)
{
    return QString("page-source-%1").arg(pageIndex);
}
QString staffRenderedRole(int pageIndex)
{
    return pageIndex == 0 ? QStringLiteral("image") : QString("page-score-%1").arg(pageIndex);
}
void validateStaffPages(const Project &project)
{
    if (project.staffImagePlayback)
        requirePages(!project.staffPages.empty() && !project.generatedNotation &&
                     project.notationStyle == NotationStyle::Staff && project.staffPerformance);
    if (project.staffPages.empty())
    {
        for (const auto &note : project.score.notes)
            requirePages(note.pageIndex == 0);
        if (project.staffPerformance)
            for (const auto &note : project.staffPerformance->notes)
                requirePages(note.pageIndex == 0);
        return;
    }
    requirePages(project.staffPages.size() <= 32 && project.staffPerformance &&
                 (project.generatedNotation || project.staffImagePlayback));
    std::int64_t previousEnd = 0;
    qint64 totalPixels = 0;
    for (const auto &page : project.staffPages)
    {
        requirePages(!page.label.isEmpty() && page.label.size() <= 255 && !page.label.contains(QChar(0)) &&
                     validImage(page.renderedImage) &&
                     (page.sourceImage.isNull() || validImage(page.sourceImage)) &&
                     page.startTick == previousEnd && page.endTick > page.startTick && page.endTick <= 1000000000);
        previousEnd = page.endTick;
        if (project.staffImagePlayback)
            requirePages(validImage(page.sourceImage) && page.renderedImage == page.sourceImage);
        totalPixels += qint64(page.renderedImage.width()) * page.renderedImage.height();
        if (!page.sourceImage.isNull())
            totalPixels += qint64(page.sourceImage.width()) * page.sourceImage.height();
        requirePages(totalPixels <= 400000000);
    }
    requirePages(previousEnd == project.staffPerformance->durationTicks &&
                 project.image == project.staffPages.front().renderedImage);
    requirePages(!project.score.writtenMeasures.empty());
    for (std::size_t index = 0; index < project.staffPages.size(); ++index)
    {
        std::int64_t first = -1, end = -1;
        for (const auto &measure : project.score.writtenMeasures)
            if (measure.pageIndex == int(index))
            {
                if (first < 0)
                    first = measure.startTick;
                end = measure.startTick + measure.durationTicks;
            }
        requirePages(first == project.staffPages[index].startTick && end == project.staffPages[index].endTick);
    }
    for (const auto &note : project.score.notes)
    {
        requirePages(note.pageIndex >= 0 && std::size_t(note.pageIndex) < project.staffPages.size());
        requirePages(note.hasImageAnchor
                         ? insideImage(note.source, project.staffPages[std::size_t(note.pageIndex)].renderedImage,
                                       project.staffImagePlayback)
                         : emptyAnchor(note.source));
    }
    for (const auto &note : project.staffPerformance->notes)
    {
        requirePages(note.pageIndex >= 0 && std::size_t(note.pageIndex) < project.staffPages.size());
        const auto &page = project.staffPages[std::size_t(note.pageIndex)];
        requirePages(note.startTick >= page.startTick && note.startTick < page.endTick &&
                     (note.hasImageAnchor
                          ? insideImage(note.source, page.renderedImage, project.staffImagePlayback)
                          : emptyAnchor(note.source)));
    }
}
QJsonArray staffPagesToJson(const Project &project)
{
    validateStaffPages(project);
    QJsonArray pages;
    for (std::size_t index = 0; index < project.staffPages.size(); ++index)
    {
        const auto &page = project.staffPages[index];
        pages.append(QJsonObject{
            {"label", page.label},
            {"startTick", double(page.startTick)},
            {"endTick", double(page.endTick)},
            {"sourceAsset", page.sourceImage.isNull() ? QString{} : "asset:" + staffSourceRole(int(index))},
            {"renderedAsset", "asset:" + staffRenderedRole(int(index))}});
    }
    return pages;
}
void validateStaffPageMetadata(const QJsonArray &metadata, const std::vector<StaffPage> &pages)
{
    requirePages(metadata.size() == int(pages.size()) && metadata.size() <= 32);
    for (int index = 0; index < metadata.size(); ++index)
    {
        requirePages(metadata[index].isObject());
        const auto object = metadata[index].toObject();
        const auto &page = pages[std::size_t(index)];
        requirePages(object.value("label").isString() && object.value("label").toString() == page.label &&
                     object.value("startTick").isDouble() && object.value("endTick").isDouble() &&
                     object.value("startTick").toDouble() == double(page.startTick) &&
                     object.value("endTick").toDouble() == double(page.endTick) &&
                     object.value("sourceAsset").isString() && object.value("renderedAsset").isString() &&
                     object.value("sourceAsset").toString() ==
                         (page.sourceImage.isNull() ? QString{} : "asset:" + staffSourceRole(index)) &&
                     object.value("renderedAsset").toString() == "asset:" + staffRenderedRole(index));
    }
}
} // namespace singlilt

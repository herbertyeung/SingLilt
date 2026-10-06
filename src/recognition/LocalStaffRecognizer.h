// Local staff recognition and candidate construction.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "StaffPageInput.h"
#include "storage/ProjectStore.h"
#include <QStringList>
#include <atomic>
#include <limits>

namespace singlilt
{
struct LocalStaffRecognitionOptions
{
    QString engineExecutable;
    QString modelPath;
    int timeoutSeconds = 180;
    std::size_t primaryTrackIndex = std::numeric_limits<std::size_t>::max();
    QStringList engineArgumentsPrefix;
};

struct LocalStaffRecognitionResult
{
    std::optional<Project> project;
    QImage originalImage;
    std::vector<QImage> originalImages;
    QString error;
    QString engineLog;
    QByteArray sourceNotation;
    bool cancelled = false;
    bool valid() const;
};

// Routing hint for clean printed pages; recognition still validates the full OMR result.
bool hasStaffLines(const QImage &image);

// Synchronous worker entry. The caller keeps cancellation alive until it returns.
LocalStaffRecognitionResult recognizeLocalStaff(const QImage &image, const QString &imagePath,
                                                const LocalStaffRecognitionOptions &options = {},
                                                const std::atomic_bool *cancellation = nullptr);
LocalStaffRecognitionResult recognizeLocalStaffPages(const std::vector<StaffPageInput> &pages,
                                                     const LocalStaffRecognitionOptions &options = {},
                                                     const std::atomic_bool *cancellation = nullptr);
} // namespace singlilt

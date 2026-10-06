// Local runtime, model, and sound-bank readiness checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

namespace singlilt
{
struct AppSettings;
struct AudioTranscriptionOptions;

struct CapabilityFileStatus
{
    bool filesPresent = true;
    QStringList checkedFiles;
    QStringList missingFiles;
    QStringList issues;
};

struct CapabilityStatus
{
    CapabilityFileStatus piano;
    CapabilityFileStatus gm;
    CapabilityFileStatus whisper;
    CapabilityFileStatus separation;
};

// Checks local files and small metadata only; does not load models or run inference.
CapabilityStatus inspectCapabilities(const AppSettings &settings, const QString &appDirectory = {});
QString capabilityStatusText(const CapabilityFileStatus &status);
QString audioImportCapabilityError(const AudioTranscriptionOptions &options, const QString &appDirectory = {});
} // namespace singlilt

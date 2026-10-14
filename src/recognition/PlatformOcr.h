// SPDX-License-Identifier: MIT
#pragma once
#include "WindowsOcr.h"
namespace singlilt
{
OcrText recognizeText(const QImage &image, const QString &language = QStringLiteral("zh-Hans-CN"));
#ifdef Q_OS_MACOS
OcrText recognizeMacText(const QImage &image, const QString &language);
#endif
} // namespace singlilt

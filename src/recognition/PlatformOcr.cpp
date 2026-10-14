// SPDX-License-Identifier: MIT
#include "PlatformOcr.h"
namespace singlilt
{
OcrText recognizeText(const QImage &image, const QString &language)
{
#ifdef Q_OS_MACOS
    return recognizeMacText(image, language);
#else
    return recognizeWindowsText(image, language);
#endif
}
} // namespace singlilt

// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
namespace singlilt
{
inline QString notationTextFontFamily()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("PingFang SC");
#else
    return QStringLiteral("Microsoft YaHei UI");
#endif
}
} // namespace singlilt

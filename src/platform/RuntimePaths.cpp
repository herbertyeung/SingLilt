// SPDX-License-Identifier: MIT
#include "RuntimePaths.h"
#include <QCoreApplication>
#include <QDir>
namespace singlilt
{
QString runtimeResourceDirectory(const QString &directory)
{
    QString path = QDir::cleanPath(directory);
    if (path.endsWith("/Contents/MacOS"))
    {
        path.chop(5);
        return path + "Resources";
    }
    return path;
}
QString runtimeResourceDirectory()
{
    return runtimeResourceDirectory(QCoreApplication::applicationDirPath());
}
QString helperExecutableName(const QString &name)
{
#ifdef Q_OS_WIN
    return name + ".exe";
#else
    return name;
#endif
}
} // namespace singlilt

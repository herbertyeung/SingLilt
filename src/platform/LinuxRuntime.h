// Linux distribution sound-bank discovery.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QFileInfo>
#include <QString>

namespace singlilt
{
inline QString linuxGmSoundFontPath()
{
    for (const auto *path : {"/usr/share/sounds/sf2/FluidR3_GM.sf2", "/usr/share/sounds/sf2/default-GM.sf2",
                             "/usr/share/soundfonts/FluidR3_GM.sf2", "/usr/share/soundfonts/default.sf2"})
    {
        const QFileInfo bank(QString::fromLatin1(path));
        if (bank.isFile() && bank.isReadable() && bank.size() > 0)
            return QString::fromLatin1(path);
    }
    return {};
}

inline QString linuxPianoSoundFontPath(const QString &directory)
{
    const QString bundled = directory + "/assets/soundfonts/Salamander.sf2";
    const QFileInfo bank(bundled);
    if (bank.isFile() && bank.isReadable() && bank.size() > 0)
        return bundled;
    const QString system = linuxGmSoundFontPath();
    return system.isEmpty() ? bundled : system;
}
} // namespace singlilt

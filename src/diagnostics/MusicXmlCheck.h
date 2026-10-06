// MusicXML import, timing, and voice-preservation regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
#include <QString>
class QApplication;
class QCommandLineParser;

namespace singlilt
{
QJsonObject checkMusicXmlImport(const QString &fixtureDirectory);
void runMusicXmlCheck(const QCommandLineParser &args, QApplication &app);
} // namespace singlilt

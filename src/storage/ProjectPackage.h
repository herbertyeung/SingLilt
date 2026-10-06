// Self-contained JPP containers and checked resource hashes.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include <QByteArray>
#include <QString>

namespace singlilt
{
struct Project;
QByteArray projectPackageMagic();
void saveProjectPackage(const QString &path, const Project &project);
Project loadProjectPackage(const QString &path);
} // namespace singlilt

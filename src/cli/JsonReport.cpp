// Atomic JSON report output with checked writes.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "JsonReport.h"
#include "i18n/LanguageManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <stdexcept>

namespace singlilt
{
void writeJsonReport(const QString &path, const QJsonObject &report)
{
    QSaveFile file(path);
    const QByteArray json = QJsonDocument(report).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
        throw std::runtime_error(trText("app.report_error").toStdString());
}
} // namespace singlilt

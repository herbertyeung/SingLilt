// Practice-summary persistence and bounded history.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace singlilt
{
class PracticeHistory final
{
  public:
    explicit PracticeHistory(QString path);
    void load();
    void append(QJsonObject attempt);
    const QJsonArray &attempts() const
    {
        return attempts_;
    }
    const QString &path() const
    {
        return path_;
    }

  private:
    QString path_;
    QJsonArray attempts_;
};
} // namespace singlilt

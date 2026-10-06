// Application appearance and system color-scheme changes.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>

class QApplication;

namespace singlilt
{
enum class ThemeMode;

class ThemeManager final : public QObject
{
  public:
    explicit ThemeManager(QApplication &application);
    void setMode(ThemeMode mode);
    ThemeMode mode() const;
    bool isDark() const;

  private:
    void applyPalette();
    QApplication &application_;
    ThemeMode mode_;
    QString stylesheet_;
};
} // namespace singlilt

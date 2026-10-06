// Translation catalogs, language selection, and message lookup.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

namespace singlilt
{

// Immutable QTranslator snapshots back each installed language. Call language
// changes from the GUI/application thread; translation reads may be concurrent.
class LanguageManager final : public QObject
{
  public:
    explicit LanguageManager(QObject *parent = nullptr);
    explicit LanguageManager(const QString &directory, QObject *parent = nullptr);
    ~LanguageManager() override;
    LanguageManager(const LanguageManager &) = delete;
    LanguageManager &operator=(const LanguageManager &) = delete;

    // Reads installed language folders on construction. Unknown/invalid locales
    // preserve the current translator; missing keys use the configured fallback.
    bool setLanguage(QString locale);
    QString language() const;
    QStringList availableLanguages() const;
    QString defaultLanguage() const;
    QString fallbackLanguage() const;
    QString languageName(const QString &locale) const;
    static QString catalogDirectory();
    static QString configuredDefaultLanguage();
    static bool isInstalledLanguage(const QString &locale);
    QString errorString() const;
    // Reports malformed resources, duplicate keys, missing keys and mismatched
    // placeholders. Missing individual translations do not prevent fallback.
    QStringList validateCatalogs() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

QString trText(const char *key);
// Only pass application diagnostics here, never lyrics or imported prose.
// Accepts a resource key or a legacy rendered message from any compiled catalog.
QString localizeMessage(const QString &message);

} // namespace singlilt

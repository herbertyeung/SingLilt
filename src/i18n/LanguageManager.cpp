// Translation catalogs, language selection, and message lookup.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "LanguageManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSet>
#include <QStringConverter>
#include <QThread>
#include <QTranslator>
#include <algorithm>
#include <utility>
#include <vector>

namespace singlilt
{
namespace
{

using Catalog = QMap<QString, QString>;
const QRegularExpression Placeholder(QStringLiteral("%([1-9])"));

struct Catalogs
{
    QMap<QString, Catalog> languages;
    QMap<QString, QStringList> localeErrors;
    QStringList errors;
    QStringList missing;
    QString defaultLocale;
    QString fallbackLocale;
};

QString localeKey(QString locale)
{
    return locale.trimmed().toLower().replace('-', '_');
}

QString normalizedLocale(const QString &requested, const Catalogs &catalogs)
{
    const auto key = localeKey(requested);
    for (auto it = catalogs.languages.cbegin(); it != catalogs.languages.cend(); ++it)
        if (localeKey(it.key()) == key)
            return it.key();
    if (key.contains('_'))
        return {};
    QStringList matches;
    for (auto it = catalogs.languages.cbegin(); it != catalogs.languages.cend(); ++it)
        if (localeKey(it.key()).section('_', 0, 0) == key)
            matches.append(it.key());
    for (const auto &preferred : {catalogs.defaultLocale, catalogs.fallbackLocale})
        if (matches.contains(preferred))
            return preferred;
    return matches.size() == 1 ? matches.front() : QString{};
}

QMap<QString, int> placeholders(const QString &text)
{
    QMap<QString, int> result;
    auto matches = Placeholder.globalMatch(text);
    while (matches.hasNext())
        ++result[matches.next().captured(1)];
    return result;
}

std::shared_ptr<const Catalogs> readCatalogs(const QString &directoryPath)
{
    auto result = std::make_shared<Catalogs>();
    const QDir root(directoryPath);
    QFile configuration(root.filePath(QStringLiteral("config.json")));
    if (!configuration.open(QIODevice::ReadOnly) || configuration.size() > 64 * 1024)
    {
        result->errors.append(
            QStringLiteral("Could not read language configuration: %1.").arg(configuration.fileName()));
        return result;
    }
    QJsonParseError configError;
    const auto configDocument = QJsonDocument::fromJson(configuration.readAll(), &configError);
    if (configError.error != QJsonParseError::NoError || !configDocument.isObject())
    {
        result->errors.append(
            QStringLiteral("Language configuration must be a JSON object: %1.").arg(configuration.fileName()));
        return result;
    }
    const auto config = configDocument.object();
    if (!config.value("defaultLocale").isString() || !config.value("fallbackLocale").isString())
    {
        result->errors.append(
            QStringLiteral("Language configuration requires defaultLocale and fallbackLocale strings."));
        return result;
    }
    result->defaultLocale = config.value("defaultLocale").toString();
    result->fallbackLocale = config.value("fallbackLocale").toString();
    const auto locales = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const auto &locale : locales)
    {
        const QDir directory(root.filePath(locale));
        const auto files = directory.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        if (files.isEmpty())
            continue;
        auto &catalog = result->languages[locale];
        auto &errors = result->localeErrors[locale];
        for (const auto &name : files)
        {
            const QString path = directory.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
            {
                errors.append(
                    QStringLiteral("Could not read translation catalog, or it exceeds 4 MiB: %1.").arg(path));
                continue;
            }
            QStringDecoder decoder(QStringDecoder::Utf8);
            const QString decoded = decoder(file.readAll());
            if (decoder.hasError())
            {
                errors.append(QStringLiteral("Translation catalog is not UTF-8: %1.").arg(path));
                continue;
            }
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(decoded.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
            {
                errors.append(QStringLiteral("Translation catalog must be a JSON object: %1.").arg(path));
                continue;
            }
            const auto object = document.object();
            for (auto it = object.begin(); it != object.end(); ++it)
            {
                if (it.key().isEmpty() || !it.value().isString())
                    errors.append(QStringLiteral("Translation catalog requires string values: %1, key %2.")
                                      .arg(path, it.key()));
                else if (catalog.contains(it.key()))
                    errors.append(QStringLiteral("Duplicate translation key %1 in %2.").arg(it.key(), locale));
                else
                    catalog.insert(it.key(), it.value().toString());
            }
        }
        if (catalog.isEmpty())
            errors.append(QStringLiteral("No usable translations for %1.").arg(locale));
    }
    result->defaultLocale = normalizedLocale(result->defaultLocale, *result);
    result->fallbackLocale = normalizedLocale(result->fallbackLocale, *result);
    if (result->defaultLocale.isEmpty())
        result->errors.append(QStringLiteral("The configured default language is not installed."));
    if (result->fallbackLocale.isEmpty())
        result->errors.append(QStringLiteral("The configured fallback language is not installed."));
    const auto fallback = result->languages.value(result->fallbackLocale);
    for (auto locale = result->languages.cbegin(); locale != result->languages.cend(); ++locale)
    {
        if (locale.key() == result->fallbackLocale)
            continue;
        const auto &catalog = locale.value();
        for (auto it = fallback.begin(); it != fallback.end(); ++it)
        {
            if (!catalog.contains(it.key()))
                result->missing.append(QStringLiteral("Missing translation %1 in %2; %3 fallback will be used.")
                                           .arg(it.key(), locale.key(), result->fallbackLocale));
            else if (placeholders(it.value()) != placeholders(catalog.value(it.key())))
                result->localeErrors[locale.key()].append(
                    QStringLiteral("Placeholder mismatch for translation key %1 in %2.")
                        .arg(it.key(), locale.key()));
        }
        for (auto it = catalog.begin(); it != catalog.end(); ++it)
            if (!fallback.contains(it.key()))
                result->missing.append(QStringLiteral("Missing fallback translation for key %1 from %2.")
                                           .arg(it.key(), locale.key()));
    }
    return result;
}

QStringList languageErrors(const Catalogs &catalogs, const QString &locale)
{
    auto errors = catalogs.errors + catalogs.localeErrors.value(catalogs.fallbackLocale);
    if (locale != catalogs.fallbackLocale)
        errors += catalogs.localeErrors.value(locale);
    return errors;
}

struct MessagePattern
{
    QRegularExpression expression;
    QString key;
    int literalLength = 0;
};

MessagePattern messagePattern(const QString &text, const QString &key)
{
    QString expression = QStringLiteral("\\A");
    QSet<QString> captured;
    qsizetype previous = 0;
    int literalLength = 0;
    auto placeholders = Placeholder.globalMatch(text);
    while (placeholders.hasNext())
    {
        const auto match = placeholders.next();
        const auto literal = text.mid(previous, match.capturedStart() - previous);
        expression += QRegularExpression::escape(literal);
        literalLength += static_cast<int>(literal.size());
        const auto number = match.captured(1);
        if (captured.contains(number))
            expression += QStringLiteral("\\k<p%1>").arg(number);
        else
        {
            expression += QStringLiteral("(?<p%1>.*?)").arg(number);
            captured.insert(number);
        }
        previous = match.capturedEnd();
    }
    const auto suffix = text.mid(previous);
    literalLength += static_cast<int>(suffix.size());
    expression += QRegularExpression::escape(suffix) + QStringLiteral("\\z");
    // A bare placeholder has no identifying message text and would capture any
    // unknown diagnostic. It still works normally through its stable resource key.
    if (captured.isEmpty() || literalLength == 0)
        return {};
    return {QRegularExpression(expression, QRegularExpression::DotMatchesEverythingOption), key, literalLength};
}

QString applyArguments(const QString &text, const QRegularExpressionMatch &match)
{
    QString result;
    qsizetype previous = 0;
    auto placeholders = Placeholder.globalMatch(text);
    while (placeholders.hasNext())
    {
        const auto placeholder = placeholders.next();
        result += text.mid(previous, placeholder.capturedStart() - previous);
        result += match.captured(QStringLiteral("p") + placeholder.captured(1));
        previous = placeholder.capturedEnd();
    }
    result += text.mid(previous);
    return result;
}

class CatalogTranslator final : public QTranslator
{
  public:
    CatalogTranslator(std::shared_ptr<const Catalogs> catalogs, const QString &locale)
        : catalogs_(std::move(catalogs)), selected_(catalogs_->languages.value(locale)),
          fallback_(catalogs_->languages.value(catalogs_->fallbackLocale))
    {
        QSet<QString> templates;
        for (const auto &catalog : catalogs_->languages)
        {
            for (auto it = catalog.begin(); it != catalog.end(); ++it)
            {
                if (!exactMessages_.contains(it.value()))
                    exactMessages_.insert(it.value(), it.key());
                if (templates.contains(it.value()))
                    continue;
                templates.insert(it.value());
                auto pattern = messagePattern(it.value(), it.key());
                if (!pattern.expression.pattern().isEmpty() && pattern.expression.isValid())
                    patterns_.push_back(std::move(pattern));
            }
        }
        std::stable_sort(patterns_.begin(), patterns_.end(), [](const auto &left, const auto &right)
                         { return left.literalLength > right.literalLength; });
    }

    bool isEmpty() const override
    {
        return selected_.isEmpty() && fallback_.isEmpty();
    }

    QString translate(const char *context, const char *sourceText, const char *, int) const override
    {
        if (!context || !sourceText)
            return {};
        const auto text = QString::fromUtf8(sourceText);
        const QByteArray name(context);
        if (name == "SingLilt")
            return lookup(text);
        if (name == "Diagnostics")
        {
            if (selected_.contains(text) || fallback_.contains(text))
                return lookup(text);
            const auto exact = exactMessages_.constFind(text);
            if (exact != exactMessages_.cend())
                return lookup(exact.value());
            for (const auto &pattern : patterns_)
            {
                const auto match = pattern.expression.match(text);
                if (match.hasMatch())
                    return applyArguments(lookup(pattern.key), match);
            }
            return {};
        }
        if (name == "QPlatformTheme" || name == "QDialogButtonBox" || name == "QMessageBox")
        {
            static const QHash<QString, QString> buttons{
                {"Save", "common.save"},   {"Discard", "common.discard"}, {"Cancel", "common.cancel"},
                {"OK", "common.ok"},       {"Yes", "common.yes"},         {"No", "common.no"},
                {"Close", "common.close"}, {"Open", "common.open"}};
            const auto key = buttons.constFind(QString(text).remove('&'));
            if (key != buttons.cend())
                return lookup(key.value());
        }
        return {};
    }

  private:
    QString lookup(const QString &key) const
    {
        const auto selected = selected_.constFind(key);
        if (selected != selected_.cend())
            return selected.value().isNull() ? QStringLiteral("") : selected.value();
        const auto fallback = fallback_.constFind(key);
        return fallback != fallback_.cend() ? fallback.value() : QString{};
    }

    const std::shared_ptr<const Catalogs> catalogs_;
    const Catalog selected_;
    const Catalog fallback_;
    QHash<QString, QString> exactMessages_;
    std::vector<MessagePattern> patterns_;
};

} // namespace

struct LanguageManager::Impl
{
    explicit Impl(const QString &directory) : catalogs(readCatalogs(directory)), locale(catalogs->defaultLocale) {}
    std::shared_ptr<const Catalogs> catalogs;
    std::unique_ptr<CatalogTranslator> translator;
    QString locale;
    QString error;
    bool changing = false;
};

LanguageManager::LanguageManager(QObject *parent) : LanguageManager(catalogDirectory(), parent) {}

LanguageManager::LanguageManager(const QString &directory, QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(directory))
{
}

QString LanguageManager::catalogDirectory()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/language");
}

QString LanguageManager::configuredDefaultLanguage()
{
    return readCatalogs(catalogDirectory())->defaultLocale;
}

bool LanguageManager::isInstalledLanguage(const QString &locale)
{
    const auto catalogs = readCatalogs(catalogDirectory());
    const auto normalized = normalizedLocale(locale, *catalogs);
    return !normalized.isEmpty() && languageErrors(*catalogs, normalized).isEmpty();
}

LanguageManager::~LanguageManager()
{
    if (impl_->translator && QCoreApplication::instance())
        QCoreApplication::removeTranslator(impl_->translator.get());
}

bool LanguageManager::setLanguage(QString locale)
{
    if (!impl_->catalogs->errors.isEmpty())
    {
        impl_->error = impl_->catalogs->errors.join('\n');
        return false;
    }
    const auto normalized = normalizedLocale(locale, *impl_->catalogs);
    if (normalized.isEmpty())
    {
        impl_->error = QStringLiteral("Unsupported language: %1.").arg(locale);
        return false;
    }
    const auto *application = QCoreApplication::instance();
    if (!application || QThread::currentThread() != application->thread())
    {
        impl_->error = QStringLiteral("Language changes require the application thread.");
        return false;
    }
    if (impl_->changing)
    {
        impl_->error = QStringLiteral("A language change is already in progress.");
        return false;
    }
    const auto errors = languageErrors(*impl_->catalogs, normalized);
    if (!errors.isEmpty())
    {
        impl_->error = errors.join('\n');
        return false;
    }
    impl_->error.clear();
    if (impl_->translator && impl_->locale == normalized)
        return true;
    QScopedValueRollback<bool> changing(impl_->changing, true);
    auto next = std::make_unique<CatalogTranslator>(impl_->catalogs, normalized);
    const auto previousLocale = impl_->locale;
    auto previous = std::move(impl_->translator);
    impl_->locale = normalized; // Widgets see the new language during LanguageChange.
    impl_->translator = std::move(next);
    if (!QCoreApplication::installTranslator(impl_->translator.get()))
    {
        impl_->translator = std::move(previous);
        impl_->locale = previousLocale;
        impl_->error = QStringLiteral("The translation catalog could not be installed.");
        return false;
    }
    // The new translator has priority before the old one leaves the chain, so
    // LanguageChange delivery never exposes raw IDs between two installed locales.
    if (previous)
        QCoreApplication::removeTranslator(previous.get());
    return true;
}

QString LanguageManager::language() const
{
    return impl_->locale;
}
QStringList LanguageManager::availableLanguages() const
{
    QStringList locales;
    for (const auto &locale : impl_->catalogs->languages.keys())
        if (languageErrors(*impl_->catalogs, locale).isEmpty())
            locales.append(locale);
    return locales;
}
QString LanguageManager::defaultLanguage() const
{
    return impl_->catalogs->defaultLocale;
}
QString LanguageManager::fallbackLanguage() const
{
    return impl_->catalogs->fallbackLocale;
}
QString LanguageManager::languageName(const QString &locale) const
{
    const auto catalog = impl_->catalogs->languages.value(locale);
    const auto name = catalog.value(QStringLiteral("common.language_name"));
    if (!name.isEmpty())
        return name;
    const auto nativeName = QLocale(locale).nativeLanguageName();
    return nativeName.isEmpty() ? locale : nativeName;
}
QString LanguageManager::errorString() const
{
    return impl_->error;
}
QStringList LanguageManager::validateCatalogs() const
{
    auto errors = impl_->catalogs->errors + impl_->catalogs->missing;
    for (const auto &localeErrors : impl_->catalogs->localeErrors)
        errors += localeErrors;
    return errors;
}

QString trText(const char *key)
{
    return key ? QCoreApplication::translate("SingLilt", key) : QString{};
}

QString localizeMessage(const QString &message)
{
    const auto source = message.toUtf8();
    return QCoreApplication::translate("Diagnostics", source.constData());
}

} // namespace singlilt

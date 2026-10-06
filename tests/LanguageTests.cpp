// External catalog discovery, fallback, and invalid-pack regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "i18n/LanguageManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace
{
void writeJson(const QString &path, const QJsonObject &object)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        throw std::runtime_error("Fixture directory creation failed");
    QFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("Fixture file write failed");
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    int checks = 0;
    const auto check = [&checks](bool passed, const char *name)
    {
        if (!passed)
            throw std::runtime_error(name);
        ++checks;
    };
    try
    {
        QTemporaryDir directory;
        check(directory.isValid(), "temporary catalog directory");
        const auto root = directory.path();
        writeJson(root + "/config.json", {{"defaultLocale", "zz_ZZ"}, {"fallbackLocale", "aa_AA"}});
        writeJson(root + "/aa_AA/core.json", {{"test.greeting", "Hello %1"}, {"test.other", "Fallback"}});
        writeJson(root + "/zz_ZZ/core.json",
                  {{"test.greeting", "Bonjour %1"}, {"common.language_name", "Fixture language"}});
        singlilt::LanguageManager languages(root);
        check(languages.availableLanguages().size() == 2, "directory discovery");
        check(languages.defaultLanguage() == "zz_ZZ" && languages.fallbackLanguage() == "aa_AA",
              "config locale IDs");
        check(languages.setLanguage("ZZ-zz"), "case and separator normalization");
        check(singlilt::trText("test.greeting").arg("Alex") == "Bonjour Alex", "selected external text");
        check(singlilt::trText("test.other") == "Fallback", "missing-key fallback");
        check(languages.languageName("zz_ZZ") == "Fixture language", "pack display name");
        check(languages.setLanguage("zz"), "generic language alias");
        check(!languages.setLanguage("missing") && languages.language() == "zz_ZZ",
              "unknown locale preserves state");
        writeJson(root + "/bb_BB/core.json", {{"test.greeting", "Mismatch"}});
        writeJson(root + "/cc_CC/core.json", {{"test.greeting", 42}});
        check(QDir().mkpath(root + "/ee_EE") && QDir().mkpath(root + "/ff_FF"), "invalid pack directories");
        QFile malformed(root + "/ee_EE/core.json");
        check(malformed.open(QIODevice::WriteOnly), "malformed fixture open");
        malformed.write("{");
        malformed.close();
        QFile invalidEncoding(root + "/ff_FF/core.json");
        check(invalidEncoding.open(QIODevice::WriteOnly), "encoding fixture open");
        invalidEncoding.write(QByteArray(1, char(0xff)));
        invalidEncoding.close();
        singlilt::LanguageManager updated(root);
        check(!updated.availableLanguages().contains("bb_BB") && !updated.availableLanguages().contains("cc_CC"),
              "bad packs excluded");
        check(!updated.availableLanguages().contains("ee_EE") && !updated.availableLanguages().contains("ff_FF"),
              "malformed JSON and UTF-8 rejected");
        check(updated.setLanguage("zz_ZZ"), "bad optional pack does not block valid language");
        check(!updated.setLanguage("bb_BB") && singlilt::trText("test.greeting") == "Bonjour %1",
              "bad switch preserves translator");
        writeJson(root + "/dd_DD/core.json", {{"test.greeting", "New %1"}});
        singlilt::LanguageManager restarted(root);
        check(restarted.availableLanguages().contains("dd_DD") && restarted.setLanguage("dd_DD"),
              "new manager discovers added pack");
        check(singlilt::trText("test.greeting") == "New %1", "new pack translation");
        writeJson(root + "/dd_DD/duplicate.json", {{"test.greeting", "Duplicate %1"}});
        singlilt::LanguageManager duplicatePack(root);
        check(!duplicatePack.setLanguage("dd_DD"), "duplicate keys rejected");
        writeJson(root + "/config.json", {{"defaultLocale", "zz_ZZ"}, {"fallbackLocale", "missing"}});
        singlilt::LanguageManager missingFallback(root);
        check(!missingFallback.setLanguage("zz_ZZ"), "missing fallback rejected");
        std::cout << "Language tests: " << checks << '/' << checks << " passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

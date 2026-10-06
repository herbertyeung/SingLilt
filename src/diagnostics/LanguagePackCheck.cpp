// External language selection and settings-dialog integration checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "LanguagePackCheck.h"
#include "cli/JsonReport.h"
#include "i18n/LanguageManager.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <stdexcept>

namespace singlilt
{
void runLanguagePackCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                          QApplication &application)
{
    QTimer::singleShot(
        0, &window,
        [&window, &languages, &args, &application]
        {
            QJsonArray checks;
            bool passed = true;
            const auto check = [&checks, &passed](const char *name, bool success)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", success}});
                passed &= success;
            };
            QJsonObject report;
            try
            {
                const auto locale = languages.language();
                auto *selector = window.findChild<QComboBox *>("languageSelector");
                auto *menu = window.findChild<QMenu *>("fileMenu");
                if (!selector || !menu)
                    throw std::runtime_error("Language controls are missing");
                check("external locale is selectable", selector->findData(locale) >= 0);
                check("workspace locale ID is preserved", selector->currentData().toString() == locale);
                check("menu uses selected translation", menu->title() == trText("ui.menu.file"));
                auto settings = window.appSettings();
                settings.language = locale;
                OptionsDialog dialog(settings, window.optionsContext(), &window);
                auto *choice = dialog.findChild<QComboBox *>("optionsLanguage");
                if (!choice)
                    throw std::runtime_error("Options language control is missing");
                check("options use locale data, not an index", choice->currentData().toString() == locale);
                bool applied = false;
                dialog.applyChanges = [&applied, &locale](const AppSettings &selected, const OptionsContext &)
                {
                    applied = selected.language == locale;
                    return applied;
                };
                auto *buttons = dialog.findChild<QDialogButtonBox *>();
                if (!buttons || !buttons->button(QDialogButtonBox::Ok))
                    throw std::runtime_error("Options acceptance button is missing");
                buttons->button(QDialogButtonBox::Ok)->click();
                check("settings validation accepts external locale", applied);
                report.insert("language", locale);
                report.insert("menuTitle", menu->title());
            }
            catch (const std::exception &error)
            {
                passed = false;
                report.insert("error", QString::fromUtf8(error.what()));
            }
            report.insert("checks", checks);
            report.insert("passed", passed);
            const auto path = args.value("report");
            QDir().mkpath(QFileInfo(path).absolutePath());
            try
            {
                writeJsonReport(path, report);
            }
            catch (const std::exception &)
            {
                passed = false;
            }
            application.exit(passed ? 0 : 3);
        });
}
} // namespace singlilt

// Appearance selection, persistence, system changes, and display-only regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ThemeCheck.h"
#include "cli/JsonReport.h"
#include "i18n/LanguageManager.h"
#include "ui/ClassroomDialog.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/PitchCurve.h"
#include "ui/ScoreView.h"
#include "ui/ThemeManager.h"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStyleHints>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
double luminance(const QColor &color)
{
    const auto linear = [](double value)
    { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

double contrast(const QColor &first, const QColor &second)
{
    const double a = luminance(first), b = luminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}
} // namespace

void runThemeCheck(MainWindow &window, LanguageManager &languages, ThemeManager &themes,
                   const QCommandLineParser &args, QApplication &application)
{
    QTimer::singleShot(
        0, &window,
        [&]
        {
            QJsonArray checks;
            QJsonObject report;
            const auto check = [&checks](bool passed, const char *name)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", passed}});
                if (!passed)
                    throw std::runtime_error(name);
            };
            const auto pump = [] { QApplication::processEvents(); };
            try
            {
                const auto folder = QFileInfo(args.value("report")).absolutePath();
                check(QDir().mkpath(folder), "theme report directory");
                const auto originalScore = scoreToJson(window.project().score);
                const auto originalImage = window.project().image;
                const bool originalDirty = window.hasUnsavedChanges();
                themes.setMode(ThemeMode::Light);
                pump();
                auto settings = window.appSettings();
                settings.audioBackend = 1;
                settings.themeMode = ThemeMode::Light;
                OptionsDialog options(settings, window.optionsContext(), &window);
                auto *selector = options.findChild<QComboBox *>("optionsTheme");
                check(selector && selector->count() == 3, "three appearance choices");
                for (const auto mode : {ThemeMode::System, ThemeMode::Light, ThemeMode::Dark})
                    check(selector->findData(static_cast<int>(mode)) >= 0, "stable theme item IDs");
                auto *buttons = options.findChild<QDialogButtonBox *>("optionsButtons");
                check(buttons, "options buttons available");
                bool applied = false;
                options.applyChanges = [&](const AppSettings &s, const OptionsContext &context)
                {
                    applied = window.applyOptions(s, context);
                    return applied;
                };
                options.show();
                selector->setCurrentIndex(selector->findData(static_cast<int>(ThemeMode::Dark)));
                pump();
                check(!themes.isDark(), "editing a choice does not apply it");
                buttons->button(QDialogButtonBox::Apply)->click();
                pump();
                check(applied && themes.mode() == ThemeMode::Dark && themes.isDark(),
                      "apply switches to dark immediately");
                check(QSettings().value("ui/theme").toString() == "dark" &&
                          loadAppSettings().themeMode == ThemeMode::Dark,
                      "dark preference survives reload");
                check(options.palette().color(QPalette::Window).lightness() < 128,
                      "open options dialog inherits dark palette");
                auto *scoreView = window.findChild<QGraphicsView *>("scoreView");
                check(scoreView && scoreView->backgroundBrush().color().lightness() < 128,
                      "existing score canvas follows palette changes");
                check(options.grab().save(folder + "/options-dark.png"), "dark options screenshot");
                check(window.grab().save(folder + "/main-dark.png"), "dark workspace screenshot");
                selector->setCurrentIndex(selector->findData(static_cast<int>(ThemeMode::Light)));
                buttons->button(QDialogButtonBox::Cancel)->click();
                check(themes.isDark() && QSettings().value("ui/theme").toString() == "dark",
                      "cancel keeps the last applied theme");

                PitchCurve curve;
                curve.resize(620, 180);
                curve.show();
                pump();
                check(curve.grab().toImage().pixelColor(5, 5).lightness() < 128,
                      "custom pitch chart uses dark background");
                check(curve.grab().save(folder + "/curve-dark.png"), "dark chart screenshot");
                ClassroomDialog classroom(languages, AudioBackend::WindowsMidi, defaultLessonDirectory(),
                                          folder + "/history.json", &window);
                classroom.show();
                pump();
                check(classroom.palette().color(QPalette::Window).lightness() < 128,
                      "classroom inherits dark theme");
                check(classroom.grab().save(folder + "/classroom-dark.png"), "dark classroom screenshot");
                classroom.hide();
                auto *menu = window.findChild<QMenu *>("fileMenu");
                check(menu && menu->palette().color(QPalette::Base).lightness() < 128, "menus inherit dark theme");
                menu->popup(window.mapToGlobal(QPoint(20, 40)));
                pump();
                check(menu->grab().save(folder + "/menu-dark.png"), "dark menu screenshot");
                menu->hide();

                for (const auto mode : {ThemeMode::Light, ThemeMode::Dark})
                {
                    themes.setMode(mode);
                    pump();
                    const auto palette = application.palette();
                    check(contrast(palette.color(QPalette::Text), palette.color(QPalette::Base)) >= 4.5,
                          "primary text contrast");
                    check(contrast(palette.color(QPalette::HighlightedText), palette.color(QPalette::Highlight)) >=
                              4.5,
                          "selected text contrast");
                }
                themes.setMode(ThemeMode::Light);
                pump();
                check(window.grab().save(folder + "/main-light.png"), "light workspace screenshot");
                check(curve.grab().toImage().pixelColor(5, 5).lightness() > 128,
                      "existing chart updates to light");

                themes.setMode(ThemeMode::System);
                pump();
                const auto systemScheme = application.styleHints()->colorScheme();
                report.insert("initialSystemScheme", static_cast<int>(systemScheme));
                check(themes.isDark() == (systemScheme == Qt::ColorScheme::Dark),
                      "system mode reads the platform scheme");
                // Exercise the Qt notification path without changing the user's Windows preferences.
                application.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
                pump();
                check(themes.isDark() && application.palette().color(QPalette::Window).lightness() < 128,
                      "system mode responds to a dark scheme notification");
                application.styleHints()->setColorScheme(Qt::ColorScheme::Light);
                pump();
                check(!themes.isDark() && application.palette().color(QPalette::Window).lightness() > 128,
                      "system mode responds to a light scheme notification");
                themes.setMode(ThemeMode::Light);
                application.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
                pump();
                check(!themes.isDark() && application.palette().color(QPalette::Window).lightness() > 128,
                      "explicit light does not follow scheme notifications");
                themes.setMode(ThemeMode::Dark);
                application.styleHints()->setColorScheme(Qt::ColorScheme::Light);
                pump();
                check(themes.isDark() && application.palette().color(QPalette::Window).lightness() < 128,
                      "explicit dark does not follow scheme notifications");
                themes.setMode(ThemeMode::System);
                pump();
                check(application.styleHints()->colorScheme() == systemScheme,
                      "follow system clears the application override");

                for (const auto mode : {ThemeMode::System, ThemeMode::Light, ThemeMode::Dark})
                {
                    settings.themeMode = mode;
                    saveAppSettings(settings);
                    check(loadAppSettings().themeMode == mode, "theme preference round-trip");
                }
                QSettings().setValue("ui/theme", "invalid-fixture");
                check(loadAppSettings().themeMode == ThemeMode::System,
                      "invalid stored preference falls back to system");
                settings.themeMode = static_cast<ThemeMode>(99);
                bool rejected = false;
                try
                {
                    validateAppSettings(settings);
                }
                catch (const SettingsValidationError &error)
                {
                    rejected = QString::fromLatin1(error.field()) == "optionsTheme";
                }
                check(rejected, "invalid theme enum is rejected");
                const auto previousMode = themes.mode();
                const auto previousPalette = application.palette();
                rejected = false;
                try
                {
                    themes.setMode(static_cast<ThemeMode>(99));
                }
                catch (const std::invalid_argument &)
                {
                    rejected = true;
                }
                check(rejected && themes.mode() == previousMode && application.palette() == previousPalette,
                      "invalid manager mode preserves appearance");
                check(window.project().image == originalImage &&
                          scoreToJson(window.project().score) == originalScore &&
                          window.hasUnsavedChanges() == originalDirty,
                      "appearance does not edit project content or paper pixels");
                themes.setMode(ThemeMode::Light);
                settings.themeMode = ThemeMode::Light;
                saveAppSettings(settings);
                report.insert("passed", true);
            }
            catch (const std::exception &error)
            {
                report.insert("passed", false);
                report.insert("error", QString::fromUtf8(error.what()));
            }
            report.insert("checks", checks);
            report.insert("systemChangeTest", "Qt color-scheme notifications; Windows settings unchanged");
            writeJsonReport(args.value("report"), report);
            application.exit(report.value("passed").toBool() ? 0 : 3);
        });
}
} // namespace singlilt

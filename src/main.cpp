// Application startup, command dispatch, and GUI lifetime.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "cli/CommandLine.h"
#include "cli/JsonReport.h"
#include "cli/ProjectCommands.h"
#include "diagnostics/DiagnosticCommands.h"
#include "i18n/LanguageManager.h"
#include "settings/LegacyMigration.h"
#include "ui/MainWindow.h"
#include "ui/ThemeManager.h"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QIcon>
#include <QImageReader>
#include <QJsonObject>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyleFactory>
#include <QTextStream>
#include <exception>

using namespace singlilt;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("SingLilt");
    app.setApplicationName("SingLilt");
    app.setApplicationVersion(SINGLILT_VERSION);
    app.setApplicationDisplayName("SingLilt");
    app.setWindowIcon(QIcon(":/branding/singlilt-icon.png"));
    app.setStyle(QStyleFactory::create("Fusion"));
    QImageReader::setAllocationLimit(256);
    LanguageManager languages;
    const auto rawArguments = app.arguments();
    if (!configureDiagnosticSettings(rawArguments))
        return 2;
    const bool headless = isHeadlessCommand(rawArguments);
    if (!headless && QSettings::defaultFormat() == QSettings::NativeFormat)
    {
        try
        {
            migrateLegacyUserState();
        }
        catch (const std::exception &error)
        {
            QTextStream(stderr) << "SingLilt could not import existing user state: " << error.what() << Qt::endl;
            return 1;
        }
    }
    QSettings languageSettings;
    QString language = headless ? languages.fallbackLanguage()
                                : languageSettings.value("ui/language", languages.defaultLanguage()).toString();
    bool explicitLanguage = false;
    for (int i = 1; i < rawArguments.size(); ++i)
    {
        if (rawArguments[i] == "--language" && i + 1 < rawArguments.size())
        {
            language = rawArguments[i + 1];
            explicitLanguage = true;
        }
        else if (rawArguments[i].startsWith("--language="))
        {
            language = rawArguments[i].mid(11);
            explicitLanguage = true;
        }
    }
    if (!explicitLanguage && !LanguageManager::isInstalledLanguage(language))
        language = headless ? languages.fallbackLanguage() : languages.defaultLanguage();
    if (!languages.setLanguage(language))
    {
        QTextStream(stderr) << languages.errorString() << Qt::endl;
        return 1;
    }
    QCommandLineParser args;
    addCommandLineOptions(args);
    args.process(app);
    if (args.isSet("version"))
    {
        QTextStream(stdout) << app.applicationDisplayName() << ' ' << app.applicationVersion() << Qt::endl;
        return 0;
    }
    try
    {
        ThemeManager themes(app);
        themes.setMode(headless || QSettings::defaultFormat() == QSettings::IniFormat
                           ? ThemeMode::Light
                           : loadAppSettings().themeMode);
        if (const auto exitCode = runCoreDiagnostics(args, languages))
            return *exitCode;
        if (const auto exitCode = runProjectCommand(args))
            return *exitCode;
        MainWindow window(languages, themes);
        if (args.isSet("audio-backend"))
        {
            const auto name = args.value("audio-backend");
            if (name != "sampled" && name != "system")
                throw std::runtime_error(trText("app.backend_error").toStdString());
            const auto backend = name == "system" ? AudioBackend::WindowsMidi : AudioBackend::SampledPiano;
            window.player().setAudioBackend(backend);
            auto *selector = window.findChild<QComboBox *>("audioBackend");
            if (selector)
            {
                QSignalBlocker blocker(selector);
                selector->setCurrentIndex(name == "system" ? 1 : 0);
            }
        }
        if (!args.positionalArguments().isEmpty())
            window.openFile(args.positionalArguments().first());
        if (args.isSet("gm-soundfont") && !window.player().setGmSoundFontPath(args.value("gm-soundfont")))
            throw std::runtime_error(window.player().errorString().toStdString());
        window.show();
        if (args.isSet("classroom") || (app.arguments().size() == 1 && window.appSettings().startupClassroom))
            window.openClassroom();
        runWindowDiagnostics(window, languages, themes, args, app);
        return app.exec();
    }
    catch (const std::exception &e)
    {
        const auto message = localizeMessage(QString::fromUtf8(e.what()));
        QTextStream(stderr) << message << Qt::endl;
        if (args.isSet("report"))
        {
            try
            {
                writeJsonReport(args.value("report"), {{"error", message}});
            }
            catch (const std::exception &reportError)
            {
                QTextStream(stderr) << localizeMessage(QString::fromUtf8(reportError.what())) << Qt::endl;
            }
        }
        return 1;
    }
}

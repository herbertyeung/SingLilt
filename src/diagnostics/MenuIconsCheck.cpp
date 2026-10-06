// Menu icon visibility and action-state checks.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "MenuIconsCheck.h"
#include "i18n/LanguageManager.h"
#include "storage/LessonStore.h"
#include "ui/ClassroomDialog.h"
#include "ui/MainWindow.h"
#include "ui/MenuIcons.h"
#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMenuBar>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace singlilt
{
namespace
{
bool hasInk(const QImage &image)
{
    int pixels = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            pixels += qAlpha(image.pixel(x, y)) > 32;
    return pixels > 8;
}
} // namespace

void runMenuIconsCheck(MainWindow &window, LanguageManager &languages, const QCommandLineParser &args,
                       QApplication &app)
{
    QTimer::singleShot(
        100, &window,
        [&window, &languages, &args, &app]
        {
            QJsonArray checks;
            QJsonArray screenshots;
            bool passed = true;
            const auto check = [&](const QString &name, bool valid)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", valid}});
                passed &= valid;
            };
            const QString folder = QFileInfo(args.value("report")).absolutePath();
            try
            {
                const QString fixture = folder + "/run-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
                if (!QDir().mkpath(fixture))
                    throw std::runtime_error("Menu icon fixture directory could not be created");
                auto classroom = std::make_unique<ClassroomDialog>(languages, AudioBackend::WindowsMidi,
                                                                   defaultLessonDirectory(),
                                                                   fixture + "/history.json", &window);
                classroom->setAttribute(Qt::WA_DeleteOnClose, false);
                const auto verifyMenu = [&](QMenu &menu, const QString &scope)
                {
                    for (auto *action : menu.actions())
                    {
                        if (action->isSeparator())
                            continue;
                        check(scope + "/" + action->text() + " icon shown",
                              !action->icon().isNull() && action->isIconVisibleInMenu() &&
                                  hasInk(action->icon().pixmap(24, 24).toImage()));
                    }
                };
                const auto capture = [&](QMenu &menu, const QString &name)
                {
                    menu.popup(window.mapToGlobal(QPoint(25, 45)));
                    QApplication::processEvents();
                    for (auto *action : menu.actions())
                        if (action->isEnabled() && !action->isSeparator())
                        {
                            menu.setActiveAction(action);
                            break;
                        }
                    QApplication::processEvents();
                    const QString path = fixture + '/' + name + ".png";
                    check(name + " screenshot saved", menu.grab().save(path));
                    screenshots.append(path);
                    menu.hide();
                };
                for (const QString locale : {QStringLiteral("zh_CN"), QStringLiteral("en_US")})
                {
                    check("Language " + locale, languages.setLanguage(locale));
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
                    for (auto *action : window.menuBar()->actions())
                        if (auto *menu = action->menu())
                        {
                            verifyMenu(*menu, "main " + locale);
                            capture(*menu, locale + '-' + menu->objectName());
                        }
                    for (const auto *name : {"practiceMenu", "sampleScoresMenu", "recentProjectsMenu"})
                    {
                        auto *menu = window.findChild<QMenu *>(name);
                        if (!menu)
                            throw std::runtime_error("Expected menu is missing");
                        verifyMenu(*menu, QString::fromLatin1(name) + ' ' + locale);
                    }
                    auto *classroomMenus = classroom->findChild<QMenuBar *>();
                    if (!classroomMenus)
                        throw std::runtime_error("Classroom menu bar is missing");
                    int index = 0;
                    for (auto *action : classroomMenus->actions())
                        if (auto *menu = action->menu())
                        {
                            verifyMenu(*menu, "classroom " + locale);
                            capture(*menu, locale + "-classroom-" + QString::number(index++));
                        }
                    QSettings().setValue("projects/recent", QStringList{fixture + "/recent-example.jpp"});
                }
                languages.setLanguage("zh_CN");
                QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
                auto *recent = window.findChild<QMenu *>("recentProjectsMenu");
                check("Recent project rebuild retains icons",
                      recent && recent->actions().size() == 1 &&
                          hasInk(recent->actions().first()->icon().pixmap(24, 24).toImage()));
                const auto *save = window.findChild<QAction *>("menuSave");
                const auto *open = window.findChild<QAction *>("menuOpenProject");
                const auto *undo = window.findChild<QAction *>("menuUndo");
                check("File shortcuts unchanged", save && save->shortcut() == QKeySequence::Save && open &&
                                                      open->shortcut() == QKeySequence::Open);
                check("Undo shortcut and disabled state unchanged",
                      undo && undo->shortcut() == QKeySequence::Undo && !undo->isEnabled());
                for (int glyph = 0; glyph <= static_cast<int>(MenuIcon::Replay); ++glyph)
                {
                    const QIcon icon = menuIcon(static_cast<MenuIcon>(glyph));
                    for (int size : {16, 24})
                    {
                        for (qreal scale : {1.0, 1.25, 1.5, 2.0})
                        {
                            const QPixmap pixmap = icon.pixmap(QSize(size, size), scale);
                            check(QString("Glyph %1 size %2 DPR %3").arg(glyph).arg(size).arg(scale),
                                  pixmap.size() == QSize(qRound(size * scale), qRound(size * scale)) &&
                                      std::abs(pixmap.devicePixelRatio() - scale) < 0.01 &&
                                      hasInk(pixmap.toImage()));
                        }
                    }
                    const QImage normal = icon.pixmap(24, 24, QIcon::Normal).toImage();
                    const QImage disabled = icon.pixmap(24, 24, QIcon::Disabled).toImage();
                    const QImage active = icon.pixmap(24, 24, QIcon::Active).toImage();
                    const QImage selected = icon.pixmap(24, 24, QIcon::Selected).toImage();
                    check(QString("Glyph %1 disabled/selected distinct and nonempty").arg(glyph),
                          hasInk(disabled) && hasInk(active) && hasInk(selected) && normal != disabled &&
                              normal != active && normal != selected);
                }
                classroom->hide();
            }
            catch (const std::exception &error)
            {
                check(QString::fromUtf8(error.what()), false);
            }
            QSaveFile report(args.value("report"));
            const QByteArray bytes =
                QJsonDocument(QJsonObject{{"passed", passed}, {"checks", checks}, {"screenshots", screenshots}})
                    .toJson();
            if (!report.open(QIODevice::WriteOnly) || report.write(bytes) != bytes.size() || !report.commit())
                app.exit(4);
            else
                app.exit(passed ? 0 : 3);
        });
}
} // namespace singlilt

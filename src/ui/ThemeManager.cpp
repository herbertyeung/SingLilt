// Application appearance and system color-scheme changes.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ThemeManager.h"
#include "settings/AppSettings.h"
#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleHints>
#include <QTimer>
#include <stdexcept>

namespace singlilt
{
ThemeManager::ThemeManager(QApplication &application) : application_(application), mode_(ThemeMode::System)
{
    QFile stylesheet(":/styles/app.qss");
    if (!stylesheet.open(QIODevice::ReadOnly))
        throw std::runtime_error("The application theme resource is missing");
    stylesheet_ = QString::fromUtf8(stylesheet.readAll());
    connect(application_.styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this]
            {
                // Qt refreshes its platform palette after sending the color-scheme notification.
                QTimer::singleShot(0, this, [this] { applyPalette(); });
            });
}

void ThemeManager::setMode(ThemeMode mode)
{
    if (mode != ThemeMode::System && mode != ThemeMode::Light && mode != ThemeMode::Dark)
        throw std::invalid_argument("Unknown application theme mode");
    mode_ = mode;
    if (mode == ThemeMode::System)
        application_.styleHints()->unsetColorScheme();
    else
        application_.styleHints()->setColorScheme(mode == ThemeMode::Dark ? Qt::ColorScheme::Dark
                                                                          : Qt::ColorScheme::Light);
    applyPalette();
}

ThemeMode ThemeManager::mode() const
{
    return mode_;
}

bool ThemeManager::isDark() const
{
    return mode_ == ThemeMode::Dark ||
           (mode_ == ThemeMode::System && application_.styleHints()->colorScheme() == Qt::ColorScheme::Dark);
}

void ThemeManager::applyPalette()
{
    const bool dark = isDark();
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(dark ? "#182130" : "#F2F5FA"));
    palette.setColor(QPalette::Base, QColor(dark ? "#111827" : "#FFFFFF"));
    palette.setColor(QPalette::AlternateBase, QColor(dark ? "#223044" : "#EDF2FA"));
    palette.setColor(QPalette::Button, QColor(dark ? "#263449" : "#FFFFFF"));
    const QColor text(dark ? "#E7EDF6" : "#172B4D");
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText})
        palette.setColor(role, text);
    palette.setColor(QPalette::ToolTipBase, palette.color(QPalette::Base));
    palette.setColor(QPalette::Highlight, QColor(dark ? "#79AAFF" : "#2463EB"));
    palette.setColor(QPalette::Accent, palette.color(QPalette::Highlight));
    palette.setColor(QPalette::HighlightedText, QColor(dark ? "#0B1423" : "#FFFFFF"));
    palette.setColor(QPalette::PlaceholderText, QColor(dark ? "#A0AEC2" : "#627389"));
    palette.setColor(QPalette::Mid, QColor(dark ? "#44556E" : "#CED8E5"));
    palette.setColor(QPalette::Midlight, QColor(dark ? "#0F1724" : "#E6ECF3"));
    palette.setColor(QPalette::Light, QColor(dark ? "#3A4A62" : "#FFFFFF"));
    palette.setColor(QPalette::Dark, QColor(dark ? "#0B1220" : "#AABAD1"));
    palette.setColor(QPalette::Shadow, QColor(dark ? "#060B13" : "#71839B"));
    palette.setColor(QPalette::Link, QColor(dark ? "#66D9C0" : "#0E8174"));
    palette.setColor(QPalette::LinkVisited, QColor(dark ? "#F1B87B" : "#D26019"));
    palette.setColor(QPalette::BrightText, QColor(dark ? "#FF9C9C" : "#B3261E"));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, palette.color(QPalette::PlaceholderText));
    application_.setPalette(palette);
    // Re-polish the shared palette-based sheet so existing popups and dialogs update too.
    application_.setStyleSheet(stylesheet_);
}
} // namespace singlilt

// Painter-based icons for application menu actions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QIcon>

namespace singlilt
{
enum class MenuIcon
{
    NewProject,
    OpenProject,
    ImportImage,
    ImportAudio,
    Paste,
    Save,
    SaveAs,
    ExportAudio,
    Exit,
    Recent,
    Undo,
    Redo,
    Lyrics,
    Repeat,
    FitWidth,
    Practice,
    Correction,
    Classroom,
    EarTraining,
    Accompaniment,
    Samples,
    Scale,
    Score,
    Recognize,
    Settings,
    About,
    Folder,
    Refresh,
    Play,
    Microphone,
    Stop,
    Calibration,
    Replay
};

QIcon menuIcon(MenuIcon icon);
} // namespace singlilt

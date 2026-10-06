// Public command-line options and early command detection.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "CommandLine.h"
#include "i18n/LanguageManager.h"
#include <QCommandLineParser>
#include <algorithm>

namespace singlilt
{
bool hasCommandLineOption(const QStringList &arguments, const QString &name)
{
    return std::any_of(arguments.begin(), arguments.end(), [&name](const QString &argument)
                       { return argument == name || argument.startsWith(name + '='); });
}

bool isHeadlessCommand(const QStringList &arguments)
{
    const auto hasOption = [&arguments](const QString &name) { return hasCommandLineOption(arguments, name); };
    return hasOption("--version") || hasOption("-v") || hasOption("--help") || hasOption("-h") ||
           hasOption("--recognize") || hasOption("--recognize-staff-local") ||
           hasOption("--recognize-staff-pages") || hasOption("--inspect") || hasOption("--render-wave") ||
           hasOption("--catalog-check") || hasOption("--generate-accompaniment") ||
           hasOption("--whole-song-candidates") || hasOption("--transcribe-audio") ||
           hasOption("--native-staff-core-check") || hasOption("--native-staff-process-check") ||
           hasOption("--staff-note-editing-check") || hasOption("--staff-position-mapping-check") ||
           hasOption("--staff-edit-recovery-check");
}

void addCommandLineOptions(QCommandLineParser &args)
{
    args.setApplicationDescription(trText("app.description"));
    args.addHelpOption();
    args.addOption({{"v", "version"}, trText("cli.version")});
    args.addOption({"recognize", trText("cli.recognize"), "image"});
    args.addOption({"recognize-staff-local", trText("cli.local_staff.recognize"), "image"});
    args.addOption({"staff-position-mapping-check", trText("cli.original_playback.smart_check")});
    args.addOption({"staff-edit-recovery-check", trText("cli.original_playback.smart_check")});
    args.addOption({"staff-smart-note-check", trText("cli.original_playback.smart_check")});
    args.addOption({"staff-smart-project", trText("cli.original_playback.smart_check"), "project"});
    args.addOption({"staff-note-canvas-check", trText("cli.original_playback.note_edit_check")});
    args.addOption({"staff-note-project", trText("cli.original_playback.note_edit_check"), "project"});
    args.addOption({"staff-note-editing-check", trText("cli.original_playback.note_edit_check")});
    args.addOption({"native-staff-core-check", trText("cli.native_staff.core_check")});
    args.addOption({"native-staff-process-check", "Verify native OMR process handling with isolated fixtures"});
    args.addOption({"theme-check", "Verify appearance preferences and system color-scheme changes"});
    args.addOption({"staff-engine", trText("cli.native_staff.engine"), "engine", "crisp"});
    args.addOption({"staff-model", trText("cli.native_staff.model"), "model"});
    args.addOption({"staff-executable", trText("cli.native_staff.executable"), "program"});
    args.addOption({"recognize-staff-pages", trText("cli.pages.recognize")});
    args.addOption({"staff-page", trText("cli.pages.input"), "image"});
    args.addOption({"staff-page-split", trText("cli.pages.split"), "mode", "auto"});
    args.addOption({"multi-page-workflow-check", trText("cli.pages.workflow_check")});
    args.addOption({"multi-page-inputs", trText("cli.pages.fixture"), "directory"});
    args.addOption({"local-staff-image-check", trText("cli.local_staff.real_check")});
    args.addOption({"local-staff-image", trText("cli.local_staff.real_image"), "image"});
    args.addOption({"staff-primary-program", trText("cli.local_staff.primary_program"), "program"});
    args.addOption({"staff-tempo", trText("cli.fidelity.tempo"), "bpm"});
    args.addOption({"staff-fidelity-check", trText("cli.fidelity.check")});
    args.addOption({"original-staff-view-check", trText("cli.original_playback.view_check")});
    args.addOption({"staff-anchor-correction-check", trText("cli.original_playback.anchor_check")});
    args.addOption({"staff-anchor-project", trText("cli.original_playback.anchor_check"), "project"});
    args.addOption({"original-image-workflow-check", trText("cli.original_playback.workflow_check")});
    for (const auto *name : {"original-source-musicxml", "original-source-image", "original-source-bpm"})
        args.addOption({name, trText("cli.original_playback.workflow_check"), "input"});
    args.addOption({"staff-fidelity-reference", trText("cli.fidelity.reference"), "musicxml"});
    args.addOption({"staff-fidelity-original", trText("cli.fidelity.original"), "image"});
    args.addOption({"staff-other-program", trText("cli.local_staff.other_program"), "program"});
    args.addOption({"out", trText("cli.out"), "project"});
    args.addOption({"vision-endpoint", trText("cli.vision_endpoint"), "url"});
    args.addOption({"vision-model", trText("cli.vision_model"), "model", "gpt-4.1"});
    args.addOption({"vision-timeout", trText("cli.vision_timeout"), "seconds", "600"});
    args.addOption({"inspect", trText("cli.inspect"), "project"});
    args.addOption({"timeline", trText("cli.timeline")});
    args.addOption({"smoke-verse", trText("cli.smoke_verse"), "index"});
    args.addOption({"report", trText("cli.report"), "file"});
    args.addOption({"smoke", trText("cli.smoke")});
    args.addOption({"audio-backend", trText("cli.audio_backend"), "backend"});
    args.addOption({"render-wave", trText("cli.render_wave"), "file"});
    args.addOption({"render-seconds", trText("cli.render_seconds"), "seconds", "20"});
    args.addOption({"velocity", trText("cli.velocity"), "value"});
    args.addOption({"uniform", trText("cli.uniform")});
    args.addOption({"screenshot", trText("cli.screenshot"), "file"});
    args.addOption({"language", trText("cli.language"), "locale"});
    args.addOption({"ui-localization-check", trText("cli.ui_check")});
    args.addOption({"async-recognition-check", trText("cli.async_check"), "scenario"});
    args.addOption({"catalog-check", trText("cli.catalog_check")});
    args.addOption({"language-pack-check", "Check external locale selection and settings integration."});
    args.addOption({"generate-accompaniment", trText("cli.generate_accompaniment")});
    args.addOption({"accompaniment-pattern", trText("cli.accompaniment_pattern"), "pattern", "block"});
    args.addOption({"harmony-mode", trText("cli.harmony_mode"), "mode", "auto"});
    args.addOption({"harmony-tonic", trText("cli.harmony_tonic"), "pitch-class"});
    args.addOption({"practice-mix", trText("cli.practice_mix"), "mix"});
    args.addOption({"melody-volume", trText("cli.melody_volume"), "volume"});
    args.addOption({"accompaniment-volume", trText("cli.accompaniment_volume"), "volume"});
    args.addOption({"transpose", trText("cli.transpose"), "semitones", "0"});
    args.addOption({"speed", trText("cli.speed"), "factor", "1"});
    args.addOption({"metronome", trText("cli.metronome")});
    args.addOption({"gm-soundfont", trText("cli.gm_soundfont"), "file"});
    args.addOption({"accompaniment-check", trText("cli.accompaniment_check")});
    args.addOption({"accompaniment-preview-check", trText("cli.accompaniment_preview_check")});
    args.addOption({"whole-song-candidates", trText("cli.whole_song_candidates")});
    args.addOption({"accompaniment-variant", trText("cli.accompaniment_variant"), "id"});
    args.addOption({"whole-song-check", trText("cli.whole_song_check")});
    args.addOption({"classroom", trText("cli.classroom")});
    args.addOption({"project-package-check", trText("cli.project_check")});
    args.addOption({"product-workspace-check", trText("cli.product_workspace_check")});
    args.addOption({"settings-product-check", trText("cli.settings_product_check")});
    args.addOption({"wave-export-check", trText("cli.wave_export_check")});
    args.addOption({"history-recovery-check", trText("cli.history_recovery_check")});
    args.addOption({"menu-icons-check", trText("cli.menu_icons_check")});
    args.addOption({"staff-renderer-check", trText("cli.staff_renderer_check")});
    args.addOption({"musicxml-check", trText("cli.musicxml_check")});
    args.addOption({"staff-recognition-check", trText("cli.staff_recognition_check")});
    args.addOption({"staff-playback-check", trText("cli.staff_playback_check")});
    args.addOption({"staff-workflow-check", trText("cli.staff_workflow_check")});
    args.addOption({"staff-fixture", "Staff fixture data for integration verification", "file"});
    args.addOption({"staff-source-image", "Staff fixture original image", "file"});
    args.addOption({"project-test-audio", trText("cli.project_test_audio"), "file"});
    args.addOption({"options-check", trText("cli.options_check")});
    args.addOption({"options-range-only", "Only run the focused current-score range diagnostic"});
    args.addOption({"options-read-check", trText("cli.options_check")});
    args.addOption({"instrument-check", trText("cli.note_preview_check")});
    args.addOption({"note-preview-check", trText("cli.note_preview_check")});
    args.addOption({"classroom-check", trText("cli.classroom_check")});
    args.addOption({"microphone-check", trText("cli.microphone_check")});
    args.addOption({"transcribe-audio", trText("cli.transcribe_audio"), "file"});
    args.addOption({"audio-start", trText("cli.audio_start"), "seconds", "0"});
    args.addOption({"audio-end", trText("cli.audio_end"), "seconds", "60"});
    args.addOption({"audio-bpm", trText("cli.audio_bpm"), "bpm", "0"});
    args.addOption({"audio-tonic", trText("cli.audio_tonic"), "pitch-class", "-1"});
    args.addOption({"lyrics-file", trText("cli.lyrics_file"), "file"});
    args.addOption({"recognize-lyrics", trText("cli.recognize_lyrics")});
    args.addOption({"enhance-voice", trText("cli.enhance_voice")});
    args.addOption({"whisper-executable", trText("cli.whisper_executable"), "file"});
    args.addOption({"whisper-model", trText("cli.whisper_model"), "file"});
    args.addOption({"notation-image", trText("cli.notation_image"), "file"});
    args.addOption({"audio-task-check", trText("cli.audio_task_check"), "file"});
    args.addOption({"audio-whole-song", trText("cli.audio_whole_song")});
    args.addOption({"audio-language", trText("cli.audio_language"), "language", "auto"});
    args.addOption({"input-isolated-vocals", trText("cli.input_isolated_vocals")});
    args.addOption({"separator-python", trText("cli.separator_python"), "file"});
    args.addOption({"separator-script", trText("cli.separator_script"), "file"});
    args.addOption({"separator-model-dir", trText("cli.separator_model_dir"), "directory"});
    args.addOption({"separator-cache-dir", trText("cli.separator_cache_dir"), "directory"});
    args.addPositionalArgument("file", trText("cli.file"), "[file]");
}
} // namespace singlilt

// Recognition, transcription, inspection, and export commands.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "ProjectCommands.h"
#include "JsonReport.h"
#include "audio/WaveRenderer.h"
#include "domain/AccompanimentTimeline.h"
#include "i18n/LanguageManager.h"
#include "recognition/AudioTranscriber.h"
#include "recognition/CloudRecognizer.h"
#include "recognition/LocalRecognizer.h"
#include "recognition/LocalStaffRecognizer.h"
#include "recognition/StaffPageSplitter.h"
#include "storage/ProjectStore.h"
#include "ui/NotationRenderer.h"
#include "ui/PracticeScore.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
QJsonArray timelineJson(const Timeline &timeline)
{
    QJsonArray events;
    for (const auto &event : timeline.events)
        events.append(QJsonObject{{"sourceNoteIndex", int(event.sourceNoteIndex)},
                                  {"startTick", qint64(event.startTick)},
                                  {"durationTicks", qint64(event.durationTicks)},
                                  {"midiPitch", event.midiPitch},
                                  {"attack", event.attack},
                                  {"verseIndex", event.verseIndex},
                                  {"program", event.program},
                                  {"velocity", event.velocity},
                                  {"lyric", QString::fromStdString(event.lyric)}});
    return events;
}

double numericOption(const QCommandLineParser &args, const char *name, double minimum, double maximum)
{
    bool parsed = false;
    const double number = args.value(name).toDouble(&parsed);
    if (!parsed || !std::isfinite(number) || number < minimum || number > maximum)
        throw std::runtime_error(
            trText("messages.accompaniment.cli_range").arg(name).arg(minimum).arg(maximum).toStdString());
    return number;
}

void configurePractice(Project &project, const QCommandLineParser &args)
{
    if (args.isSet("staff-tempo"))
    {
        if (!project.staffPerformance)
            throw std::runtime_error(trText("messages.local_staff.program_requires_staff").toStdString());
        project.score.bpm = numericOption(args, "staff-tempo", MinimumScoreBpm, MaximumScoreBpm);
        project.processing.insert("tempoNeedsConfirmation", false);
        project.processing.insert("tempoSource", "cli-user-confirmed");
        project.processing.insert("confirmedQuarterBpm", project.score.bpm);
    }
    for (const char *name : {"staff-primary-program", "staff-other-program"})
    {
        if (!args.isSet(name))
            continue;
        if (!project.staffPerformance)
            throw std::runtime_error(trText("messages.local_staff.program_requires_staff").toStdString());
        const double program = numericOption(args, name, 0, 127);
        if (std::floor(program) != program)
            throw std::runtime_error(trText("messages.accompaniment.cli_integer").arg(name).toStdString());
        if (QString::fromLatin1(name) == "staff-primary-program")
            project.staffPerformance->primaryProgram = static_cast<int>(program);
        else
            project.staffPerformance->otherProgram = static_cast<int>(program);
    }
    if (args.isSet("generate-accompaniment"))
    {
        if (project.staffPerformance)
            throw std::runtime_error(trText("ui.staff.original_parts").toStdString());
        AccompanimentSettings settings;
        const auto pattern = args.value("accompaniment-pattern");
        if (pattern != "block" && pattern != "arpeggio" && pattern != "sparse")
            throw std::runtime_error(trText("messages.accompaniment.cli_pattern").toStdString());
        settings.pattern = pattern == "block"    ? AccompanimentPattern::BlockChords
                           : pattern == "sparse" ? AccompanimentPattern::Sparse
                                                 : AccompanimentPattern::Arpeggio;
        const auto mode = args.value("harmony-mode");
        if (mode != "auto" && mode != "major" && mode != "minor")
            throw std::runtime_error(trText("messages.accompaniment.cli_mode").toStdString());
        settings.mode = mode == "major"   ? HarmonicMode::Major
                        : mode == "minor" ? HarmonicMode::Minor
                                          : HarmonicMode::Automatic;
        if (args.isSet("harmony-tonic"))
        {
            const double tonic = numericOption(args, "harmony-tonic", 0, 11);
            if (std::floor(tonic) != tonic)
                throw std::runtime_error(
                    trText("messages.accompaniment.cli_integer").arg("harmony-tonic").toStdString());
            settings.harmonicTonic = static_cast<int>(tonic);
        }
        auto arrangement = generateAccompaniment(project.score, settings);
        if (args.isSet("accompaniment-variant"))
        {
            const auto candidates = generateAccompanimentCandidates(project.score, settings);
            const auto selected = std::find_if(
                candidates.begin(), candidates.end(), [&args](const auto &candidate)
                { return QString::fromStdString(candidate.id) == args.value("accompaniment-variant"); });
            if (selected == candidates.end())
                throw std::runtime_error(trText("messages.whole_song.variant").toStdString());
            arrangement = selected->arrangement;
        }
        if (!arrangement.valid())
        {
            for (const auto &diagnostic : arrangement.diagnostics)
                if (diagnostic.severity == DiagnosticSeverity::Error)
                    throw std::runtime_error(
                        localizeMessage(QString::fromStdString(diagnostic.message)).toStdString());
        }
        project.accompaniment = std::move(arrangement);
        project.practiceMix.accompanimentEnabled = true;
    }
    if (args.isSet("practice-mix"))
    {
        const auto mix = args.value("practice-mix");
        if (mix != "project" && mix != "melody" && mix != "accompaniment" && mix != "both" && mix != "silent")
            throw std::runtime_error(trText("messages.accompaniment.cli_mix").toStdString());
        if (mix != "project")
        {
            project.practiceMix.melodyEnabled = mix == "melody" || mix == "both";
            project.practiceMix.accompanimentEnabled = mix == "accompaniment" || mix == "both";
        }
    }
    if (args.isSet("melody-volume"))
        project.practiceMix.melodyVolume = numericOption(args, "melody-volume", 0, 1);
    if (args.isSet("accompaniment-volume"))
        project.practiceMix.accompanimentVolume = numericOption(args, "accompaniment-volume", 0, 1);
}

QJsonObject accompanimentJson(const Project &project, const Timeline &timeline)
{
    QJsonObject report{{"melodyEnabled", project.practiceMix.melodyEnabled},
                       {"accompanimentEnabled", project.practiceMix.accompanimentEnabled},
                       {"melodyVolume", project.practiceMix.melodyVolume},
                       {"accompanimentVolume", project.practiceMix.accompanimentVolume},
                       {"hasArrangement", project.accompaniment.has_value()}};
    if (!project.accompaniment)
        return report;
    const auto &arrangement = *project.accompaniment;
    const auto plan = buildAccompanimentPlan(project.score, timeline, arrangement);
    QJsonArray chords;
    for (const auto &chord : arrangement.chords)
        chords.append(QJsonObject{{"startTick", qint64(chord.startTick)},
                                  {"endTick", qint64(chord.endTick)},
                                  {"rootPitchClass", chord.rootPitchClass},
                                  {"quality", int(chord.quality)},
                                  {"inversion", chord.inversion},
                                  {"userEdited", chord.userEdited}});
    QJsonArray events;
    for (const auto &event : plan.events)
        events.append(QJsonObject{{"startTick", qint64(event.startTick)},
                                  {"durationTicks", qint64(event.durationTicks)},
                                  {"midiPitch", event.midiPitch},
                                  {"velocity", event.velocity},
                                  {"role", event.role == AccompanimentRole::Chord ? "chord" : "bass"}});
    QJsonArray diagnostics;
    for (const auto &diagnostic : plan.diagnostics)
        diagnostics.append(QJsonObject{{"error", diagnostic.severity == DiagnosticSeverity::Error},
                                       {"message", QString::fromStdString(diagnostic.message)}});
    report.insert("valid", plan.valid());
    report.insert("fingerprint", QString::fromStdString(arrangement.melodyFingerprint));
    report.insert("chords", chords);
    report.insert("events", events);
    report.insert("diagnostics", diagnostics);
    return report;
}
} // namespace

std::optional<int> runProjectCommand(const QCommandLineParser &args)
{
    const auto staffOptions = [&args]
    {
        LocalStaffRecognitionOptions options;
        const auto engine = args.value("staff-engine");
        if (engine != "crisp")
            throw std::runtime_error(trText("messages.local_staff.invalid_options").toStdString());
        options.modelPath = args.value("staff-model");
        options.engineExecutable = args.value("staff-executable");
        return options;
    };
    if (args.isSet("recognize-staff-pages"))
    {
        if (args.isSet("report") && !QDir().mkpath(QFileInfo(args.value("report")).absolutePath()))
            throw std::runtime_error(trText("app.report_error").toStdString());
        const auto paths = args.values("staff-page");
        if (paths.isEmpty() || paths.size() > 32)
            throw std::runtime_error(trText("messages.pages.too_many").toStdString());
        const auto split = args.value("staff-page-split");
        if (split != "auto" && split != "none" && split != "left-right")
            throw std::runtime_error(trText("messages.pages.split_mode").toStdString());
        const auto mode = split == "none"         ? StaffPageSplitMode::Single
                          : split == "left-right" ? StaffPageSplitMode::LeftRight
                                                  : StaffPageSplitMode::Auto;
        std::vector<StaffPageInput> pages;
        qint64 totalPixels = 0;
        for (int index = 0; index < paths.size(); ++index)
        {
            QImageReader reader(paths[index]);
            reader.setAutoTransform(true);
            const auto size = reader.size();
            if (size.width() > 12000 || size.height() > 20000 || qint64(size.width()) * size.height() > 50000000)
                throw std::runtime_error(trText("ui.error.image_large").toStdString());
            if (size.isValid() && qint64(size.width()) * size.height() > 400000000 - totalPixels)
                throw std::runtime_error(trText("messages.local_staff.page_pixel_limit").toStdString());
            const auto image = reader.read();
            if (image.isNull())
                throw std::runtime_error(reader.errorString().toStdString());
            totalPixels += qint64(image.width()) * image.height();
            if (totalPixels > 400000000)
                throw std::runtime_error(trText("messages.local_staff.page_pixel_limit").toStdString());
            auto inputs = splitStaffPageInputs(image, QFileInfo(paths[index]).fileName(), index, mode);
            pages.insert(pages.end(), inputs.begin(), inputs.end());
            if (pages.size() > 32)
                throw std::runtime_error(trText("messages.pages.too_many").toStdString());
        }
        auto options = staffOptions();
        options.timeoutSeconds = std::min(600, std::max(180, int(pages.size()) * 60));
        const auto result = recognizeLocalStaffPages(pages, options);
        if (args.isSet("report") && !result.sourceNotation.isEmpty())
        {
            const auto folder = QFileInfo(args.value("report")).absolutePath();
            if (!QDir().mkpath(folder))
                throw std::runtime_error(trText("app.report_error").toStdString());
            QFile notation(folder + "/recognition-source.krn");
            if (!notation.open(QIODevice::WriteOnly) ||
                notation.write(result.sourceNotation) != result.sourceNotation.size())
                throw std::runtime_error(trText("app.report_error").toStdString());
        }
        if (!result.valid())
            throw std::runtime_error(localizeMessage(result.error).toStdString());
        Project project = *result.project;
        configurePractice(project, args);
        if (args.isSet("out"))
            saveProject(args.value("out"), project);
        QJsonArray pageSummary;
        for (const auto &page : project.staffPages)
            pageSummary.append(QJsonObject{
                {"label", page.label}, {"startTick", double(page.startTick)}, {"endTick", double(page.endTick)}});
        const auto timeline = buildTimeline(project.score);
        QJsonObject report{{"local", true},
                           {"valid", timeline.valid()},
                           {"pages", pageSummary},
                           {"notes", int(project.staffPerformance->notes.size())},
                           {"durationTicks", double(timeline.durationTicks)},
                           {"seconds", timeline.durationSeconds()},
                           {"bpm", project.score.bpm},
                           {"writtenMeasures", int(project.score.writtenMeasures.size())},
                           {"clefChanges", int(project.staffPerformance->clefChanges.size())},
                           {"processing", project.processing},
                           {"warnings", QJsonArray::fromStringList(project.warnings)}};
        if (args.isSet("report"))
            writeJsonReport(args.value("report"), report);
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
    }
    if (args.isSet("recognize-staff-local"))
    {
        QImageReader reader(args.value("recognize-staff-local"));
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.width() > 12000 || size.height() > 20000 || qint64(size.width()) * size.height() > 50000000)
            throw std::runtime_error(trText("ui.error.image_large").toStdString());
        const QImage image = reader.read();
        if (image.isNull())
            throw std::runtime_error(reader.errorString().toStdString());
        const auto result = recognizeLocalStaff(image, args.value("recognize-staff-local"), staffOptions());
        if (args.isSet("report") && !result.sourceNotation.isEmpty())
        {
            const auto folder = QFileInfo(args.value("report")).absolutePath();
            if (!QDir().mkpath(folder))
                throw std::runtime_error(trText("app.report_error").toStdString());
            QFile notation(folder + "/recognition-source.krn");
            if (!notation.open(QIODevice::WriteOnly) ||
                notation.write(result.sourceNotation) != result.sourceNotation.size())
                throw std::runtime_error(trText("app.report_error").toStdString());
        }
        if (!result.valid())
            throw std::runtime_error(localizeMessage(result.error).toStdString());
        Project project = *result.project;
        configurePractice(project, args);
        if (args.isSet("out"))
            saveProject(args.value("out"), project);
        const auto timeline = buildTimeline(project.score);
        const QJsonObject report{{"local", true},
                                 {"valid", timeline.valid()},
                                 {"notes", int(project.staffPerformance->notes.size())},
                                 {"guideNotes", int(project.score.notes.size())},
                                 {"staves", project.staffPerformance->staffCount},
                                 {"durationTicks", qint64(timeline.durationTicks)},
                                 {"bpm", project.score.bpm},
                                 {"primaryProgram", project.staffPerformance->primaryProgram},
                                 {"otherProgram", project.staffPerformance->otherProgram},
                                 {"processing", project.processing},
                                 {"warnings", QJsonArray::fromStringList(project.warnings)}};
        if (args.isSet("report"))
            writeJsonReport(args.value("report"), report);
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
    }
    if (args.isSet("transcribe-audio"))
    {
        AudioTranscriptionOptions options;
        options.startSeconds = numericOption(args, "audio-start", 0, 1200);
        options.endSeconds = numericOption(args, "audio-end", 0.1, 1200);
        options.bpm = numericOption(args, "audio-bpm", 0, 400);
        const double tonic = numericOption(args, "audio-tonic", -1, 11);
        if (std::floor(tonic) != tonic)
            throw std::runtime_error(
                trText("messages.accompaniment.cli_integer").arg("audio-tonic").toStdString());
        options.tonic = int(tonic);
        options.recognizeLyrics = args.isSet("recognize-lyrics");
        options.wholeSong = args.isSet("audio-whole-song");
        options.language = args.value("audio-language");
        options.separateVocals = !args.isSet("input-isolated-vocals");
        options.separatorPython = args.value("separator-python");
        options.separatorScript = args.value("separator-script");
        options.separatorModelDirectory = args.value("separator-model-dir");
        options.separatorCacheDirectory = args.value("separator-cache-dir");
        options.enhanceVoice = args.isSet("enhance-voice");
        options.whisperExecutable = args.value("whisper-executable");
        options.whisperModel = args.value("whisper-model");
        if (args.isSet("lyrics-file"))
        {
            QFile file(args.value("lyrics-file"));
            if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
                throw std::runtime_error(trText("messages.audio_import.lyrics_file").toStdString());
            options.lyricsText = QString::fromUtf8(file.readAll());
        }
        const auto result = transcribeAudio(args.value("transcribe-audio"), options);
        Project project;
        project.score = result.score;
        project.image = renderNumberedScore(project.score);
        project.warnings = result.warnings;
        project.generatedNotation = true;
        project.audioSource = AudioSourceInfo{result.sourcePath.toStdString(),
                                              result.sourceDurationSeconds,
                                              result.selectedStartSeconds,
                                              result.selectedEndSeconds,
                                              result.timings,
                                              result.lyricTimings,
                                              audioTimingFingerprint(project.score)};
        project.processing = audioProcessingMetadata(result, options);
        project.audioSource->vocalsPath = result.vocalsPath.toStdString();
        project.audioSource->instrumentalPath = result.instrumentalPath.toStdString();
        project.audioSource->separationModel = result.separationModel.toStdString();
        project.audioSource->vocalsSeparated = result.vocalsSeparated;
        if (args.isSet("generate-accompaniment"))
            configurePractice(project, args);
        if (args.isSet("out"))
            saveProject(args.value("out"), project);
        if (args.isSet("notation-image") && !project.image.save(args.value("notation-image")))
            throw std::runtime_error(trText("messages.audio_import.notation_layout_failed").toStdString());
        const auto timeline = buildTimeline(project.score);
        QJsonArray timings;
        for (const auto &timing : result.timings)
            timings.append(QJsonObject{{"sourceNoteIndex", timing.sourceNoteIndex},
                                       {"startSeconds", timing.startSeconds},
                                       {"endSeconds", timing.endSeconds},
                                       {"startTick", qint64(timing.startTick)},
                                       {"endTick", qint64(timing.endTick)}});
        QJsonObject report{{"valid", timeline.valid()},
                           {"sourcePath", result.sourcePath},
                           {"vocalsSeparated", result.vocalsSeparated},
                           {"vocalsPath", result.vocalsPath},
                           {"instrumentalPath", result.instrumentalPath},
                           {"separationModel", result.separationModel},
                           {"separationManifest", result.separationManifest},
                           {"separationSeconds", result.separationSeconds},
                           {"analysisSourcePath", result.vocalsSeparated ? result.vocalsPath : result.sourcePath},
                           {"sourceDurationSeconds", result.sourceDurationSeconds},
                           {"selectedStartSeconds", result.selectedStartSeconds},
                           {"selectedEndSeconds", result.selectedEndSeconds},
                           {"estimatedBpm", result.estimatedBpm},
                           {"estimatedTonic", result.estimatedTonic},
                           {"elapsedMilliseconds", result.elapsedMilliseconds},
                           {"recognizedLyrics", result.recognizedLyrics},
                           {"warnings", QJsonArray::fromStringList(result.warnings)},
                           {"score", scoreToJson(project.score)},
                           {"events", timelineJson(timeline)},
                           {"timings", timings},
                           {"imageWidth", project.image.width()},
                           {"imageHeight", project.image.height()}};
        if (args.isSet("report"))
            writeJsonReport(args.value("report"), report);
        QTextStream(stdout) << "AUDIO_IMPORT notes=" << project.score.notes.size()
                            << " ticks=" << timeline.durationTicks << " valid=" << timeline.valid() << Qt::endl;
        return timeline.valid() && !project.score.notes.empty() ? 0 : 2;
    }
    if (args.isSet("render-wave"))
    {
        auto project = args.positionalArguments().isEmpty() ? makePracticeScore()
                                                            : loadProject(args.positionalArguments().first());
        if (project.staffPerformance && (args.isSet("velocity") || args.isSet("uniform")))
            throw std::runtime_error(trText("ui.staff.original_parts").toStdString());
        if (args.isSet("velocity"))
            project.score.baseVelocity = args.value("velocity").toInt();
        if (args.isSet("uniform"))
            project.score.accentBeats = false;
        configurePractice(project, args);
        const auto timeline = buildTimeline(project.score);
        if (project.staffPerformance && project.processing.value("tempoNeedsConfirmation").toBool())
            throw std::runtime_error(trText("messages.local_staff.tempo_needs_confirmation").toStdString());
        AccompanimentPlan plan;
        plan.durationTicks = timeline.durationTicks;
        if (project.staffPerformance)
            plan = buildStaffPerformancePlan(project.score, timeline, *project.staffPerformance);
        else if (project.accompaniment)
            plan = buildAccompanimentPlan(project.score, timeline, *project.accompaniment);
        if (!plan.valid() && !project.staffPerformance && !project.practiceMix.accompanimentEnabled)
        {
            plan = {};
            plan.durationTicks = timeline.durationTicks;
        }
        WaveRenderOptions options;
        options.maxSeconds = numericOption(args, "render-seconds", 1, 600);
        options.metronome = args.isSet("metronome");
        const double transpose = numericOption(args, "transpose", -24, 24);
        if (std::floor(transpose) != transpose)
            throw std::runtime_error(trText("messages.accompaniment.cli_integer").arg("transpose").toStdString());
        options.transpose = static_cast<int>(transpose);
        options.speed = numericOption(args, "speed", 0.25, 2);
        options.mix = project.practiceMix;
        if (project.staffPerformance)
        {
            options.settings.originalStaff = true;
            options.settings.chordProgram = project.staffPerformance->primaryProgram;
            options.settings.bassProgram = project.staffPerformance->otherProgram;
        }
        else if (project.accompaniment)
            options.settings = project.accompaniment->settings;
        if (args.isSet("gm-soundfont"))
            options.gmSoundFontPath = args.value("gm-soundfont");
        const bool legacyRender =
            options.maxSeconds <= 120 && !project.staffPerformance && !project.accompaniment &&
            project.practiceMix.melodyEnabled && !project.practiceMix.accompanimentEnabled &&
            project.practiceMix.melodyVolume == 0.9 && !args.isSet("practice-mix") &&
            !args.isSet("melody-volume") && !args.isSet("accompaniment-volume") && !args.isSet("transpose") &&
            !args.isSet("speed") && !args.isSet("metronome") && !args.isSet("gm-soundfont");
        const auto result = legacyRender
                                ? renderWave(project.score, args.value("render-wave"), options.maxSeconds)
                                : renderWave(project.score, timeline, plan, args.value("render-wave"), options);
        QJsonObject report{{"frames", result.frames},
                           {"sampleRate", result.sampleRate},
                           {"peak", result.peak},
                           {"rms", result.rms},
                           {"clippedSamples", qint64(result.clippedSamples)},
                           {"engine", result.engine},
                           {"pianoPath", result.pianoPath},
                           {"gmSoundFontPath", result.gmSoundFontPath},
                           {"baseVelocity", project.score.baseVelocity},
                           {"accentBeats", project.score.accentBeats}};
        report.insert("practice", accompanimentJson(project, timeline));
        report.insert("transpose", options.transpose);
        report.insert("speed", options.speed);
        report.insert("musicFrames", result.musicFrames);
        report.insert("startSeconds", result.startSeconds);
        report.insert("endSeconds", result.endSeconds);
        report.insert("fragment", result.fragment);
        if (project.staffPerformance)
            report.insert("staffPerformance", QJsonObject{{"notes", int(project.staffPerformance->notes.size())},
                                                          {"soundingEvents", int(plan.events.size())},
                                                          {"staves", project.staffPerformance->staffCount},
                                                          {"originalParts", true}});
        if (args.isSet("out"))
            saveProject(args.value("out"), project);
        if (args.isSet("report"))
            writeJsonReport(args.value("report"), report);
        return result.frames > 0 ? 0 : 2;
    }
    if (args.isSet("inspect") || args.isSet("generate-accompaniment") || args.isSet("whole-song-candidates"))
    {
        auto project = args.isSet("inspect")                  ? loadProject(args.value("inspect"))
                       : args.positionalArguments().isEmpty() ? makePracticeScore()
                                                              : loadProject(args.positionalArguments().first());
        configurePractice(project, args);
        const auto timeline = buildTimeline(project.score);
        QJsonObject report{{"valid", timeline.valid()},
                           {"notes", int(project.score.notes.size())},
                           {"bpm", project.score.bpm},
                           {"tonic", project.score.tonic},
                           {"durationTicks", qint64(timeline.durationTicks)}};
        report.insert("practice", accompanimentJson(project, timeline));
        if (args.isSet("whole-song-candidates"))
        {
            QJsonArray candidates;
            for (const auto &candidate : generateAccompanimentCandidates(project.score))
            {
                auto copy = project;
                copy.accompaniment = candidate.arrangement;
                candidates.append(QJsonObject{{"id", QString::fromStdString(candidate.id)},
                                              {"name", trText(candidate.labelKey.c_str())},
                                              {"practice", accompanimentJson(copy, timeline)}});
            }
            report.insert("wholeSongCandidates", candidates);
        }
        if (project.audioSource)
            report.insert("audioSource",
                          QJsonObject{{"path", QString::fromStdString(project.audioSource->path)},
                                      {"timings", int(project.audioSource->timings.size())},
                                      {"mappingCurrent", project.audioSource->timingFingerprint ==
                                                             audioTimingFingerprint(project.score)}});
        report.insert("generatedNotation", project.generatedNotation);
        if (args.isSet("out"))
            saveProject(args.value("out"), project);
        if (args.isSet("timeline"))
        {
            report.insert("events", timelineJson(timeline));
            report.insert("score", scoreToJson(project.score));
        }
        if (args.isSet("report"))
            writeJsonReport(args.value("report"), report);
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
        return timeline.valid() ? 0 : 2;
    }
    if (args.isSet("recognize"))
    {
        QImage image(args.value("recognize"));
        if (image.isNull())
            throw std::runtime_error(trText("app.image_error").toStdString());
        RecognitionResult result;
        if (args.isSet("vision-endpoint"))
        {
            VisionConfig config{args.value("vision-endpoint"), args.value("vision-model"),
                                qEnvironmentVariable("OPENAI_API_KEY")};
            bool validTimeout = false;
            config.timeoutSeconds = args.value("vision-timeout").toInt(&validTimeout);
            if (!validTimeout)
                throw std::runtime_error(trText("messages.recognition.invalid_timeout").toStdString());
            result = recognizeCloud(image, args.value("recognize"), config);
        }
        else
            result = LocalRecognizer::recognize(image, args.value("recognize"));
        Project p{result.score, image, result.warnings};
        p.staffPerformance = result.staffPerformance;
        if (p.staffPerformance)
            p.practiceMix.accompanimentEnabled = true;
        if (args.isSet("out"))
            saveProject(args.value("out"), p);
        auto timeline = buildTimeline(result.score);
        auto report = scoreToJson(result.score);
        report.insert("noteCount", int(result.score.notes.size()));
        report.insert("validTimeline", timeline.valid());
        report.insert("durationSeconds", timeline.durationSeconds());
        report.insert("warnings", QJsonArray::fromStringList(result.warnings));
        report.insert("ocrText", result.debugText);
        if (p.staffPerformance)
            report.insert("staffPerformance", staffPerformanceToJson(*p.staffPerformance));
        if (args.isSet("timeline"))
            report.insert("events", timelineJson(timeline));
        if (args.isSet("report"))
            writeJsonReport(args.value("report"), report);
        QTextStream(stdout)
            << trText("app.recognition_result").arg(result.score.notes.size()).arg(timeline.valid()) << Qt::endl;
        return result.score.notes.empty() ? 2 : 0;
    }
    return std::nullopt;
}
} // namespace singlilt

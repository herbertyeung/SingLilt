// Native CrispEmbed invocation and notation result extraction.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "CrispStaffRecognizer.h"
#include "BekernDecoder.h"
#include "CrispStaffSourceAnchors.h"
#include "StaffProcessJob.h"
#include "StaffTempoRecognizer.h"
#include "i18n/LanguageManager.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace singlilt
{
namespace
{
struct NativeStaffCancelled
{
};

void checkCancellation(const std::atomic_bool *cancellation)
{
    if (cancellation && cancellation->load(std::memory_order_relaxed))
        throw NativeStaffCancelled{};
}

[[noreturn]] void fail(const char *key, const QString &detail = {})
{
    throw std::runtime_error((detail.isEmpty() ? trText(key) : trText(key).arg(detail)).toStdString());
}

QString fileHash(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        fail("messages.local_staff.native_model_missing", path);
    return QString::fromLatin1(hash.result().toHex());
}

QByteArray recognizePage(const QString &executable, const QString &model, const QString &imagePath,
                         const LocalStaffRecognitionOptions &options, QElapsedTimer &elapsed, QString &engineLog,
                         const std::atomic_bool *cancellation)
{
    QProcess process;
#ifdef Q_OS_WIN
    StaffProcessJob job;
    if (!job.configure(process))
        fail("messages.local_staff.process_guard_failed");
#endif
    process.setProgram(executable);
    auto arguments = options.engineArgumentsPrefix;
    arguments.append({"-m", model, "-t", "4", "--offline", "--ocr", imagePath});
    process.setArguments(arguments);
    process.setWorkingDirectory(QFileInfo(executable).absolutePath());
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("CRISPEMBED_OFFLINE", "1");
    environment.insert("HF_HUB_OFFLINE", "1");
    process.setProcessEnvironment(environment);
    QByteArray notation;
    QByteArray log;
    bool outputLimit = false;
    const auto drain = [&]
    {
        const auto output = process.readAllStandardOutput();
        if (notation.size() + output.size() > 8 * 1024 * 1024)
            outputLimit = true;
        else
            notation.append(output);
        log.append(process.readAllStandardError());
        if (log.size() > 65536)
            log = log.right(65536);
        engineLog = QString::fromUtf8(log);
    };
    const auto stop = [&]
    {
#ifdef Q_OS_WIN
        job.terminate();
#endif
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            process.waitForFinished(5000);
        }
#ifdef Q_OS_WIN
        job.waitForEmpty(5000);
#endif
        drain();
    };
    try
    {
        checkCancellation(cancellation);
        process.start();
        while (process.state() != QProcess::NotRunning)
        {
            if (process.state() == QProcess::Starting)
                process.waitForStarted(100);
            else
                process.waitForFinished(100);
            drain();
            checkCancellation(cancellation);
            if (elapsed.elapsed() >= options.timeoutSeconds * 1000LL)
                fail("messages.local_staff.timeout", QString::number(options.timeoutSeconds));
            if (outputLimit)
                fail("messages.local_staff.invalid_export");
        }
        drain();
        checkCancellation(cancellation);
        if (process.error() == QProcess::FailedToStart)
            fail("messages.local_staff.process_failed", process.errorString().left(512));
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            fail("messages.local_staff.process_failed", QString::number(process.exitCode()));
        if (outputLimit || notation.trimmed().isEmpty())
            fail("messages.local_staff.invalid_export");
        stop();
        return notation;
    }
    catch (...)
    {
        stop();
        throw;
    }
}

void appendPage(MusicXmlImportResult &book, MusicXmlImportResult page, int pageIndex, std::int64_t offset)
{
    if (page.parts.size() != 1 || page.parts.front().measures.empty())
        fail("messages.local_staff.native_structure");
    if (book.parts.empty())
    {
        book.parts.push_back(page.parts.front());
        book.parts.front().measures.clear();
        book.parts.front().attributes.clear();
        book.parts.front().repeats.clear();
        book.title = page.title;
    }
    auto &part = book.parts.front();
    if (part.staffCount != page.parts.front().staffCount)
        fail("messages.local_staff.native_structure");
    const int measureOffset = int(part.measures.size());
    for (auto measure : page.parts.front().measures)
    {
        measure.startTick += offset;
        measure.endTick += offset;
        measure.pageIndex = pageIndex;
        part.measures.push_back(std::move(measure));
    }
    for (auto attributes : page.parts.front().attributes)
    {
        attributes.startTick += offset;
        attributes.measure += measureOffset;
        part.attributes.push_back(std::move(attributes));
    }
    for (auto repeat : page.parts.front().repeats)
    {
        repeat.firstMeasure += measureOffset;
        repeat.endMeasure += measureOffset;
        part.repeats.push_back(repeat);
    }
    part.conversionErrors.append(page.parts.front().conversionErrors);
    for (auto &track : page.tracks)
    {
        auto existing = std::find_if(book.tracks.begin(), book.tracks.end(), [&](const auto &candidate)
                                     { return candidate.staff == track.staff && candidate.voice == track.voice; });
        if (existing == book.tracks.end())
        {
            auto newTrack = track;
            newTrack.events.clear();
            book.tracks.push_back(std::move(newTrack));
            existing = std::prev(book.tracks.end());
        }
        for (auto event : track.events)
        {
            event.startTick += offset;
            event.endTick += offset;
            event.measure += measureOffset;
            event.pageIndex = pageIndex;
            existing->events.push_back(std::move(event));
        }
    }
    for (auto tempo : page.tempos)
    {
        tempo.startTick += offset;
        book.tempos.push_back(tempo);
    }
    book.warnings.append(page.warnings);
}

std::size_t primaryTrack(const MusicXmlImportResult &imported, std::size_t requested)
{
    if (requested != std::numeric_limits<std::size_t>::max())
        return requested;
    std::size_t best = std::numeric_limits<std::size_t>::max();
    std::size_t count = 0;
    for (std::size_t index = 0; index < imported.tracks.size(); ++index)
    {
        const auto &track = imported.tracks[index];
        if (track.staff != 1)
            continue;
        const auto pitched = std::count_if(track.events.begin(), track.events.end(),
                                           [](const auto &event) { return !event.rest && event.midiPitch >= 0; });
        if (std::size_t(pitched) > count)
        {
            count = std::size_t(pitched);
            best = index;
        }
    }
    return best;
}
} // namespace

LocalStaffRecognitionResult recognizeCrispStaffPages(const std::vector<StaffPageInput> &pages,
                                                     const LocalStaffRecognitionOptions &options,
                                                     const std::atomic_bool *cancellation)
{
    LocalStaffRecognitionResult result;
    if (!pages.empty())
        result.originalImage = pages.front().image;
    for (const auto &page : pages)
        result.originalImages.push_back(page.image);
    QElapsedTimer elapsed;
    elapsed.start();
    try
    {
        checkCancellation(cancellation);
        if (options.timeoutSeconds < 1 || options.timeoutSeconds > 600 || pages.empty() || pages.size() > 32)
            fail("messages.local_staff.invalid_options");
        qint64 pixels = 0;
        for (const auto &page : pages)
        {
            const auto count = qint64(page.image.width()) * page.image.height();
            if (page.image.isNull() || page.image.width() > 12000 || page.image.height() > 20000 ||
                count > 50000000 || page.sourceIndex < 0)
                fail("messages.recognition.invalid_image");
            pixels += count;
            if (pixels > 400000000)
                fail("messages.local_staff.page_pixel_limit");
        }
        const QString runtime = QCoreApplication::applicationDirPath() + "/tools/omr-native";
        const QString executable =
            options.engineExecutable.isEmpty() ? runtime + "/crispembed.exe" : options.engineExecutable;
        const QString model = options.modelPath.isEmpty() ? runtime + "/models/staff.gguf" : options.modelPath;
        if (!QFileInfo(executable).isFile())
            fail("messages.local_staff.native_missing");
        if (!QFileInfo(model).isFile())
            fail("messages.local_staff.native_model_missing", model);
        const QString modelSha256 = fileHash(model);
        QJsonObject manifest;
        QFile manifestFile(runtime + "/runtime-manifest.json");
        if (manifestFile.open(QIODevice::ReadOnly) && manifestFile.size() <= 1024 * 1024)
            manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        const auto expected = manifest.value("modelSha256").toString();
        if (options.modelPath.isEmpty())
        {
            if (!QRegularExpression("^[0-9a-fA-F]{64}$").match(expected).hasMatch() ||
                expected.compare(modelSha256, Qt::CaseInsensitive) != 0)
                fail("messages.local_staff.native_model_hash");
        }
        QTemporaryDir temporary(QDir::tempPath() + "/singlilt-native-staff-XXXXXX");
        if (!temporary.isValid())
            fail("messages.local_staff.temporary_failed");
        MusicXmlImportResult imported;
        QJsonArray metadata;
        QJsonArray strictRhythmErrors;
        bool rhythmReviewUsed = false;
        std::int64_t offset = 0;
        for (std::size_t index = 0; index < pages.size(); ++index)
        {
            checkCancellation(cancellation);
            const auto &page = pages[index];
            auto canvas = makeStaffInputCanvas(page.image);
            // Transcoda resizes to width 1050 then takes 1485 rows. Padding avoids cropping tall pages.
            const int paddedWidth = int((qint64(canvas.height()) * 1050 + 1484) / 1485);
            const bool padded = paddedWidth > canvas.width();
            if (padded)
            {
                if (paddedWidth > 12000 || qint64(paddedWidth) * canvas.height() > 50000000)
                    fail("messages.local_staff.native_structure");
                QImage paddedImage(paddedWidth, canvas.height(), QImage::Format_RGB32);
                if (paddedImage.isNull())
                    fail("messages.local_staff.temporary_failed");
                paddedImage.fill(Qt::white);
                QPainter painter(&paddedImage);
                painter.drawImage(0, 0, canvas);
                painter.end();
                canvas = std::move(paddedImage);
            }
            const QString input = temporary.filePath(QString("page-%1.png").arg(index));
            if (!canvas.save(input, "PNG"))
                fail("messages.local_staff.temporary_failed");
            QString pageLog;
            QByteArray notation;
            try
            {
                notation = recognizePage(executable, model, input, options, elapsed, pageLog, cancellation);
            }
            catch (...)
            {
                result.engineLog += QString("\nPage %1\n").arg(index + 1) + pageLog;
                result.engineLog = result.engineLog.right(65536);
                throw;
            }
            result.engineLog += QString("\nPage %1\n").arg(index + 1) + pageLog;
            result.engineLog = result.engineLog.right(65536);
            BekernDecodeOptions decodeOptions;
            decodeOptions.pageFragment = true;
            decodeOptions.preserveGraceAsReviewAnnotation = true;
            if (!imported.parts.empty() && !imported.parts.front().attributes.empty())
                decodeOptions.initialAttributes = imported.parts.front().attributes.back();
            auto decoded = decodeBekern(QString::fromUtf8(notation), page.label, decodeOptions);
            QJsonArray systemMetadata;
            QString fullPageError;
            bool pageRhythmReview = false;
            if (!decoded.valid())
            {
                const auto strictError = decoded.error;
                auto reviewOptions = decodeOptions;
                reviewOptions.reviewRhythmConflicts = true;
                auto reviewed = decodeBekern(QString::fromUtf8(notation), page.label, reviewOptions);
                if (reviewed.valid())
                {
                    strictRhythmErrors.append(strictError);
                    fullPageError = strictError;
                    pageRhythmReview = true;
                    rhythmReviewUsed = true;
                    decoded = std::move(reviewed);
                }
            }
            if (decoded.valid())
            {
                result.sourceNotation += notation + '\n';
                appendPage(imported, std::move(decoded), int(index), offset);
                offset = imported.parts.front().measures.back().endTick;
            }
            else
            {
                fullPageError = decoded.error;
                const auto regions = crispStaffSystemRegions(page.image);
                if (regions.empty() || regions.size() > 32)
                    throw std::runtime_error(decoded.error.toStdString());
                for (std::size_t system = 0; system < regions.size(); ++system)
                {
                    checkCancellation(cancellation);
                    const auto rectangle = regions[system];
                    const auto systemImage = makeStaffInputCanvas(page.image.copy(rectangle));
                    const auto systemPath =
                        temporary.filePath(QString("page-%1-system-%2.png").arg(index).arg(system));
                    if (systemImage.isNull() || !systemImage.save(systemPath, "PNG"))
                        fail("messages.local_staff.temporary_failed");
                    QString systemLog;
                    QByteArray systemNotation;
                    try
                    {
                        systemNotation = recognizePage(executable, model, systemPath, options, elapsed, systemLog,
                                                       cancellation);
                    }
                    catch (...)
                    {
                        result.engineLog += systemLog;
                        result.engineLog = result.engineLog.right(65536);
                        throw;
                    }
                    result.engineLog +=
                        QString("\nPage %1 system %2\n").arg(index + 1).arg(system + 1) + systemLog;
                    result.engineLog = result.engineLog.right(65536);
                    auto systemOptions = decodeOptions;
                    if (!imported.parts.empty() && !imported.parts.front().attributes.empty())
                        systemOptions.initialAttributes = imported.parts.front().attributes.back();
                    auto systemDecoded =
                        decodeBekern(QString::fromUtf8(systemNotation), page.label, systemOptions);
                    if (!systemDecoded.valid())
                    {
                        const auto strictError = systemDecoded.error;
                        systemOptions.reviewRhythmConflicts = true;
                        auto reviewed = decodeBekern(QString::fromUtf8(systemNotation), page.label, systemOptions);
                        if (reviewed.valid())
                        {
                            strictRhythmErrors.append(strictError);
                            pageRhythmReview = true;
                            rhythmReviewUsed = true;
                            systemDecoded = std::move(reviewed);
                        }
                    }
                    if (!systemDecoded.valid())
                        throw std::runtime_error(QString("Page %1 system %2: %3")
                                                     .arg(index + 1)
                                                     .arg(system + 1)
                                                     .arg(systemDecoded.error)
                                                     .toStdString());
                    appendPage(imported, std::move(systemDecoded), int(index), offset);
                    offset = imported.parts.front().measures.back().endTick;
                    result.sourceNotation +=
                        "!!!source-system: " + QByteArray::number(system + 1) + '\n' + systemNotation + '\n';
                    systemMetadata.append(
                        QJsonObject{{"sourceRect", QJsonArray{rectangle.x(), rectangle.y(), rectangle.width(),
                                                              rectangle.height()}},
                                    {"engineInputSHA256", fileHash(systemPath)}});
                }
                imported.warnings.append(QString("Full-page recognition failed structural validation; all %1 "
                                                 "detected staff systems were recognized separately. %2")
                                             .arg(regions.size())
                                             .arg(fullPageError));
            }
            metadata.append(
                QJsonObject{{"label", page.label},
                            {"sourceIndex", page.sourceIndex},
                            {"sourceRect", QJsonArray{page.sourceRect.x(), page.sourceRect.y(),
                                                      page.sourceRect.width(), page.sourceRect.height()}},
                            {"sourceWidth", page.image.width()},
                            {"sourceHeight", page.image.height()},
                            {"engineInputWidth", canvas.width()},
                            {"engineInputHeight", canvas.height()},
                            {"engineInputPadding", padded},
                            {"engineInputCropped", false},
                            {"recognitionRoute", systemMetadata.isEmpty()
                                                     ? (pageRhythmReview ? "full-page-review" : "full-page")
                                                     : "system-crops"},
                            {"systems", systemMetadata},
                            {"fullPageValidationError", fullPageError},
                            {"engineInputSHA256", fileHash(input)}});
        }
        imported.sourceSha256 =
            QCryptographicHash::hash(result.sourceNotation, QCryptographicHash::Sha256).toHex();
        const auto selected = primaryTrack(imported, options.primaryTrackIndex);
        const auto converted = musicXmlToPerformance(imported, selected, true);
        if (!converted.valid())
            throw std::runtime_error(converted.error.toStdString());
        using WrittenEvent = std::tuple<int, int, std::string, int, std::int64_t, std::int64_t>;
        std::vector<WrittenEvent> decodedEvents;
        std::vector<WrittenEvent> retainedEvents;
        for (const auto &track : imported.tracks)
            for (const auto &event : track.events)
                if (!event.rest && event.midiPitch >= 0)
                    decodedEvents.emplace_back(event.pageIndex, track.staff,
                                               (track.partId + ':' + track.voice).toStdString(), event.midiPitch,
                                               event.startTick, event.endTick);
        for (const auto &event : converted.performance->notes)
            retainedEvents.emplace_back(event.pageIndex, event.staff, event.voice, event.midiPitch,
                                        event.startTick, event.startTick + event.durationTicks);
        std::sort(decodedEvents.begin(), decodedEvents.end());
        std::sort(retainedEvents.begin(), retainedEvents.end());
        if (decodedEvents != retainedEvents)
            fail("messages.local_staff.native_structure");
        Project candidate;
        candidate.score = *converted.selectedMelody.score;
        candidate.score.title = QFileInfo(pages.front().label).completeBaseName().toStdString();
        candidate.score.imagePath = QFileInfo(pages.front().label).fileName().toStdString();
        for (auto &note : candidate.score.notes)
            note.confidence = 0.5;
        candidate.staffPerformance = converted.performance;
        candidate.notationStyle = NotationStyle::Staff;
        candidate.staffImagePlayback = true;
        candidate.image = pages.front().image;
        candidate.staffKeyFifths = converted.selectedMelody.keyFifths;
        candidate.staffMinor = converted.selectedMelody.minorKey;
        candidate.staffBassClef = converted.selectedMelody.clef.sign == "F";
        candidate.practiceMix.accompanimentEnabled = candidate.staffPerformance->staffCount > 1;
        candidate.warnings = imported.warnings + converted.warnings;
        candidate.warnings.append(QStringLiteral("messages.local_staff.review"));
        if (rhythmReviewUsed)
            candidate.warnings.append(QStringLiteral("messages.local_staff.native_rhythm_review"));
        for (std::size_t index = 0; index < pages.size(); ++index)
        {
            const auto first =
                std::find_if(candidate.score.writtenMeasures.begin(), candidate.score.writtenMeasures.end(),
                             [index](const auto &measure) { return measure.pageIndex == int(index); });
            const auto last =
                std::find_if(candidate.score.writtenMeasures.rbegin(), candidate.score.writtenMeasures.rend(),
                             [index](const auto &measure) { return measure.pageIndex == int(index); });
            if (first == candidate.score.writtenMeasures.end() || last == candidate.score.writtenMeasures.rend())
                fail("messages.local_staff.native_structure");
            candidate.staffPages.push_back({pages[index].label, pages[index].image, pages[index].image,
                                            first->startTick, last->startTick + last->durationTicks});
        }
        const auto anchors = applyCrispStaffSourceAnchors(result.originalImages, imported, candidate.score,
                                                          *candidate.staffPerformance, cancellation);
        checkCancellation(cancellation);
        candidate.warnings.append(anchors.warnings);
        candidate.processing = {
            {"engine", "CrispEmbed"},
            {"model", manifest.value("modelName")},
            {"modelSHA256", modelSha256},
            {"local", true},
            {"confidenceMeasured", false},
            {"sourcePageCount", int(pages.size())},
            {"sourcePages", metadata},
            {"sourceNotationSHA256", QString::fromLatin1(imported.sourceSha256)},
            {"sourceAnchorMethod", "native pixel noteheads / symbolic event alignment"},
            {"sourceAnchoredNotes", anchors.anchoredNotes},
            {"sourceUnanchoredNotes", anchors.unanchoredNotes},
            {"sourceAnchoredGuideNotes", anchors.anchoredGuideNotes},
            {"sourceAnchorCoverage", double(anchors.anchoredNotes) / candidate.staffPerformance->notes.size()},
            {"sourceAnchorCoverageIsMusicAccuracy", false},
            {"sourceAnchorError", anchors.error},
            {"sourceDetectedSystems", anchors.detectedSystems},
            {"sourceCoveredSystems", anchors.coveredSystems},
            {"sourceDetectedNoteheads", anchors.detectedNoteheads},
            {"sourceUnmatchedDetectedNoteheads", anchors.unmatchedDetectedNoteheads},
            {"pageFragmentReview", true},
            {"recognitionAccuracyVerified", false},
            {"modelStopReason", "not-reported"},
            {"strictRhythmValid", !rhythmReviewUsed},
            {"strictRhythmErrors", strictRhythmErrors},
            {"rhythmInterpretation", rhythmReviewUsed ? "record-clock-review" : "strict-kern"},
            {"decodedPitchedNotes", int(decodedEvents.size())},
            {"retainedPitchedNotes", int(retainedEvents.size())},
            {"allDecodedPitchedEventsRetained", true},
            {"elapsedMilliseconds", elapsed.elapsed()}};
        const bool nativeTempo = std::any_of(imported.tempos.begin(), imported.tempos.end(),
                                             [](const auto &tempo) { return tempo.startTick == 0; });
        candidate.processing.insert("tempoNeedsConfirmation", !nativeTempo);
        if (nativeTempo)
            candidate.processing.insert("tempoStatus", "native-tempo");
        else
        {
            const auto suggestion = recognizeStaffTempo(pages.front().image);
            checkCancellation(cancellation);
            candidate.processing.insert("tempoStatus", suggestion.status);
            if (suggestion.quarterBpm)
                candidate.processing.insert("tempoSuggestion", *suggestion.quarterBpm);
            candidate.processing.insert("tempoOcrText", suggestion.ocrText);
            candidate.warnings.append(QStringLiteral("messages.local_staff.tempo_needs_confirmation"));
        }
        candidate.warnings.removeDuplicates();
        candidate = projectFromJson(projectToJson(candidate), candidate.image, candidate.staffPages);
        checkCancellation(cancellation);
        result.project = std::move(candidate);
    }
    catch (const NativeStaffCancelled &)
    {
        result.cancelled = true;
        result.error = QStringLiteral("messages.local_staff.cancelled");
    }
    catch (const std::exception &error)
    {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}
} // namespace singlilt

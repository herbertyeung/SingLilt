// Local vocal separation and stem-cache validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "VocalSeparator.h"
#include "i18n/LanguageManager.h"
#include "platform/RuntimePaths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace singlilt
{
namespace
{
struct Cancelled
{
};

void checkCancellation(const std::shared_ptr<std::atomic_bool> &cancellation)
{
    if (cancellation && cancellation->load(std::memory_order_relaxed))
        throw Cancelled{};
}

[[noreturn]] void fail(const char *key, const QString &detail = {})
{
    throw std::runtime_error((detail.isEmpty() ? trText(key) : trText(key).arg(detail)).toStdString());
}

QString sha256(const QString &path, const std::shared_ptr<std::atomic_bool> &cancellation)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail("messages.vocal_separation.file_failed", file.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
    {
        checkCancellation(cancellation);
        const auto bytes = file.read(4 * 1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError)
            fail("messages.vocal_separation.file_failed", file.errorString());
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool validHash(const QString &hash)
{
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(),
                                            [](QChar character)
                                            {
                                                return (character >= '0' && character <= '9') ||
                                                       (character >= 'a' && character <= 'f') ||
                                                       (character >= 'A' && character <= 'F');
                                            });
}

qint64 integerField(const QJsonObject &object, const char *name)
{
    const auto value = object.value(QLatin1String(name));
    const double number = value.toDouble(-1.0);
    if (!value.isDouble() || !std::isfinite(number) || number < 0.0 || number > 1000000000.0 ||
        std::floor(number) != number)
        fail("messages.vocal_separation.invalid_manifest");
    return static_cast<qint64>(number);
}

QString artifactPath(const QString &value, const QDir &bundle)
{
    const QFileInfo file(QDir::isAbsolutePath(value) ? value : bundle.absoluteFilePath(value));
    const auto path = file.canonicalFilePath();
    const auto directory = QDir::cleanPath(bundle.canonicalPath()) + '/';
    if (value.isEmpty() || !file.isFile() || !path.startsWith(directory, Qt::CaseInsensitive))
        fail("messages.vocal_separation.invalid_manifest");
    return path;
}

void verifyWave(const QString &path, int sampleRate, int channels, qint64 expectedFrames)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail("messages.vocal_separation.file_failed", file.errorString());
    const auto header = file.read(65536);
    if (header.size() < 12 || header.first(4) != "RIFF" || header.mid(8, 4) != "WAVE")
        fail("messages.vocal_separation.invalid_manifest");
    bool formatFound = false;
    quint16 blockAlign = 0;
    for (qsizetype offset = 12; offset + 8 <= header.size();)
    {
        const auto tag = header.mid(offset, 4);
        const auto size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(header.data() + offset + 4));
        const auto content = offset + 8;
        if (tag == "fmt ")
        {
            if (size < 16 || content + 16 > header.size())
                fail("messages.vocal_separation.invalid_manifest");
            const auto bytes = reinterpret_cast<const uchar *>(header.data() + content);
            const auto format = qFromLittleEndian<quint16>(bytes);
            const auto actualChannels = qFromLittleEndian<quint16>(bytes + 2);
            const auto actualRate = qFromLittleEndian<quint32>(bytes + 4);
            blockAlign = qFromLittleEndian<quint16>(bytes + 12);
            const auto bits = qFromLittleEndian<quint16>(bytes + 14);
            if ((format != 1 && format != 3 && format != 65534) || actualChannels != channels ||
                actualRate != static_cast<quint32>(sampleRate) || (bits != 16 && bits != 32) ||
                blockAlign != channels * bits / 8)
                fail("messages.vocal_separation.invalid_manifest");
            formatFound = true;
        }
        else if (tag == "data")
        {
            if (!formatFound || blockAlign == 0 || size % blockAlign != 0 || size / blockAlign != expectedFrames ||
                file.size() < content + static_cast<qint64>(size))
                fail("messages.vocal_separation.invalid_manifest");
            return;
        }
        const auto next = content + static_cast<qint64>(size) + (size % 2);
        if (next > header.size())
            break;
        offset = next;
    }
    fail("messages.vocal_separation.invalid_manifest");
}

VocalSeparationResult validateManifest(const QString &path, const QString &original, const QString &inputHash,
                                       const QString &cacheDirectory,
                                       const std::shared_ptr<std::atomic_bool> &cancellation)
{
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto cache = QDir::cleanPath(QDir(cacheDirectory).canonicalPath()) + '/';
    if (canonical.isEmpty() || !canonical.startsWith(cache, Qt::CaseInsensitive))
        fail("messages.vocal_separation.invalid_manifest");
    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        fail("messages.vocal_separation.invalid_manifest");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        fail("messages.vocal_separation.invalid_manifest");
    const auto manifest = document.object();
    if (integerField(manifest, "schema") != 1 || !manifest.value("complete").isBool() ||
        !manifest.value("complete").toBool() || manifest.value("model").toString() != "htdemucs" ||
        manifest.value("modelVersion").toString() != "demucs-infer-4.2.2" ||
        manifest.value("modelSignature").toString() != "955717e8" || !manifest.value("fullInput").isBool() ||
        !manifest.value("fullInput").toBool() || !validHash(manifest.value("modelSHA256").toString()) ||
        manifest.value("inputSHA256").toString().compare(inputHash, Qt::CaseInsensitive) != 0 ||
        QFileInfo(manifest.value("source").toString())
                .canonicalFilePath()
                .compare(original, Qt::CaseInsensitive) != 0 ||
        integerField(manifest, "offset0") != 0)
        fail("messages.vocal_separation.invalid_manifest");
    VocalSeparationResult result;
    result.originalPath = original;
    result.inputSha256 = inputHash;
    result.manifestPath = canonical;
    result.model = manifest.value("model").toString();
    result.frames = integerField(manifest, "inputFrames");
    result.sampleRate = static_cast<int>(integerField(manifest, "sampleRate"));
    result.channels = static_cast<int>(integerField(manifest, "channels"));
    result.separationSeconds = manifest.value("separationSeconds").toDouble(-1.0);
    const double maximumResidual = manifest.value("reconstructionMaxAbs").toDouble(-1.0);
    const double rmsResidual = manifest.value("reconstructionRms").toDouble(-1.0);
    if (result.frames <= 0 || result.sampleRate != 44100 || result.channels != 2 ||
        integerField(manifest, "outputFrames") != result.frames ||
        integerField(manifest, "vocalsFrames") != result.frames ||
        integerField(manifest, "instrumentalFrames") != result.frames ||
        !std::isfinite(result.separationSeconds) || result.separationSeconds < 0.0 ||
        !std::isfinite(maximumResidual) || maximumResidual < 0.0 || !std::isfinite(rmsResidual) ||
        rmsResidual < 0.0 || rmsResidual > maximumResidual + 1e-7)
        fail("messages.vocal_separation.invalid_manifest");
    result.durationSeconds = static_cast<double>(result.frames) / result.sampleRate;
    if (result.durationSeconds > 1200.0)
        fail("messages.audio_transcription.duration_limit");
    const QDir bundle = QFileInfo(canonical).absoluteDir();
    result.vocalsPath = artifactPath(manifest.value("vocals").toString(), bundle);
    result.instrumentalPath = artifactPath(manifest.value("instrumental").toString(), bundle);
    if (result.vocalsPath == result.instrumentalPath || result.vocalsPath == original ||
        result.instrumentalPath == original)
        fail("messages.vocal_separation.invalid_manifest");
    for (const auto &entry :
         {std::pair{result.vocalsPath, "vocalsSHA256"}, std::pair{result.instrumentalPath, "instrumentalSHA256"}})
    {
        const auto expected = manifest.value(QLatin1String(entry.second)).toString();
        if (!validHash(expected) || sha256(entry.first, cancellation).compare(expected, Qt::CaseInsensitive) != 0)
            fail("messages.vocal_separation.invalid_manifest");
        verifyWave(entry.first, result.sampleRate, result.channels, result.frames);
    }
    checkCancellation(cancellation);
    return result;
}
} // namespace

VocalSeparationResult separateVocals(const QString &path, const VocalSeparationOptions &options,
                                     const std::shared_ptr<std::atomic_bool> &cancellation,
                                     const VocalSeparationProgress &progress)
{
    QElapsedTimer elapsed;
    elapsed.start();
    VocalSeparationResult result;
    result.originalPath = QFileInfo(path).canonicalFilePath();
    QProcess process;
    try
    {
        checkCancellation(cancellation);
        if (result.originalPath.isEmpty() || !QFileInfo(result.originalPath).isFile())
            fail("messages.audio_source.missing_file", path);
        const auto appDirectory = QCoreApplication::applicationDirPath();
        QString python = options.pythonExecutable;
        if (python.isEmpty())
            python = qEnvironmentVariable("JIANPU_SEPARATOR_PYTHON");
        if (python.isEmpty())
            python = appDirectory + SeparatorPythonPath;
        const QString script = options.workerScript.isEmpty()
                                   ? appDirectory + "/tools/separation/separate_vocals.py"
                                   : options.workerScript;
        const QString models =
            options.modelDirectory.isEmpty() ? appDirectory + "/tools/separation/models" : options.modelDirectory;
        QString cache = options.cacheDirectory;
        if (cache.isEmpty())
            cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/vocal-separation";
        cache = QFileInfo(cache).absoluteFilePath();
        if (!QFileInfo(python).isFile() || !QFileInfo(script).isFile() || !QDir(models).exists())
            fail("messages.vocal_separation.missing_runtime");
        if (!QDir().mkpath(cache))
            fail("messages.vocal_separation.file_failed", cache);
        if (progress)
            progress(0, "messages.audio_transcription.progress_separate");
        const auto inputHash = sha256(result.originalPath, cancellation);
        // Saved projects retain these paths; another song must use another bundle.
        const auto jobDirectory = QDir(cache).absoluteFilePath(inputHash + "-955717e8-demucs-infer-4.2.2");
        if (!QDir().mkpath(jobDirectory))
            fail("messages.vocal_separation.file_failed", jobDirectory);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("PYTHONHOME");
        environment.remove("PYTHONPATH");
        environment.insert("PYTHONUTF8", "1");
        environment.insert("PYTHONIOENCODING", "utf-8");
        environment.insert("PYTHONUNBUFFERED", "1");
        environment.insert("PYTHONNOUSERSITE", "1");
        process.setProcessEnvironment(environment);
        process.setProgram(QFileInfo(python).absoluteFilePath());
        process.setArguments({QFileInfo(script).absoluteFilePath(), "--input", result.originalPath, "--output-dir",
                              jobDirectory, "--model-dir", QFileInfo(models).absoluteFilePath()});
        process.setWorkingDirectory(QFileInfo(script).absolutePath());
        process.start();
        bool started = process.waitForStarted(100);
        while (!started && process.state() == QProcess::Starting)
        {
            checkCancellation(cancellation);
            if (elapsed.elapsed() > 10000)
                fail("messages.vocal_separation.process_failed", process.errorString());
            started = process.waitForStarted(100);
        }
        if (!started)
            fail("messages.vocal_separation.process_failed", process.errorString());
        QByteArray output;
        QByteArray diagnostic;
        QString manifestPath;
        QString workerError;
        bool cacheHit = false;
        const auto consumeLine = [&](const QByteArray &line)
        {
            if (line.trimmed().isEmpty())
                return;
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                fail("messages.vocal_separation.invalid_protocol");
            const auto message = document.object();
            const auto type = message.value("type").toString();
            if (type == "progress")
            {
                const double percent = message.value("percent").toDouble(-1.0);
                if (!std::isfinite(percent) || percent < 0.0 || percent > 100.0)
                    fail("messages.vocal_separation.invalid_protocol");
                if (progress)
                    progress(static_cast<int>(percent), "messages.audio_transcription.progress_separate");
            }
            else if (type == "result")
            {
                if (!manifestPath.isEmpty() || !message.value("manifest").isString())
                    fail("messages.vocal_separation.invalid_protocol");
                manifestPath = message.value("manifest").toString();
                cacheHit = message.value("cacheHit").toBool();
            }
            else if (type == "error")
                workerError = message.value("message").toString();
            else
                fail("messages.vocal_separation.invalid_protocol");
        };
        const auto readOutput = [&]
        {
            output.append(process.readAllStandardOutput());
            diagnostic.append(process.readAllStandardError());
            if (diagnostic.size() > 65536)
                diagnostic = diagnostic.right(65536);
            for (auto newline = output.indexOf('\n'); newline >= 0; newline = output.indexOf('\n'))
            {
                consumeLine(output.first(newline));
                output.remove(0, newline + 1);
            }
            if (output.size() > 1024 * 1024)
                fail("messages.vocal_separation.invalid_protocol");
        };
        while (process.state() != QProcess::NotRunning)
        {
            process.waitForFinished(100);
            readOutput();
            checkCancellation(cancellation);
            if (elapsed.elapsed() > 2 * 60 * 60 * 1000)
                fail("messages.vocal_separation.timeout");
        }
        readOutput();
        if (!output.trimmed().isEmpty())
            consumeLine(output);
        checkCancellation(cancellation);
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !workerError.isEmpty())
            fail("messages.vocal_separation.process_failed",
                 workerError.isEmpty() ? QString::fromUtf8(diagnostic.right(4000)) : workerError);
        if (manifestPath.isEmpty())
            fail("messages.vocal_separation.invalid_protocol");
        result = validateManifest(manifestPath, result.originalPath, inputHash, jobDirectory, cancellation);
        if (sha256(result.originalPath, cancellation).compare(inputHash, Qt::CaseInsensitive) != 0)
            fail("messages.vocal_separation.source_changed");
        result.cacheHit = cacheHit;
        result.separationSeconds = elapsed.elapsed() / 1000.0;
        if (progress)
            progress(100, "messages.audio_transcription.progress_separate");
        return result;
    }
    catch (const Cancelled &)
    {
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            process.waitForFinished(5000);
        }
        const auto original = result.originalPath;
        result = {};
        result.originalPath = original;
        result.cancelled = true;
        return result;
    }
    catch (...)
    {
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            process.waitForFinished(5000);
        }
        throw;
    }
}
} // namespace singlilt

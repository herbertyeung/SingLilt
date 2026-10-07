// FFmpeg runs on the transcription worker; subprocess waits never reach the UI thread.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "AudioDecoder.h"
#include "i18n/LanguageManager.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace singlilt
{
namespace
{
QByteArray runDecoder(const QString &name, const QStringList &arguments, qsizetype outputLimit,
                      const std::atomic_bool *cancellation)
{
    if (cancellation && cancellation->load(std::memory_order_relaxed))
        throw AudioDecodeCancelled{};
    const QString executable = QStandardPaths::findExecutable(name);
    if (executable.isEmpty())
        throw std::runtime_error(QString("Install %1 and make it available on PATH.").arg(name).toStdString());
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start();
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray output;
    QByteArray diagnostic;
    const auto stop = [&]
    {
        process.kill();
        process.waitForFinished(5000);
    };
    do
    {
        if (process.state() == QProcess::Starting)
            process.waitForStarted(100);
        else
            process.waitForFinished(100);
        output.append(process.readAllStandardOutput());
        diagnostic.append(process.readAllStandardError());
        diagnostic = diagnostic.right(16384);
        if (cancellation && cancellation->load(std::memory_order_relaxed))
        {
            stop();
            throw AudioDecodeCancelled{};
        }
        if (elapsed.elapsed() > 120000 || output.size() > outputLimit)
        {
            stop();
            throw std::runtime_error("Audio decoder exceeded its time or output limit.");
        }
    } while (process.state() != QProcess::NotRunning);
    if (process.error() == QProcess::FailedToStart || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
        throw std::runtime_error(
            QString("%1: %2")
                .arg(name, diagnostic.isEmpty() ? process.errorString() : QString::fromUtf8(diagnostic))
                .toStdString());
    return output;
}
} // namespace

LinuxDecodedAudio decodeLinuxAudio(const QString &path, double start, double end,
                                   const std::atomic_bool *cancellation)
{
    if (!QFileInfo(path).isFile() || !std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start ||
        end - start > 120.0)
        throw std::runtime_error(trText("messages.audio_transcription.invalid_selection").toStdString());
    const QString input = QFileInfo(path).absoluteFilePath();
    const auto metadata = runDecoder(
        "ffprobe",
        {"-v", "error", "-show_entries", "format=duration", "-of", "default=noprint_wrappers=1:nokey=1", input},
        65536, cancellation);
    bool numeric = false;
    const double duration = QString::fromUtf8(metadata).trimmed().toDouble(&numeric);
    if (!numeric || !std::isfinite(duration) || duration <= 0 || duration > 1200)
        throw std::runtime_error(trText("messages.audio_transcription.duration_limit").toStdString());
    if (start >= duration)
        throw std::runtime_error(trText("messages.audio_transcription.invalid_selection").toStdString());
    LinuxDecodedAudio audio;
    audio.duration = duration;
    audio.start = start;
    audio.end = std::min(end, duration);
    const auto bytes = runDecoder("ffmpeg",
                                  {"-nostdin", "-v", "error", "-ss", QString::number(start, 'f', 6), "-i", input,
                                   "-t", QString::number(audio.end - start, 'f', 6), "-map", "0:a:0", "-ac", "1",
                                   "-ar", "16000", "-f", "s16le", "pipe:1"},
                                  120 * 16000 * 2 + 65536, cancellation);
    if (bytes.isEmpty() || bytes.size() % 2 != 0)
        throw std::runtime_error(trText("messages.audio_transcription.decode_failed").toStdString());
    audio.samples.assign(static_cast<std::size_t>(std::llround((audio.end - start) * 16000)), 0.0F);
    const auto count = std::min(audio.samples.size(), static_cast<std::size_t>(bytes.size() / 2));
    for (std::size_t frame = 0; frame < count; ++frame)
        audio.samples[frame] = qFromLittleEndian<qint16>(bytes.constData() + frame * 2) / 32768.0F;
    return audio;
}
} // namespace singlilt

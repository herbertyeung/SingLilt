// Subprocess fault injection; real codec accuracy is covered by LinuxMediaTests.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "recognition/linux/AudioDecoder.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtEndian>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
int childFixture(const QString &tool, const QStringList &arguments)
{
    const QString mode = qEnvironmentVariable("SINGLILT_DECODER_FIXTURE");
    if (mode == "slow")
    {
        QFile ready(qEnvironmentVariable("SINGLILT_DECODER_READY"));
        if (!ready.open(QIODevice::WriteOnly) || ready.write("ready") != 5)
            return 2;
        ready.close();
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    if (mode == "nonzero")
        return 9;
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly))
        return 2;
    if (tool == "ffprobe")
    {
        const QByteArray duration = mode == "duration" ? QByteArray("nan\n") : QByteArray("1.0\n");
        return output.write(duration) == duration.size() ? 0 : 2;
    }
    const int position = arguments.indexOf("-t");
    if (position < 0 || position + 1 >= arguments.size())
        return 2;
    const int frames = static_cast<int>(arguments[position + 1].toDouble() * 16000);
    QByteArray samples(mode == "oversized" ? 4000000 : frames * 2, '\0');
    for (qsizetype frame = 0; frame < samples.size() / 2; ++frame)
        qToLittleEndian<qint16>(16384, samples.data() + frame * 2);
    return output.write(samples) == samples.size() ? 0 : 2;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString tool = QFileInfo(app.applicationFilePath()).completeBaseName();
    if (tool == "ffprobe" || tool == "ffmpeg")
        return childFixture(tool, app.arguments());
    using namespace singlilt;
    int checks = 0;
    const auto check = [&](bool passed, const char *name)
    {
        if (!passed)
            throw std::runtime_error(name);
        ++checks;
    };
    const auto expectFailure = [&](const char *mode) { qputenv("SINGLILT_DECODER_FIXTURE", mode); };
    try
    {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary fixture directory");
        const auto oldPath = qgetenv("PATH");
        const auto oldMode = qgetenv("SINGLILT_DECODER_FIXTURE");
        const auto oldReady = qgetenv("SINGLILT_DECODER_READY");
        const auto restore = qScopeGuard(
            [&]
            {
                qputenv("PATH", oldPath);
                qputenv("SINGLILT_DECODER_FIXTURE", oldMode);
                qputenv("SINGLILT_DECODER_READY", oldReady);
            });
        for (const auto *name : {"ffprobe", "ffmpeg"})
        {
#ifdef Q_OS_WIN
            const QString destination = temporary.filePath(QString::fromLatin1(name) + ".exe");
#else
            const QString destination = temporary.filePath(QString::fromLatin1(name));
#endif
            check(QFile::copy(app.applicationFilePath(), destination), "copy executable process fixture");
            check(QFile::setPermissions(destination, QFileInfo(app.applicationFilePath()).permissions()),
                  "fixture executable permissions");
        }
        qputenv("PATH", temporary.path().toUtf8() + QDir::listSeparator().toLatin1() + oldPath);
        QFile input(temporary.filePath(QString::fromUtf8("音频 fixture.wav")));
        check(input.open(QIODevice::WriteOnly) && input.write("fixture") == 7, "input fixture");
        input.close();
        qputenv("SINGLILT_DECODER_FIXTURE", "");
        const auto audio = decodeLinuxAudio(input.fileName(), 0.25, 0.75);
        check(audio.duration == 1 && audio.samples.size() == 8000 && audio.samples.front() == 0.5F,
              "process output parsed and selected interval retained");
        for (const auto *mode : {"nonzero", "duration", "oversized"})
        {
            expectFailure(mode);
            bool failed = false;
            try
            {
                decodeLinuxAudio(input.fileName(), 0, 1);
            }
            catch (const std::runtime_error &error)
            {
                failed = *error.what() != '\0';
            }
            check(failed, "process fault rejected with a diagnostic");
        }
        const QString ready = temporary.filePath("ready");
        qputenv("SINGLILT_DECODER_READY", ready.toUtf8());
        expectFailure("slow");
        std::atomic_bool cancellation{false};
        std::jthread canceller(
            [&](std::stop_token stop)
            {
                for (int iteration = 0; iteration < 500 && !stop.stop_requested(); ++iteration)
                {
                    if (QFileInfo::exists(ready))
                    {
                        cancellation = true;
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                cancellation = true;
            });
        bool cancelled = false;
        const auto started = std::chrono::steady_clock::now();
        try
        {
            decodeLinuxAudio(input.fileName(), 0, 1, &cancellation);
        }
        catch (const AudioDecodeCancelled &)
        {
            cancelled = true;
        }
        canceller.request_stop();
        canceller.join();
        check(cancelled && QFileInfo::exists(ready) &&
                  std::chrono::steady_clock::now() - started < std::chrono::seconds(5),
              "running subprocess cancellation finishes promptly");
        qputenv("PATH", "");
        bool missing = false;
        try
        {
            decodeLinuxAudio(input.fileName(), 0, 1);
        }
        catch (const std::runtime_error &error)
        {
            missing = *error.what() != '\0';
        }
        check(missing, "missing tool diagnostic");
        std::cout << "Audio decoder process tests: " << checks << "/" << checks << " passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

// Linux media regression tests without physical playback or capture hardware.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "audio/MicrophoneCapture.h"
#include "audio/MidiInstrument.h"
#include "audio/OriginalAudioPlayer.h"
#include "audio/SoundFontInstrument.h"
#include "recognition/WindowsOcr.h"
#include "recognition/linux/AudioDecoder.h"
#include <QApplication>
#include <QDataStream>
#include <QFile>
#include <QPainter>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <thread>

namespace
{
void writeWave(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot create wave fixture");
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(32000 + 36);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(16000) << quint32(32000) << quint16(2)
           << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(32000);
    for (int frame = 0; frame < 16000; ++frame)
        stream << qint16(16000 * std::sin(2 * std::numbers::pi * 440 * frame / 16000));
    if (stream.status() != QDataStream::Ok)
        throw std::runtime_error("Wave fixture write failed");
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    using namespace singlilt;
    int checks = 0;
    const auto check = [&](bool passed, const char *name)
    {
        if (!passed)
            throw std::runtime_error(name);
        ++checks;
    };
    const auto expectError = [&](auto operation, const char *name)
    {
        bool rejected = false;
        try
        {
            operation();
        }
        catch (const std::runtime_error &error)
        {
            rejected = *error.what() != '\0';
        }
        check(rejected, name);
    };
    try
    {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary directory");
        const QString wave = temporary.filePath(QString::fromUtf8("音频 fixture.wav"));
        writeWave(wave);
        auto audio = decodeLinuxAudio(wave, 0.25, 0.75);
        check(std::abs(audio.duration - 1) < 0.001 && audio.samples.size() == 8000,
              "Unicode source and exact 16 kHz selection");
        check(std::any_of(audio.samples.begin(), audio.samples.end(), [](float sample) { return sample > 0.4F; }),
              "decoded samples retain waveform energy");
        audio = decodeLinuxAudio(wave, 0, 2);
        check(audio.end == 1 && audio.samples.size() == 16000, "selection clamps to source duration");
        expectError([&] { decodeLinuxAudio(wave, 1, 2); }, "selection after end rejected");
        expectError([&] { decodeLinuxAudio(wave, 0, 121); }, "oversized selection rejected");
        expectError([&] { decodeLinuxAudio(wave, std::numeric_limits<double>::quiet_NaN(), 1); }, "NaN rejected");
        QFile malformed(temporary.filePath("bad.wav"));
        check(malformed.open(QIODevice::WriteOnly) && malformed.write("not audio") == 9, "malformed fixture");
        malformed.close();
        expectError([&] { decodeLinuxAudio(malformed.fileName(), 0, 1); }, "malformed media rejected");
        std::atomic_bool cancellation{true};
        bool cancelled = false;
        try
        {
            decodeLinuxAudio(wave, 0, 1, &cancellation);
        }
        catch (const AudioDecodeCancelled &)
        {
            cancelled = true;
        }
        check(cancelled, "pre-cancelled decoding");
        {
            const QByteArray path = qgetenv("PATH");
            const auto restore = qScopeGuard([&] { qputenv("PATH", path); });
            qputenv("PATH", "");
            expectError([&] { decodeLinuxAudio(wave, 0, 1); }, "missing decoder diagnostic");
        }
        OriginalAudioPlayer player;
        check(!player.play() && !player.errorString().isEmpty(), "closed player diagnostic");
        check(player.open(wave) && std::abs(player.durationSeconds() - 1) < 0.01, "original file opens");
        check(player.seek(0.5) && player.setSpeed(0.75) && player.speed() == 0.75, "seek and speed");
        check(!player.setVolume(-1) && player.setVolume(0.5) && !player.seek(2), "player range validation");
        check(!player.open(temporary.filePath("missing.wav")) && player.isOpen(),
              "failed open retains old source");
        check(player.stop() && player.positionSeconds() == 0, "stop resets original clock");
        player.close();
        player.close();
        check(!player.isOpen() && player.sourcePath().isEmpty(), "idempotent player close");
        MicrophoneCapture microphone;
        microphone.start("singlilt-intentionally-missing-device");
        for (int iteration = 0; iteration < 100 && microphone.state() == MicrophoneCapture::State::Starting;
             ++iteration)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(microphone.state() == MicrophoneCapture::State::Failed && !microphone.errorString().isEmpty(),
              "missing capture endpoint fails without hanging");
        microphone.stop();
        check(microphone.state() == MicrophoneCapture::State::Stopped, "capture shutdown");
        MidiInstrument midi;
        check(!midi.noteOn(16, 60, 100) && !midi.noteOn(0, 128, 100) && midi.allNotesOff(), "MIDI validation");
        check(!midi.setChannelVolume(0, std::numeric_limits<double>::quiet_NaN()), "invalid MIDI volume");
        SoundFontInstrument sampler;
        check(sampler.open(false) && sampler.noteOn(0, 60, 100), "offline system SoundFont");
        std::vector<float> rendered(9600);
        check(sampler.render(rendered.data(), 4800) &&
                  std::any_of(rendered.begin(), rendered.end(),
                              [](float sample) { return std::abs(sample) > 0.001F; }),
              "offline SoundFont renders non-silent PCM");
        sampler.close();
        check(!recognizeWindowsText({}).error.isEmpty(), "empty OCR image rejected");
        QImage image(700, 150, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::black);
        painter.setFont(QFont("DejaVu Sans", 42));
        painter.drawText(image.rect(), Qt::AlignCenter, "123 TEST");
        painter.end();
        const auto ocr = recognizeWindowsText(image, "en-US");
        check(ocr.error.isEmpty() && ocr.text.contains("TEST") && !ocr.words.empty(), "Tesseract text extraction");
        check(std::all_of(ocr.words.begin(), ocr.words.end(), [&](const OcrWord &word)
                          { return image.rect().contains(word.box.toAlignedRect()) && word.line >= 0; }),
              "OCR boxes map to original image");
        std::cout << "Linux media tests: " << checks << "/" << checks << " passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Linux media failure after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}

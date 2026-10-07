#include "audio/OriginalAudioPlayer.h"
#include "audio/QtAudioDecode.h"
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool ok)
{
    if (!ok)
        throw std::runtime_error("audio assertion");
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    QByteArray pcm;
    for (int i = 0; i < 48000; i++)
    {
        qint16 n = qToLittleEndian<qint16>(qint16(10000 * std::sin(2 * 3.141592653589793 * 440 * i / 48000)));
        pcm.append(reinterpret_cast<const char *>(&n), 2);
        pcm.append(reinterpret_cast<const char *>(&n), 2);
    }
    QByteArray wav("RIFF");
    auto u32 = [&](quint32 n)
    {
        n = qToLittleEndian(n);
        wav.append(reinterpret_cast<char *>(&n), 4);
    };
    auto u16 = [&](quint16 n)
    {
        n = qToLittleEndian(n);
        wav.append(reinterpret_cast<char *>(&n), 2);
    };
    u32(36 + pcm.size());
    wav.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(2);
    u32(48000);
    u32(192000);
    u16(4);
    u16(16);
    wav.append("data");
    u32(pcm.size());
    wav.append(pcm);
    QString path = tmp.filePath("test audio.wav");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly));
    check(file.write(wav) == wav.size());
    file.close();
    double duration = 0;
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    auto samples = singlilt::decodeMonoAudio(path, 16000, 0.25, 0.75, duration, cancellation);
    check(std::abs(duration - 1) < 0.02);
    check(std::abs(int(samples.size()) - 8000) < 10);
    check(*std::max_element(samples.begin(), samples.end()) > 0.2);
    cancellation->store(true);
    bool failed = false;
    try
    {
        singlilt::decodeMonoAudio(path, 16000, 0, 1, duration, cancellation);
    }
    catch (...)
    {
        failed = true;
    }
    check(failed);
    cancellation->store(false);
    failed = false;
    try
    {
        singlilt::decodeMonoAudio(tmp.filePath("missing.wav"), 16000, 0, 1, duration, cancellation);
    }
    catch (...)
    {
        failed = true;
    }
    check(failed);
    singlilt::OriginalAudioPlayer player;
    check(!player.open(tmp.filePath("missing.wav")));
    check(!player.setSpeed(NAN));
    check(player.open(path));
    check(std::abs(player.durationSeconds() - 1) < 0.02);
    check(player.setSpeed(0.5));
    check(player.seek(0.25));
    check(!player.seek(2));
    player.close();
    check(!player.isOpen());
    std::cout << "PASS decode stereo, interval, duration, cancellation, missing input and player state\n";
}

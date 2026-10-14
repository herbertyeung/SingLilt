// SPDX-License-Identifier: MIT
#include "QtAudioDecode.h"
#include "i18n/LanguageManager.h"
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace singlilt
{
std::vector<float> decodeMonoAudio(const QString &path, int sampleRate, double start, double end, double &duration,
                                   const std::shared_ptr<std::atomic_bool> &cancel)
{
    auto fail = [](const char *key) { throw std::runtime_error(trText(key).toStdString()); };
    if (cancel && cancel->load())
        fail("messages.audio_transcription.cancelled");
    if (!QFileInfo(path).isFile())
        fail("messages.audio_transcription.decode_failed");
    if (sampleRate <= 0 || !std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start || end > 1200)
        fail("messages.audio_transcription.invalid_selection");
    QAudioDecoder decoder;
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    decoder.setAudioFormat(format);
    decoder.setSource(QUrl::fromLocalFile(path));
    QEventLoop loop;
    QTimer deadline, poll;
    QString error;
    bool finished = false;
    std::vector<float> samples;
    qint64 frameEnd = 0;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop,
                     [&]
                     {
                         auto buffer = decoder.read();
                         if (!buffer.isValid())
                             return;
                         auto actual = buffer.format();
                         if (actual.sampleRate() != sampleRate || actual.channelCount() != 1 ||
                             actual.sampleFormat() != QAudioFormat::Float)
                         {
                             error = trText("messages.audio_transcription.unsupported_pcm");
                             loop.quit();
                             return;
                         }
                         auto first = buffer.startTime() < 0
                                          ? frameEnd
                                          : qint64(std::llround(buffer.startTime() * sampleRate / 1e6));
                         frameEnd = std::max(frameEnd, first + buffer.frameCount());
                         if (frameEnd > qint64(sampleRate) * 1200)
                         {
                             error = trText("messages.audio_transcription.duration_limit");
                             loop.quit();
                             return;
                         }
                         const auto from = std::max<qint64>(0, qint64(std::llround(start * sampleRate)) - first);
                         const auto to =
                             std::min<qint64>(buffer.frameCount(), qint64(std::llround(end * sampleRate)) - first);
                         if (to > from)
                         {
                             auto data = buffer.constData<float>();
                             samples.insert(samples.end(), data + from, data + to);
                         }
                     });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop,
                     [&]
                     {
                         finished = true;
                         loop.quit();
                     });
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop,
                     [&](auto)
                     {
                         error = decoder.errorString();
                         loop.quit();
                     });
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop,
                     [&]
                     {
                         error = trText("messages.audio_transcription.decode_failed");
                         loop.quit();
                     });
    QObject::connect(&poll, &QTimer::timeout, &loop,
                     [&]
                     {
                         if (cancel && cancel->load())
                         {
                             error = trText("messages.audio_transcription.cancelled");
                             loop.quit();
                         }
                     });
    deadline.start(120000);
    poll.start(50);
    decoder.start();
    loop.exec();
    decoder.stop();
    if (!error.isEmpty())
        throw std::runtime_error(error.toStdString());
    duration = double(frameEnd) / sampleRate;
    if (!finished || samples.empty() || duration <= start)
        fail("messages.audio_transcription.decode_failed");
    return samples;
}
} // namespace singlilt

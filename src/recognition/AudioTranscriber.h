// Audio decoding, melody analysis, and local lyric transcription.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/AudioSource.h"
#include "domain/Score.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace singlilt
{

struct AudioTranscriptionOptions
{
    double startSeconds = 0.0;
    double endSeconds = 60.0;
    double bpm = 0.0; // Zero requests an onset-based tempo suggestion.
    int tonic = -1;   // -1 requests a key suggestion; otherwise 0=C..11=B.
    QString lyricsText;
    QString whisperExecutable;
    QString whisperModel;
    QString language = QStringLiteral("auto");
    bool recognizeLyrics = false;
    bool enhanceVoice = false;  // A simple vocal-band filter, not stem separation.
    bool wholeSong = false;     // Uses bounded 120-second chunks within the 20-minute source limit.
    bool separateVocals = true; // Disable only for already isolated vocals or clean test signals.
    QString separatorPython;
    QString separatorScript;
    QString separatorModelDirectory;
    QString separatorCacheDirectory;
};

struct AudioTranscriptionResult
{
    Score score;
    std::vector<AudioNoteTiming> timings;
    std::vector<AudioLyricTiming> lyricTimings;
    QStringList warnings;
    QString sourcePath;
    double sourceDurationSeconds = 0.0;
    double selectedStartSeconds = 0.0;
    double selectedEndSeconds = 0.0;
    double estimatedBpm = 0.0;
    int estimatedTonic = 0;
    QString recognizedLyrics;
    qint64 elapsedMilliseconds = 0;
    bool cancelled = false;
    bool vocalsSeparated = false;
    QString vocalsPath;
    QString instrumentalPath;
    QString separationModel;
    QString separationManifest;
    double separationSeconds = 0.0;
};

using AudioTranscriptionProgress = std::function<void(int, const QString &)>;

// The caller runs this on a worker. Each decoding chunk is limited to 120 s;
// wholeSong covers sources up to 20 minutes. Errors use existing exception handling.
AudioTranscriptionResult transcribeAudio(const QString &path, const AudioTranscriptionOptions &options,
                                         const std::shared_ptr<std::atomic_bool> &cancellation = {},
                                         const AudioTranscriptionProgress &progress = {});

QJsonObject audioProcessingMetadata(const AudioTranscriptionResult &result,
                                    const AudioTranscriptionOptions &options);

} // namespace singlilt

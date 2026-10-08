// Original audio playback, seeking, and transport state.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <functional>
#include <memory>

class QObject;

namespace singlilt
{

// A UI-thread-owned native file player. Its original-audio clock is independent
// of the synthesized score transport; the UI explicitly selects one source.
class OriginalAudioPlayer final
{
  public:
    OriginalAudioPlayer();
    ~OriginalAudioPlayer();
    OriginalAudioPlayer(const OriginalAudioPlayer &) = delete;
    OriginalAudioPlayer &operator=(const OriginalAudioPlayer &) = delete;

    // Synchronous compatibility path for diagnostics; UI uses openAsync.
    bool open(const QString &path);
    // UI callers use the completion path; replacement/cancellation suppresses stale callbacks.
    void openAsync(const QString &path, QObject *context, std::function<void(bool)> completion);
    bool isLoading() const;
    void cancelOpen();
    void close();
    bool isOpen() const;
    bool play();
    bool pause();
    bool stop();
    bool seek(double seconds);
    bool setSpeed(double factor);
    bool setVolume(double volume);
    bool isPlaying() const;
    double positionSeconds() const;
    double durationSeconds() const;
    double speed() const;
    QString sourcePath() const;
    QString errorString() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace singlilt

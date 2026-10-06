// Vision-API recognition requests and response validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include "LocalRecognizer.h"
#include <QString>
#include <atomic>
namespace singlilt
{
enum class RecognitionNotation
{
    Numbered,
    Staff
};

struct VisionConfig
{
    // Accept a server root, /v1 base URL, or a complete Chat Completions endpoint.
    QString endpoint = "https://api.openai.com/v1/chat/completions";
    QString model = "gpt-4.1";
    QString apiKey; // Memory only. Never written to a score or settings file.
    static constexpr int MinTimeoutSeconds = 30;
    static constexpr int MaxTimeoutSeconds = 1800;
    int timeoutSeconds = 600;
};
// The caller keeps cancellation alive until this synchronous worker call returns.
RecognitionResult recognizeCloud(const QImage &image, const QString &path, const VisionConfig &config,
                                 const std::atomic_bool *cancellation = nullptr,
                                 RecognitionNotation notation = RecognitionNotation::Numbered);
QString recognitionPrompt(int width, int height, RecognitionNotation notation = RecognitionNotation::Numbered);
} // namespace singlilt

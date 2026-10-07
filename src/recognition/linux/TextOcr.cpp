// Tesseract text and original-image word boxes through the existing OCR interface.
// Copyright (c) 2026 Herbert Yeung
// SPDX-License-Identifier: MIT

#include "i18n/LanguageManager.h"
#include "recognition/WindowsOcr.h"
#include <algorithm>
#include <memory>
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

namespace singlilt
{
OcrText recognizeWindowsText(const QImage &original, const QString &language)
{
    if (original.isNull())
        return {{}, {}, {}, trText("messages.recognition.empty_image")};
    if (original.width() > 12000 || original.height() > 20000 ||
        qint64(original.width()) * original.height() > 50000000)
        return {{}, {}, {}, trText("messages.recognition.invalid_image")};
    tesseract::TessBaseAPI engine;
    const QByteArray requested = language.startsWith("zh") ? QByteArray("chi_sim+eng") : QByteArray("eng");
    OcrText output;
    // Initialization failure is explicit; do not disguise missing language data as a successful OCR result.
    if (engine.Init(nullptr, requested.constData()) != 0)
    {
        output.error = trText("messages.recognition.ocr_languages");
        return output;
    }
    const QImage image = original.convertToFormat(QImage::Format_RGB888);
    engine.SetImage(image.constBits(), image.width(), image.height(), 3, static_cast<int>(image.bytesPerLine()));
    engine.SetSourceResolution(300);
    engine.SetPageSegMode(tesseract::PSM_AUTO);
    if (engine.Recognize(nullptr) != 0)
    {
        output.error = QStringLiteral("Tesseract OCR failed.");
        return output;
    }
    std::unique_ptr<char[]> text(engine.GetUTF8Text());
    output.text = text ? QString::fromUtf8(text.get()) : QString{};
    output.language = QString::fromLatin1(requested);
    std::unique_ptr<tesseract::ResultIterator> words(engine.GetIterator());
    if (!words)
        return output;
    int line = -1;
    do
    {
        if (words->IsAtBeginningOf(tesseract::RIL_TEXTLINE))
            ++line;
        std::unique_ptr<char[]> word(words->GetUTF8Text(tesseract::RIL_WORD));
        int left = 0, top = 0, right = 0, bottom = 0;
        if (word && words->BoundingBox(tesseract::RIL_WORD, &left, &top, &right, &bottom))
            output.words.push_back(
                {QString::fromUtf8(word.get()), QRectF(left, top, right - left, bottom - top), std::max(0, line)});
    } while (words->Next(tesseract::RIL_WORD));
    return output;
}
} // namespace singlilt

// Windows OCR text and bounding-box extraction.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "WindowsOcr.h"
#include "i18n/LanguageManager.h"

#include <algorithm>
#include <future>
#ifdef Q_OS_WIN
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace singlilt
{
OcrText recognizeWindowsText(const QImage &original, const QString &language)
{
    if (original.isNull())
        return {{}, {}, {}, trText("messages.recognition.empty_image")};
#ifdef Q_OS_WIN
    return std::async(
               std::launch::async,
               [original, language]
               {
                   OcrText output;
                   bool initialized = false;
                   try
                   {
                       winrt::init_apartment(winrt::apartment_type::multi_threaded);
                       initialized = true;
                       using namespace winrt::Windows;
                       auto engine = Media::Ocr::OcrEngine::TryCreateFromLanguage(
                           Globalization::Language(language.toStdWString()));
                       if (!engine)
                           engine = Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
                       if (!engine)
                           engine =
                               Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(L"en-US"));
                       if (!engine)
                       {
                           output.error =
                               trText("messages.recognition.ocr_languages");
                       }
                       else
                       {
                           QImage image = original.convertToFormat(QImage::Format_ARGB32);
                           const int maximum = static_cast<int>(Media::Ocr::OcrEngine::MaxImageDimension());
                           if (image.width() > maximum || image.height() > maximum)
                               image =
                                   image.scaled(maximum, maximum, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                           const double scale = double(original.width()) / image.width();
                           Storage::Streams::DataWriter writer;
                           writer.WriteBytes(winrt::array_view<const uint8_t>(
                               image.constBits(), image.constBits() + image.sizeInBytes()));
                           auto bitmap = Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
                               writer.DetachBuffer(), Graphics::Imaging::BitmapPixelFormat::Bgra8, image.width(),
                               image.height(), Graphics::Imaging::BitmapAlphaMode::Ignore);
                           auto recognized = engine.RecognizeAsync(bitmap).get();
                           output.text = QString::fromWCharArray(recognized.Text().c_str());
                           output.language =
                               QString::fromWCharArray(engine.RecognizerLanguage().LanguageTag().c_str());
                           int line = 0;
                           for (const auto &row : recognized.Lines())
                           {
                               for (const auto &word : row.Words())
                               {
                                   const auto rect = word.BoundingRect();
                                   output.words.push_back({QString::fromWCharArray(word.Text().c_str()),
                                                           QRectF(rect.X * scale, rect.Y * scale,
                                                                  rect.Width * scale, rect.Height * scale),
                                                           line});
                               }
                               ++line;
                           }
                       }
                   }
                   catch (const winrt::hresult_error &error)
                   {
                       output.error = trText("messages.recognition.windows_ocr_error")
                                          .arg(QString::fromWCharArray(error.message().c_str()))
                                          .arg(static_cast<uint32_t>(error.code().value), 8, 16, QLatin1Char('0'));
                   }
                   catch (const std::exception &error)
                   {
                       output.error = QString::fromUtf8(error.what());
                   }
                   if (initialized)
                       winrt::uninit_apartment();
                   return output;
               })
        .get();
#else
    return {{}, {}, {}, trText("messages.recognition.windows_ocr_required")};
#endif
}
} // namespace singlilt

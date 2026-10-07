// SPDX-License-Identifier: MIT
#include "PlatformOcr.h"
#include "i18n/LanguageManager.h"
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#include <algorithm>
namespace singlilt
{
OcrText recognizeMacText(const QImage &original, const QString &language)
{
    if (original.isNull())
        return {{}, {}, {}, trText("messages.recognition.empty_image")};
    @autoreleasepool
    {
        const QImage image = original.convertToFormat(QImage::Format_RGBA8888);
        CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
        CGDataProviderRef provider =
            CGDataProviderCreateWithData(nullptr, image.constBits(), image.sizeInBytes(), nullptr);
        CGImageRef bitmap = CGImageCreate(image.width(), image.height(), 8, 32, image.bytesPerLine(), color,
                                          kCGImageAlphaLast, provider, nullptr, false, kCGRenderingIntentDefault);
        CGColorSpaceRelease(color);
        CGDataProviderRelease(provider);
        if (!bitmap)
            return {{}, {}, {}, trText("messages.recognition.empty_image")};
        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection = NO;
        NSError *error = nil;
        NSArray<NSString *> *supported = [request supportedRecognitionLanguagesAndReturnError:&error];
        NSString *preferred = [NSString stringWithUTF8String:language.toUtf8().constData()];
        if ([preferred hasPrefix:@"zh"])
            preferred = @"zh-Hans";
        else if ([preferred hasPrefix:@"en"])
            preferred = @"en-US";
        if ([supported containsObject:preferred])
            request.recognitionLanguages = @[ preferred ];
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:bitmap options:@{}];
        BOOL ok = [handler performRequests:@[ request ] error:&error];
        CGImageRelease(bitmap);
        OcrText output;
        output.language = QString::fromUtf8(preferred.UTF8String);
        if (!ok)
        {
            output.error = QString::fromUtf8(error.localizedDescription.UTF8String);
            [handler release];
            [request release];
            return output;
        }
        auto observations = [request.results sortedArrayUsingComparator:^NSComparisonResult(
                                                 VNRecognizedTextObservation *a, VNRecognizedTextObservation *b) {
          const double delta = CGRectGetMidY(a.boundingBox) - CGRectGetMidY(b.boundingBox);
          if (std::abs(delta) > 0.005)
              return delta > 0 ? NSOrderedAscending : NSOrderedDescending;
          return a.boundingBox.origin.x < b.boundingBox.origin.x ? NSOrderedAscending : NSOrderedDescending;
        }];
        NSRegularExpression *words = [NSRegularExpression regularExpressionWithPattern:@"\\S+"
                                                                               options:0
                                                                                 error:nil];
        int line = 0;
        for (VNRecognizedTextObservation *observation in observations)
        {
            VNRecognizedText *candidate = [[observation topCandidates:1] firstObject];
            if (!candidate)
                continue;
            if (!output.text.isEmpty())
                output.text += '\n';
            output.text += QString::fromUtf8(candidate.string.UTF8String);
            for (NSTextCheckingResult *match in [words matchesInString:candidate.string
                                                               options:0
                                                                 range:NSMakeRange(0, candidate.string.length)])
            {
                VNRectangleObservation *word = [candidate boundingBoxForRange:match.range error:nil];
                CGRect box = word ? word.boundingBox : observation.boundingBox;
                output.words.append(
                    {QString::fromUtf8([candidate.string substringWithRange:match.range].UTF8String),
                     QRectF(box.origin.x * original.width(),
                            (1 - box.origin.y - box.size.height) * original.height(),
                            box.size.width * original.width(), box.size.height * original.height()),
                     line});
            }
            ++line;
        }
        [handler release];
        [request release];
        return output;
    }
}
} // namespace singlilt

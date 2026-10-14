// SPDX-License-Identifier: MIT
#pragma once
#include "audio/InstrumentOutput.h"
namespace singlilt
{
constexpr AudioBackend diagnosticAudioBackend()
{
#ifdef Q_OS_WIN
    return AudioBackend::WindowsMidi;
#else
    return AudioBackend::SampledPiano;
#endif
}
} // namespace singlilt

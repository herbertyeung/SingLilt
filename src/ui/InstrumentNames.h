// General MIDI instrument labels and translation keys.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once
#include <array>

namespace singlilt
{
struct InstrumentName
{
    int program;
    const char *key;
};

inline constexpr std::array<InstrumentName, 39> CommonInstruments{
    {{0, "ui.instrument.piano"},          {1, "ui.instrument.bright_piano"},
     {4, "ui.instrument.electric_piano"}, {5, "ui.instrument.electric_piano_2"},
     {6, "ui.instrument.harpsichord"},    {8, "ui.instrument.celesta"},
     {11, "ui.instrument.vibraphone"},    {12, "ui.instrument.marimba"},
     {13, "ui.instrument.xylophone"},     {16, "ui.instrument.drawbar_organ"},
     {19, "ui.instrument.church_organ"},  {21, "ui.instrument.accordion"},
     {24, "ui.instrument.guitar"},        {25, "ui.instrument.steel_guitar"},
     {27, "ui.instrument.clean_guitar"},  {30, "ui.instrument.distortion_guitar"},
     {32, "ui.instrument.acoustic_bass"}, {33, "ui.instrument.finger_bass"},
     {35, "ui.instrument.fretless_bass"}, {40, "ui.instrument.violin"},
     {41, "ui.instrument.viola"},         {42, "ui.instrument.cello"},
     {43, "ui.instrument.contrabass"},    {45, "ui.instrument.pizzicato"},
     {46, "ui.instrument.harp"},          {48, "ui.instrument.strings"},
     {52, "ui.instrument.choir"},         {56, "ui.instrument.trumpet"},
     {57, "ui.instrument.trombone"},      {60, "ui.instrument.french_horn"},
     {65, "ui.instrument.alto_sax"},      {66, "ui.instrument.tenor_sax"},
     {68, "ui.instrument.oboe"},          {70, "ui.instrument.bassoon"},
     {71, "ui.instrument.clarinet"},      {72, "ui.instrument.piccolo"},
     {73, "ui.instrument.flute"},         {74, "ui.instrument.recorder"},
     {75, "ui.instrument.pan_flute"}}};

inline const char *instrumentNameKey(int program)
{
    for (const auto &instrument : CommonInstruments)
        if (instrument.program == program)
            return instrument.key;
    return nullptr;
}
} // namespace singlilt

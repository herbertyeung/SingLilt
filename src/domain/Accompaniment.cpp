// Chord arrangements, candidate generation, and validation.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "Accompaniment.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numeric>
#include <sstream>
#include <utility>

namespace singlilt
{
namespace
{

struct SourceNote
{
    std::int64_t startTick;
    std::int64_t endTick;
    int pitchClass;
    int key;
};

struct HarmonicInterval
{
    std::int64_t startTick;
    std::int64_t endTick;
    int key;
};

bool hasErrors(const std::vector<Diagnostic> &diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic)
                       { return diagnostic.severity == DiagnosticSeverity::Error; });
}

void diagnose(std::vector<Diagnostic> &diagnostics, DiagnosticSeverity severity, const char *message)
{
    diagnostics.push_back({severity, message});
}

bool supportedMeter(const Score &score)
{
    return (score.beatUnit == 4 && (score.beatsPerBar == 2 || score.beatsPerBar == 3 || score.beatsPerBar == 4)) ||
           (score.beatUnit == 8 && score.beatsPerBar == 6);
}

bool validPattern(AccompanimentPattern pattern)
{
    return pattern == AccompanimentPattern::BlockChords || pattern == AccompanimentPattern::Arpeggio ||
           pattern == AccompanimentPattern::Sparse;
}

void validateSettings(const AccompanimentSettings &settings, std::vector<Diagnostic> &diagnostics)
{
    if (!validPattern(settings.pattern) ||
        (settings.mode != HarmonicMode::Automatic && settings.mode != HarmonicMode::Major &&
         settings.mode != HarmonicMode::Minor) ||
        settings.harmonicTonic < -1 || settings.harmonicTonic > 11 || settings.chordProgram < 0 ||
        settings.chordProgram > 127 || settings.bassProgram < 0 || settings.bassProgram > 127 ||
        settings.chordVelocity < 1 || settings.chordVelocity > 127 || settings.bassVelocity < 1 ||
        settings.bassVelocity > 127)
        diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.settings_range");
}

std::vector<SourceNote> sourceNotes(const Score &score)
{
    std::vector<SourceNote> notes;
    notes.reserve(score.notes.size());
    std::int64_t tick = 0;
    int key = score.tonic;
    for (const auto &note : score.notes)
    {
        if (note.keyOverride >= 0)
            key = note.keyOverride;
        const int pitch = midiPitch(note, key);
        notes.push_back({tick, tick + note.durationTicks, pitch < 0 ? -1 : pitch % 12, key});
        tick += note.durationTicks;
    }
    return notes;
}

std::vector<SourceMeasureRange> measureRanges(const Score &score, std::vector<Diagnostic> &diagnostics)
{
    const int barTicks = ticksPerBar(score);
    if (barTicks <= 0 || score.notes.empty() || score.notes.size() > 100000)
        return {};
    std::vector<std::int64_t> lengths;
    std::vector<int> labels;
    for (const auto &note : score.notes)
    {
        if (note.durationTicks <= 0 || note.durationTicks > TicksPerQuarter * 256)
            return {};
        if (labels.empty() || note.measure != labels.back())
        {
            labels.push_back(note.measure);
            lengths.push_back(0);
        }
        lengths.back() += note.durationTicks;
    }
    const auto totalTicks = std::accumulate(lengths.begin(), lengths.end(), std::int64_t{0});
    if (totalTicks > 100000LL * barTicks)
        return {};
    std::vector<SourceMeasureRange> ranges;
    bool aligned = labels.size() > 1 && labels.front() >= 0;
    for (std::size_t i = 0; i < lengths.size() && aligned; ++i)
    {
        const bool edge = i == 0 || i + 1 == lengths.size();
        if (lengths[i] > barTicks || (!edge && lengths[i] != barTicks) ||
            (i > 0 && static_cast<long long>(labels[i]) != static_cast<long long>(labels[i - 1]) + 1))
            aligned = false;
    }
    if (aligned)
    {
        std::int64_t tick = 0;
        for (std::size_t i = 0; i < lengths.size(); ++i)
        {
            ranges.push_back({tick, tick + lengths[i], labels[i], i == 0 && lengths[i] < barTicks});
            tick += lengths[i];
        }
        if (ranges.front().pickup)
            diagnose(diagnostics, DiagnosticSeverity::Warning, "messages.accompaniment.pickup");
    }
    else
    {
        for (std::int64_t tick = 0; tick < totalTicks; tick += barTicks)
            ranges.push_back(
                {tick, std::min(tick + barTicks, totalTicks), static_cast<int>(ranges.size()), false});
        if (labels.size() > 1)
            diagnose(diagnostics, DiagnosticSeverity::Warning, "messages.accompaniment.measure_alignment");
    }
    if (ranges.back().endTick - ranges.back().startTick < barTicks)
        diagnose(diagnostics, DiagnosticSeverity::Warning, "messages.accompaniment.incomplete_final_measure");
    return ranges;
}

std::vector<HarmonicInterval> harmonicIntervals(const Score &score, const std::vector<SourceNote> &notes,
                                                std::vector<Diagnostic> &diagnostics)
{
    const auto ranges = measureRanges(score, diagnostics);
    std::vector<std::int64_t> boundaries{0};
    for (const auto &range : ranges)
        boundaries.push_back(range.endTick);
    for (std::size_t i = 1; i < notes.size(); ++i)
    {
        if (notes[i].key != notes[i - 1].key)
            boundaries.push_back(notes[i].startTick);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    std::vector<HarmonicInterval> intervals;
    std::size_t sourceIndex = 0;
    for (std::size_t i = 0; i + 1 < boundaries.size(); ++i)
    {
        while (sourceIndex + 1 < notes.size() && notes[sourceIndex].endTick <= boundaries[i])
            ++sourceIndex;
        intervals.push_back({boundaries[i], boundaries[i + 1], notes[sourceIndex].key});
    }
    return intervals;
}

struct Tonality
{
    int tonic;
    HarmonicMode mode;
};

double tonalityScore(const std::vector<SourceNote> &notes, std::int64_t first, std::int64_t end,
                     const Tonality &tonality)
{
    static constexpr std::array<double, 12> major{5, -4, 2, -4, 3, 2, -4, 4, -4, 2, -4, 1};
    static constexpr std::array<double, 12> minor{5, -4, 2, 3, -4, 2, -4, 4, 2, -4, 2, 1};
    const auto &profile = tonality.mode == HarmonicMode::Minor ? minor : major;
    double weight = 0;
    double fit = 0;
    const SourceNote *lastSounding = nullptr;
    auto firstNote =
        std::lower_bound(notes.begin(), notes.end(), first,
                         [](const SourceNote &note, std::int64_t tick) { return note.endTick <= tick; });
    for (auto it = firstNote; it != notes.end() && it->startTick < end; ++it)
    {
        const auto &note = *it;
        const auto overlap = std::min(note.endTick, end) - std::max(note.startTick, first);
        if (overlap <= 0 || note.pitchClass < 0)
            continue;
        const double duration = static_cast<double>(overlap);
        fit += duration * profile[static_cast<std::size_t>((note.pitchClass - tonality.tonic + 12) % 12)];
        weight += duration;
        lastSounding = &note;
    }
    if (weight == 0)
        return 0;
    // Cadence is a useful differentiator when relative major/minor share all notes.
    if (lastSounding && lastSounding->pitchClass == tonality.tonic)
        fit += weight * 0.8;
    return fit / weight;
}

Tonality selectTonality(const std::vector<SourceNote> &notes, std::int64_t first, std::int64_t end, int sourceKey,
                        const AccompanimentSettings &settings)
{
    const int majorTonic = settings.harmonicTonic >= 0 ? settings.harmonicTonic : sourceKey;
    const int minorTonic = settings.harmonicTonic >= 0 ? settings.harmonicTonic : (sourceKey + 9) % 12;
    if (settings.mode == HarmonicMode::Minor)
        return {minorTonic, HarmonicMode::Minor};
    if (settings.mode == HarmonicMode::Major)
        return {majorTonic, HarmonicMode::Major};
    const Tonality major{majorTonic, HarmonicMode::Major};
    const Tonality minor{minorTonic, HarmonicMode::Minor};
    return tonalityScore(notes, first, end, minor) > tonalityScore(notes, first, end, major) ? minor : major;
}

std::array<int, 3> chordPitches(int rootPitchClass, ChordQuality quality, int inversion)
{
    int root = 48 + rootPitchClass;
    if (root > 55)
        root -= 12;
    std::array<int, 3> pitches{root, root + (quality == ChordQuality::Major ? 4 : 3),
                               root + (quality == ChordQuality::Diminished ? 6 : 7)};
    for (int i = 0; i < inversion; ++i)
    {
        const int lowest = pitches[0];
        pitches[0] = pitches[1];
        pitches[1] = pitches[2];
        pitches[2] = lowest + 12;
    }
    return pitches;
}

ChordSpan selectChord(const std::vector<SourceNote> &notes, const HarmonicInterval &interval,
                      const Tonality &tonality, const ChordSpan *previous, bool finalInterval, int beatTicks)
{
    static constexpr std::array<int, 7> majorRoots{0, 2, 4, 5, 7, 9, 11};
    static constexpr std::array<ChordQuality, 7> majorQualities{
        ChordQuality::Major, ChordQuality::Minor, ChordQuality::Minor,     ChordQuality::Major,
        ChordQuality::Major, ChordQuality::Minor, ChordQuality::Diminished};
    static constexpr std::array<int, 8> minorRoots{0, 2, 3, 5, 7, 8, 10, 7};
    static constexpr std::array<ChordQuality, 8> minorQualities{
        ChordQuality::Minor, ChordQuality::Diminished, ChordQuality::Major, ChordQuality::Minor,
        ChordQuality::Major, ChordQuality::Major,      ChordQuality::Major, ChordQuality::Minor};
    const bool minor = tonality.mode == HarmonicMode::Minor;
    const std::size_t count = minor ? minorRoots.size() : majorRoots.size();
    ChordSpan selected{interval.startTick, interval.endTick, tonality.tonic,
                       minor ? ChordQuality::Minor : ChordQuality::Major};
    double bestFit = -std::numeric_limits<double>::infinity();
    bool hasSound = false;
    for (std::size_t candidate = 0; candidate < count; ++candidate)
    {
        const int root = (tonality.tonic + (minor ? minorRoots[candidate] : majorRoots[candidate])) % 12;
        const auto quality = minor ? minorQualities[candidate] : majorQualities[candidate];
        const auto pitches = chordPitches(root, quality, 0);
        double fit = 0;
        double weight = 0;
        auto firstNote =
            std::lower_bound(notes.begin(), notes.end(), interval.startTick,
                             [](const SourceNote &note, std::int64_t tick) { return note.endTick <= tick; });
        for (auto it = firstNote; it != notes.end() && it->startTick < interval.endTick; ++it)
        {
            const auto &note = *it;
            const auto overlap =
                std::min(note.endTick, interval.endTick) - std::max(note.startTick, interval.startTick);
            if (overlap <= 0 || note.pitchClass < 0)
                continue;
            hasSound = true;
            double importance = static_cast<double>(overlap);
            if (note.startTick == interval.startTick)
                importance *= 1.5;
            else if ((note.startTick - interval.startTick) % beatTicks == 0)
                importance *= 1.15;
            const bool member = std::any_of(pitches.begin(), pitches.end(),
                                            [&note](int pitch) { return pitch % 12 == note.pitchClass; });
            fit += importance * (member ? 1.0 : -0.8);
            weight += importance;
        }
        if (weight > 0)
            fit /= weight;
        if (root == tonality.tonic)
            fit += finalInterval ? 0.16 : 0.04;
        if (previous && previous->quality != ChordQuality::None)
        {
            if (previous->rootPitchClass == root && previous->quality == quality)
                fit += 0.06;
            const int intervalClass = (root - previous->rootPitchClass + 12) % 12;
            if (intervalClass == 5 || intervalClass == 7)
                fit += 0.03;
        }
        if (fit > bestFit)
        {
            bestFit = fit;
            selected.rootPitchClass = root;
            selected.quality = quality;
        }
    }
    if (!hasSound)
    {
        selected.quality = ChordQuality::None;
        return selected;
    }
    double smallestMovement = std::numeric_limits<double>::infinity();
    for (int inversion = 0; inversion < 3; ++inversion)
    {
        const auto pitches = chordPitches(selected.rootPitchClass, selected.quality, inversion);
        double movement = 0;
        if (previous && previous->quality != ChordQuality::None)
        {
            const auto previousPitches =
                chordPitches(previous->rootPitchClass, previous->quality, previous->inversion);
            for (std::size_t voice = 0; voice < pitches.size(); ++voice)
                movement += std::abs(pitches[voice] - previousPitches[voice]);
        }
        else
        {
            movement = std::abs((pitches[0] + pitches[1] + pitches[2]) / 3.0 - 58.0);
        }
        if (movement < smallestMovement)
        {
            smallestMovement = movement;
            selected.inversion = inversion;
        }
    }
    return selected;
}

} // namespace

bool AccompanimentArrangement::valid() const
{
    return !hasErrors(diagnostics);
}

std::string accompanimentFingerprint(const Score &score)
{
    std::uint64_t hash = 14695981039346656037ULL;
    const auto append = [&hash](std::uint64_t field)
    {
        for (int byte = 0; byte < 8; ++byte)
        {
            hash ^= field & 0xff;
            hash *= 1099511628211ULL;
            field >>= 8;
        }
    };
    append(1);
    append(score.tonic);
    append(score.beatsPerBar);
    append(score.beatUnit);
    append(score.notes.size());
    for (const auto &note : score.notes)
    {
        append(note.degree);
        append(note.octave);
        append(note.accidental);
        append(note.durationTicks);
        append(note.keyOverride);
        append(note.measure);
        append(note.tieToNext ? 1 : 0);
    }
    append(score.repeats.size());
    for (const auto &repeat : score.repeats)
    {
        append(repeat.firstNote);
        append(repeat.endNote);
        append(repeat.count);
        append(repeat.firstEndingNote);
    }
    std::ostringstream fingerprint;
    fingerprint << "accompaniment-v1-" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return fingerprint.str();
}

AccompanimentArrangement generateAccompaniment(const Score &score, const AccompanimentSettings &settings)
{
    AccompanimentArrangement arrangement;
    arrangement.settings = settings;
    arrangement.melodyFingerprint = accompanimentFingerprint(score);
    arrangement.diagnostics = buildTimeline(score).diagnostics;
    validateSettings(settings, arrangement.diagnostics);
    if (!supportedMeter(score))
        diagnose(arrangement.diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.meter_unsupported");
    if (!arrangement.valid())
        return arrangement;
    const auto notes = sourceNotes(score);
    if (notes.back().endTick > 100000LL * ticksPerBar(score))
    {
        diagnose(arrangement.diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.event_limit");
        return arrangement;
    }
    const auto intervals = harmonicIntervals(score, notes, arrangement.diagnostics);
    if (intervals.size() > 100000)
    {
        diagnose(arrangement.diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.event_limit");
        return arrangement;
    }
    diagnose(arrangement.diagnostics, DiagnosticSeverity::Warning, "messages.accompaniment.harmonic_suggestion");
    std::size_t firstInterval = 0;
    while (firstInterval < intervals.size())
    {
        std::size_t endInterval = firstInterval + 1;
        while (endInterval < intervals.size() && intervals[endInterval].key == intervals[firstInterval].key)
            ++endInterval;
        const auto tonality =
            selectTonality(notes, intervals[firstInterval].startTick, intervals[endInterval - 1].endTick,
                           intervals[firstInterval].key, settings);
        for (auto i = firstInterval; i < endInterval; ++i)
        {
            const ChordSpan *previous = arrangement.chords.empty() ? nullptr : &arrangement.chords.back();
            arrangement.chords.push_back(selectChord(notes, intervals[i], tonality, previous, i + 1 == endInterval,
                                                     ticksPerMetronomeBeat(score)));
        }
        firstInterval = endInterval;
    }
    if (std::any_of(score.notes.begin(), score.notes.end(), [](const Note &note) { return note.accidental != 0; }))
        diagnose(arrangement.diagnostics, DiagnosticSeverity::Warning, "messages.accompaniment.chromatic_notes");
    return arrangement;
}

std::vector<SourceMeasureRange> sourceMeasureRanges(const Score &score)
{
    if (!score.writtenMeasures.empty())
    {
        if (score.writtenMeasures.size() > 10000 || score.notes.size() > 100000)
            return {};
        std::int64_t sourceDuration = 0;
        for (const auto &note : score.notes)
        {
            if (note.durationTicks <= 0 || note.durationTicks > TicksPerQuarter * 256)
                return {};
            sourceDuration += note.durationTicks;
        }
        std::vector<SourceMeasureRange> ranges;
        std::int64_t endTick = 0;
        for (const auto &measure : score.writtenMeasures)
        {
            if (measure.startTick != endTick || measure.durationTicks <= 0 ||
                measure.durationTicks > 1000000000 - endTick || measure.number < -1 || measure.number > 1000000 ||
                measure.pageIndex < 0 || measure.pageIndex > 9999 || ticksPerBar(measure) == 0)
                return {};
            if (measure.number == -1 && (measure.startTick != 0 || measure.durationTicks >= ticksPerBar(measure)))
                return {};
            endTick += measure.durationTicks;
            ranges.push_back({measure.startTick, endTick, measure.number,
                              ranges.empty() && measure.durationTicks < ticksPerBar(measure)});
        }
        return endTick == sourceDuration ? ranges : std::vector<SourceMeasureRange>{};
    }
    std::vector<Diagnostic> diagnostics;
    return measureRanges(score, diagnostics);
}

TonalityAt resolveHarmonicTonality(const Score &score, const AccompanimentSettings &settings,
                                   std::int64_t sourceTick)
{
    std::vector<Diagnostic> diagnostics;
    validateSettings(settings, diagnostics);
    if (hasErrors(diagnostics) || !buildTimeline(score).valid())
        return {};
    const auto notes = sourceNotes(score);
    const auto tick = std::clamp(sourceTick, std::int64_t{0}, notes.back().endTick - 1);
    const auto next =
        std::upper_bound(notes.begin(), notes.end(), tick,
                         [](std::int64_t position, const SourceNote &note) { return position < note.startTick; });
    std::size_t first = static_cast<std::size_t>(std::distance(notes.begin(), next) - 1);
    std::size_t end = first + 1;
    const int key = notes[first].key;
    while (first > 0 && notes[first - 1].key == key)
        --first;
    while (end < notes.size() && notes[end].key == key)
        ++end;
    const auto tonality = selectTonality(notes, notes[first].startTick, notes[end - 1].endTick, key, settings);
    return {tonality.mode, tonality.tonic};
}

std::vector<AccompanimentCandidate> generateAccompanimentCandidates(const Score &score,
                                                                    const AccompanimentSettings &settings)
{
    const auto base = generateAccompaniment(score, settings);
    if (!base.valid())
        return {};

    auto stable = base;
    stable.name = "Stable";
    stable.settings.pattern = AccompanimentPattern::BlockChords;
    for (auto &chord : stable.chords)
        chord.inversion = 0;

    auto flowing = base;
    flowing.name = "Flowing";
    flowing.settings.pattern = AccompanimentPattern::Arpeggio;
    flowing.settings.chordVelocity = std::max(1, settings.chordVelocity - 8);
    flowing.settings.bassVelocity = std::max(1, settings.bassVelocity - 6);

    auto sparse = base;
    sparse.name = "Sparse";
    sparse.settings.pattern = AccompanimentPattern::Sparse;
    sparse.settings.chordVelocity = std::max(1, settings.chordVelocity - 16);
    sparse.settings.bassVelocity = std::max(1, settings.bassVelocity - 14);
    for (auto &chord : sparse.chords)
    {
        if (chord.quality != ChordQuality::None)
            chord.inversion = 1;
    }

    return {{"stable", "controls.accompaniment.stable", std::move(stable)},
            {"flowing", "controls.accompaniment.flowing", std::move(flowing)},
            {"sparse", "controls.accompaniment.sparse", std::move(sparse)}};
}

std::vector<Diagnostic> validateAccompaniment(const Score &score, const AccompanimentArrangement &arrangement)
{
    auto diagnostics = buildTimeline(score).diagnostics;
    validateSettings(arrangement.settings, diagnostics);
    if (!supportedMeter(score))
        diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.meter_unsupported");
    if (arrangement.generatorVersion != 1)
        diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.version");
    if (arrangement.melodyFingerprint != accompanimentFingerprint(score))
        diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.stale");
    if (arrangement.chords.size() > 100000)
    {
        diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.event_limit");
        return diagnostics;
    }
    std::int64_t sourceDuration = 0;
    for (const auto &note : score.notes)
        sourceDuration += note.durationTicks;
    std::int64_t previousEnd = 0;
    for (const auto &chord : arrangement.chords)
    {
        if (chord.startTick < 0 || chord.startTick >= chord.endTick || chord.endTick > sourceDuration)
            diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.chord_interval");
        if (chord.startTick < previousEnd)
            diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.chord_overlap");
        previousEnd = chord.endTick;
        if (chord.rootPitchClass < 0 || chord.rootPitchClass > 11)
            diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.chord_pitch");
        if (chord.quality != ChordQuality::Major && chord.quality != ChordQuality::Minor &&
            chord.quality != ChordQuality::Diminished && chord.quality != ChordQuality::None)
            diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.chord_quality");
        if (chord.inversion < 0 || chord.inversion > 2 ||
            (chord.quality == ChordQuality::None && chord.inversion != 0))
            diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.chord_inversion");
        if (chord.patternOverride && !validPattern(*chord.patternOverride))
            diagnose(diagnostics, DiagnosticSeverity::Error, "messages.accompaniment.settings_range");
    }
    // Generation errors remain errors, but warnings are not persisted validity flags.
    for (const auto &diagnostic : arrangement.diagnostics)
    {
        if (diagnostic.severity == DiagnosticSeverity::Error)
            diagnostics.push_back(diagnostic);
    }
    return diagnostics;
}

} // namespace singlilt

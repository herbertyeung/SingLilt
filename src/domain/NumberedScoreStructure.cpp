// Shared written measures and performed traversal for authored scores.
#include "NumberedComposition.h"
#include "NumberedEditing.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace singlilt
{
namespace
{
[[noreturn]] void invalidStructure()
{
    throw std::invalid_argument("ui.editor.invalid_structure");
}
} // namespace

std::vector<NumberedMeasureSpan> numberedMeasures(const Score &score, const NumberedComposition &composition)
{
    return numberedMeasures(score, composition.structure, compositionDuration(composition));
}

std::vector<NumberedMeasureSpan> numberedMeasures(const Score &score, const NumberedScoreStructure &structure,
                                                  std::int64_t duration)
{
    const auto &specs = structure.measures;
    WrittenMeasure meter;
    meter.beatsPerBar = score.beatsPerBar;
    meter.beatUnit = score.beatUnit;
    double bpm = score.bpm;
    int tonic = score.tonic;
    std::set<int> ids;
    std::vector<NumberedMeasureSpan> result;
    std::int64_t tick = 0;
    for (std::size_t i = 0;
         i < specs.size() || i < std::size_t(std::max(1, score.editorMinimumBars)) || tick < duration; ++i)
    {
        if (i >= MaximumEditorBars)
            invalidStructure();
        int id = 0;
        if (i < specs.size())
        {
            const auto &spec = specs[i];
            id = spec.id;
            if (id <= 0 || id >= structure.nextId || !ids.insert(id).second ||
                (spec.beats == 0) != (spec.unit == 0) || spec.beats < 0 || spec.unit < 0 ||
                !std::isfinite(spec.bpm) ||
                (spec.bpm != 0 && (spec.bpm < MinimumScoreBpm || spec.bpm > MaximumScoreBpm)) || spec.tonic < -1 ||
                spec.tonic > 11)
                invalidStructure();
            if (spec.beats)
            {
                meter.beatsPerBar = spec.beats;
                meter.beatUnit = spec.unit;
            }
            if (spec.bpm)
                bpm = spec.bpm;
            if (spec.tonic >= 0)
                tonic = spec.tonic;
        }
        const int ticks = ticksPerBar(meter);
        if (ticks <= 0)
            invalidStructure();
        result.push_back({id, tick, tick + ticks, meter.beatsPerBar, meter.beatUnit, tonic, bpm});
        tick += ticks;
    }
    return result;
}

std::size_t numberedMeasureAt(const std::vector<NumberedMeasureSpan> &measures, std::int64_t tick)
{
    const auto it = std::upper_bound(measures.begin(), measures.end(), tick,
                                     [](auto position, const auto &measure) { return position < measure.end; });
    return it == measures.end() ? measures.size() - 1 : std::size_t(it - measures.begin());
}

Timeline buildNumberedWrittenTimeline(Score score, const std::vector<NumberedMeasureSpan> &measures, int transpose)
{
    // Stamp a temporary validation guide; authored notes inherit their key from the measure map.
    std::int64_t tick = 0;
    for (auto &note : score.notes)
    {
        note.keyOverride = measures[numberedMeasureAt(measures, tick)].tonic;
        tick += note.durationTicks;
    }
    return buildTimeline(score, transpose);
}

PerformedScoreMap performedScoreMap(const Score &score, const NumberedComposition &composition)
{
    PerformedScoreMap map;
    map.written = numberedMeasures(score, composition);
    map.writtenEnd = compositionDuration(composition);
    struct Repeat
    {
        std::size_t first, last, ending;
        int count;
    };
    std::vector<Repeat> repeats;
    const auto index = [&](int id)
    {
        if (id <= 0)
            invalidStructure();
        const auto it = std::find_if(map.written.begin(), map.written.end(),
                                     [id](const auto &measure) { return measure.id == id; });
        if (it == map.written.end())
            invalidStructure();
        return std::size_t(it - map.written.begin());
    };
    if (composition.structure.repeats.size() > map.written.size())
        invalidStructure();
    for (const auto &repeat : composition.structure.repeats)
    {
        const auto first = index(repeat.first);
        const auto last = index(repeat.last);
        const auto ending = repeat.firstEnding ? index(repeat.firstEnding) : last + 1;
        if (first > last || repeat.count < 2 || repeat.count > 8 ||
            (repeat.firstEnding && (ending <= first || ending > last)))
            invalidStructure();
        repeats.push_back({first, last, ending, repeat.count});
        map.writtenEnd = std::max(map.writtenEnd, map.written[last].end);
    }
    std::sort(repeats.begin(), repeats.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (std::size_t i = 1; i < repeats.size(); ++i)
        if (repeats[i].first <= repeats[i - 1].last)
            invalidStructure();
    const auto append = [&](std::size_t measure, int pass)
    {
        const auto &span = map.written[measure];
        const auto length = std::min(span.end, map.writtenEnd) - span.start;
        if (length > 0)
        {
            map.order.push_back({measure, map.duration, pass});
            map.duration += length;
        }
    };
    std::size_t measure = 0;
    int tailPass = 0;
    for (const auto &repeat : repeats)
    {
        while (measure < repeat.first)
            append(measure++, tailPass);
        for (int pass = 0; pass < repeat.count; ++pass)
            for (auto current = repeat.first; current < (pass == 0 ? repeat.last + 1 : repeat.ending); ++current)
                append(current, pass);
        measure = repeat.last + 1;
        tailPass = repeat.count - 1;
    }
    while (measure < map.written.size())
        append(measure++, tailPass);
    return map;
}

Timeline compositionTimeline(const Score &metadata, const NumberedComposition &composition)
{
    auto score = compositionPlaybackScore(metadata, composition);
    if (score.notes.empty())
        return {};
    if (composition.structure.measures.empty())
    {
        auto timeline = buildTimeline(score);
        // A guide at the note limit can leave an unnotated silent tail.
        const auto end = compositionDuration(composition);
        if (timeline.valid() && timeline.durationTicks < end)
        {
            const auto beatTicks = ticksPerMetronomeBeat(score);
            const auto barTicks = ticksPerBar(score);
            for (std::int64_t tick = 0; tick < end; tick += beatTicks)
                timeline.metronomeBeats.push_back({tick, tick % barTicks == 0});
            timeline.durationTicks = end;
        }
        return timeline;
    }
    const auto map = performedScoreMap(metadata, composition);
    Timeline timeline;
    timeline.bpm = metadata.bpm;
    timeline.durationTicks = map.duration;
    std::vector<TempoChange> tempos;
    std::vector<std::int64_t> starts{0};
    for (const auto &note : score.notes)
        starts.push_back(starts.back() + note.durationTicks);
    std::int64_t previousWrittenEnd = -1;
    for (const auto &occurrence : map.order)
    {
        const auto &measure = map.written[occurrence.measure];
        const auto end = std::min(measure.end, map.writtenEnd);
        if (tempos.empty() || tempos.back().bpm != measure.bpm)
            tempos.push_back({occurrence.start, measure.bpm});
        WrittenMeasure meter;
        meter.beatsPerBar = measure.beats;
        meter.beatUnit = measure.unit;
        for (auto beat = measure.start; beat < end; beat += ticksPerMetronomeBeat(meter))
            timeline.metronomeBeats.push_back({occurrence.start + beat - measure.start, beat == measure.start});
        auto index =
            std::size_t(std::upper_bound(starts.begin(), starts.end(), measure.start) - starts.begin() - 1);
        while (index < score.notes.size() && starts[index] < end)
        {
            const auto start = std::max(starts[index], measure.start);
            const auto length = std::min(starts[index + 1], end) - start;
            const auto &note = score.notes[index];
            const auto pitch = midiPitch(note, map.written[numberedMeasureAt(map.written, starts[index])].tonic);
            if (timeline.events.size() >= 100000 || (note.degree && (pitch < 0 || pitch > 127)))
                invalidStructure();
            bool attack = pitch >= 0;
            if (attack && previousWrittenEnd == start && !timeline.events.empty())
            {
                const auto &previous = timeline.events.back();
                if (previous.midiPitch == pitch &&
                    (previous.sourceNoteIndex == index ||
                     (previous.sourceNoteIndex + 1 == index && score.notes[previous.sourceNoteIndex].tieToNext)))
                    attack = false;
            }
            timeline.events.push_back({index, occurrence.start + start - measure.start, length, pitch, attack,
                                       occurrence.pass, programForVerse(score, 0),
                                       lyricForVerse(note, occurrence.pass), pitch < 0 ? 0 : score.baseVelocity});
            previousWrittenEnd = start + length;
            ++index;
        }
    }
    timeline.setTempoChanges(tempos);
    return timeline;
}
} // namespace singlilt

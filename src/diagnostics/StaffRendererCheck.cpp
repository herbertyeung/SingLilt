// Staff-layout, glyph, and source-position regressions.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#include "StaffRendererCheck.h"

#include "domain/StaffPerformance.h"
#include "domain/Timeline.h"
#include "ui/ScoreView.h"
#include "ui/StaffRenderer.h"

#include <QApplication>
#include <QByteArrayView>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace singlilt
{
namespace
{

void appendNote(Score &score, int degree, int octave, int alteration, int ticks, const std::string &lyric = {})
{
    Note note;
    note.id = static_cast<int>(score.notes.size()) + 1;
    note.degree = degree;
    note.octave = octave;
    note.accidental = alteration;
    note.durationTicks = ticks;
    note.lyric = lyric;
    std::int64_t sourceTick = 0;
    for (const auto &previous : score.notes)
        sourceTick += previous.durationTicks;
    note.measure = static_cast<int>(sourceTick / ticksPerBar(score));
    score.notes.push_back(note);
}

bool sameSpelling(const std::optional<StaffSpelling> &first, const std::optional<StaffSpelling> &second)
{
    if (first.has_value() != second.has_value())
        return false;
    return !first ||
           (first->step == second->step && first->alter == second->alter && first->octave == second->octave);
}

bool sameMaterial(const Score &first, const Score &second)
{
    if (first.title != second.title || first.imagePath != second.imagePath || first.tonic != second.tonic ||
        first.bpm != second.bpm || first.beatsPerBar != second.beatsPerBar || first.beatUnit != second.beatUnit ||
        first.versePrograms != second.versePrograms || first.baseVelocity != second.baseVelocity ||
        first.accentBeats != second.accentBeats || first.notes.size() != second.notes.size() ||
        first.repeats.size() != second.repeats.size() ||
        first.writtenMeasures.size() != second.writtenMeasures.size())
        return false;
    for (std::size_t i = 0; i < first.notes.size(); ++i)
    {
        const auto &a = first.notes[i];
        const auto &b = second.notes[i];
        if (a.id != b.id || a.degree != b.degree || a.octave != b.octave || a.accidental != b.accidental ||
            a.durationTicks != b.durationTicks || a.lyric != b.lyric || a.confidence != b.confidence ||
            a.keyOverride != b.keyOverride || a.tieToNext != b.tieToNext || a.verseLyrics != b.verseLyrics ||
            !sameSpelling(a.staffSpelling, b.staffSpelling))
            return false;
    }
    for (std::size_t i = 0; i < first.repeats.size(); ++i)
    {
        const auto &a = first.repeats[i];
        const auto &b = second.repeats[i];
        if (a.firstNote != b.firstNote || a.endNote != b.endNote || a.count != b.count ||
            a.firstEndingNote != b.firstEndingNote)
            return false;
    }
    for (std::size_t i = 0; i < first.writtenMeasures.size(); ++i)
    {
        const auto &a = first.writtenMeasures[i];
        const auto &b = second.writtenMeasures[i];
        if (a.startTick != b.startTick || a.durationTicks != b.durationTicks || a.number != b.number ||
            a.beatsPerBar != b.beatsPerBar || a.beatUnit != b.beatUnit || a.pageIndex != b.pageIndex)
            return false;
    }
    return true;
}

bool sameTimeline(const Timeline &first, const Timeline &second)
{
    if (!first.valid() || !second.valid() || first.durationTicks != second.durationTicks ||
        first.bpm != second.bpm || first.events.size() != second.events.size() ||
        first.metronomeBeats.size() != second.metronomeBeats.size())
        return false;
    for (std::size_t i = 0; i < first.events.size(); ++i)
    {
        const auto &a = first.events[i];
        const auto &b = second.events[i];
        if (a.sourceNoteIndex != b.sourceNoteIndex || a.startTick != b.startTick ||
            a.durationTicks != b.durationTicks || a.midiPitch != b.midiPitch || a.attack != b.attack ||
            a.verseIndex != b.verseIndex || a.program != b.program || a.lyric != b.lyric ||
            a.velocity != b.velocity)
            return false;
    }
    for (std::size_t i = 0; i < first.metronomeBeats.size(); ++i)
        if (first.metronomeBeats[i].startTick != second.metronomeBeats[i].startTick ||
            first.metronomeBeats[i].downbeat != second.metronomeBeats[i].downbeat)
            return false;
    return true;
}

bool anchorsInside(const Score &score, const QImage &image)
{
    return !image.isNull() && image.width() == 1440 && image.height() <= 16000 &&
           std::all_of(score.notes.begin(), score.notes.end(),
                       [&image](const Note &note)
                       {
                           const auto &source = note.source;
                           return std::isfinite(source.x) && std::isfinite(source.y) && source.width > 0 &&
                                  source.height > 0 && source.x >= 0 && source.y >= 0 &&
                                  source.x + source.width <= image.width() &&
                                  source.y + source.height <= image.height();
                       });
}

QByteArray imageDigest(const QImage &image)
{
    return QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()),
        QCryptographicHash::Sha256);
}

int connectedBarlines(const QImage &image, int firstX, int top, int bottom)
{
    int count = 0;
    int lastColumn = -100;
    for (int x = firstX; x < image.width(); ++x)
    {
        bool connected = true;
        for (int y = top; y <= bottom && connected; ++y)
        {
            const QRgb pixel = image.pixel(x, y);
            connected = qRed(pixel) < 110 && qGreen(pixel) < 125 && qBlue(pixel) < 150;
        }
        if (connected)
        {
            if (x - lastColumn > 10)
                ++count;
            lastColumn = x;
        }
    }
    return count;
}

int darkRegion(const QImage &image, QRect region)
{
    region = region.intersected(image.rect());
    int pixels = 0;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
        {
            const QRgb color = image.pixel(x, y);
            pixels += qRed(color) < 100 && qGreen(color) < 115 && qBlue(color) < 145;
        }
    return pixels;
}

} // namespace

void runStaffRendererCheck(const QCommandLineParser &args, QApplication &app)
{
    QTimer::singleShot(
        0, &app,
        [&args, &app]
        {
            QJsonArray checks;
            QJsonArray screenshots;
            bool passed = true;
            const auto check = [&](const QString &name, bool valid)
            {
                checks.append(QJsonObject{{"name", name}, {"passed", valid}});
                passed &= valid;
            };
            try
            {
                const QString folder = QFileInfo(args.value("report")).absolutePath();
                if (!QDir().mkpath(folder))
                    throw std::runtime_error("Staff renderer report directory could not be created");
                const auto capture = [&](Score &score, const StaffRenderOptions &options, const QString &name)
                {
                    const Score original = score;
                    const Timeline before = buildTimeline(score);
                    const QImage image = renderStaffScore(score, options);
                    check(name + " material unchanged", sameMaterial(original, score));
                    check(name + " performed MIDI, timing, ties, verses and accents unchanged",
                          sameTimeline(before, buildTimeline(score)));
                    check(name + " finite, positive click anchors inside bounded image",
                          anchorsInside(score, image));
                    const QString path = folder + "/" + name + ".png";
                    check(name + " image saved", image.save(path));
                    screenshots.append(path);
                    return image;
                };

                Score treble;
                treble.title = "SingLilt - Staff notation";
                appendNote(treble, 1, 0, 0, 480, "Sing / Hear");
                appendNote(treble, 2, 0, 0, 240, "a / the");
                appendNote(treble, 3, 0, 0, 240, "lit / notes");
                appendNote(treble, 4, 0, 1, 480, "tle / take");
                appendNote(treble, 4, 0, 0, 480, "song / flight");
                appendNote(treble, 5, 0, 0, 720, "La / Ah");
                appendNote(treble, 5, 0, 0, 240);
                treble.notes.back().tieToNext = true;
                appendNote(treble, 5, 0, 0, 480);
                appendNote(treble, 0, 0, 0, 480);
                appendNote(treble, 1, 1, 0, 960, "Sing / Hear");
                appendNote(treble, 7, 0, -1, 480, "a / the");
                appendNote(treble, 1, 1, 0, 480, "gain / tune");
                treble.repeats.push_back({0, treble.notes.size(), 2, 9});
                const auto trebleImage = capture(treble, {}, "staff-treble");
                check("Bundled Bravura family loaded", QFontDatabase::families().contains("Bravura"));
                check("Pitch direction is correctly higher on the staff",
                      treble.notes[0].source.y > treble.notes[1].source.y &&
                          treble.notes[1].source.y > treble.notes[2].source.y);
                check("Sharp and subsequent natural preserve the same staff position",
                      std::abs(treble.notes[3].source.y - treble.notes[4].source.y) < 0.01);

                Score bass;
                bass.title = "SingLilt - Bass clef";
                bass.tonic = 5;
                for (int degree = 1; degree <= 7; ++degree)
                    appendNote(bass, degree, -2, 0, degree == 7 ? 960 : 480, "Bass");
                capture(bass, {true, -1, false}, "staff-bass");

                Score pickup;
                pickup.title = "SingLilt - Pickup and written measures";
                appendNote(pickup, 5, 0, 0, 480);
                appendNote(pickup, 1, 1, 0, 960);
                appendNote(pickup, 7, 0, 0, 960);
                appendNote(pickup, 1, 1, 0, 1920);
                pickup.notes[0].measure = 0;
                pickup.notes[1].measure = pickup.notes[2].measure = 1;
                pickup.notes[3].measure = 2;
                capture(pickup, {}, "staff-pickup");
                check("Short pickup retains written measure boundaries rather than cumulative full bars",
                      pickup.notes[0].measure == 0 && pickup.notes[1].measure == 1 &&
                          pickup.notes[2].measure == 1 && pickup.notes[3].measure == 2);

                Score rhythm;
                rhythm.title = "SingLilt - Rhythm and rests";
                constexpr std::array<int, 14> Durations{1920, 960,  480, 240, 120, 60, 30,
                                                        15,   1440, 720, 360, 180, 90, 840};
                for (const int ticks : Durations)
                    appendNote(rhythm, 1, 0, 0, ticks);
                for (const int ticks : Durations)
                    appendNote(rhythm, 0, 0, 0, ticks);
                appendNote(rhythm, 2, 0, 0, 160, "160t");
                appendNote(rhythm, 0, 0, 0, 96, "96t rest");
                // A symbol gallery uses separate written measures to avoid splitting the examples.
                for (std::size_t i = 0; i < rhythm.notes.size(); ++i)
                    rhythm.notes[i].measure = static_cast<int>(i);
                capture(rhythm, {}, "staff-rhythm");

                std::set<QByteArray> noteSymbols;
                std::set<QByteArray> restSymbols;
                for (const int ticks : {1920, 960, 480, 240, 120, 60, 720, 840, 160})
                {
                    Score note;
                    appendNote(note, 1, 0, 0, ticks);
                    noteSymbols.insert(imageDigest(renderStaffScore(note)));
                    Score rest;
                    appendNote(rest, 0, 0, 0, ticks);
                    restSymbols.insert(imageDigest(renderStaffScore(rest)));
                }
                check("Whole, half, quarter, eighth, sixteenth, 32nd, dotted and exact-tick notes are distinct",
                      noteSymbols.size() == 9);
                check("Whole, half, quarter, eighth, sixteenth, 32nd, dotted and exact-tick rests are distinct",
                      restSymbols.size() == 9);

                Score extremes;
                extremes.title = "SingLilt - Ledger lines and spelling";
                appendNote(extremes, 1, -5, 0, 480); // MIDI 0.
                appendNote(extremes, 5, 5, 0, 480);  // MIDI 127.
                appendNote(extremes, 1, 0, 1, 480);
                extremes.notes.back().staffSpelling = StaffSpelling{'D', -1, 4};
                appendNote(extremes, 1, 0, 1, 480);
                extremes.notes.back().staffSpelling = StaffSpelling{'C', 1, 4};
                appendNote(extremes, 3, 0, 0, 960);
                appendNote(extremes, 3, 0, 0, 960);
                capture(extremes, {}, "staff-extremes");
                check("Enharmonic D flat and C sharp retain different written positions",
                      std::abs(extremes.notes[2].source.y - extremes.notes[3].source.y + 6) < 0.01);
                extremes.notes[2].staffSpelling = StaffSpelling{'D', -1, 9};
                renderStaffScore(extremes);
                check("Stale spelling is discarded when its octave no longer matches the sounding MIDI",
                      std::abs(extremes.notes[2].source.y - extremes.notes[3].source.y) < 0.01);

                Score grand;
                grand.title = "SingLilt - Piano grand staff";
                for (int degree = 1; degree <= 4; ++degree)
                    appendNote(grand, degree, 0, 0, 480, "Piano");
                StaffPerformance performance;
                performance.durationTicks = 1920;
                performance.timingFingerprint = staffTimingFingerprint(grand);
                const auto addStaffNote = [&](int pitch, int staff, int tick, int duration)
                {
                    StaffPerformanceNote note;
                    note.midiPitch = pitch;
                    note.staff = staff;
                    note.startTick = tick;
                    note.durationTicks = duration;
                    note.voice = staff == 1 ? "1" : "2";
                    performance.notes.push_back(note);
                };
                for (int pitch : {60, 64, 67})
                    addStaffNote(pitch, 1, 0, 480);
                for (int pitch : {62, 64, 65})
                    addStaffNote(pitch, 1, 480, 480);
                addStaffNote(64, 1, 960, 480);
                addStaffNote(65, 1, 1440, 480);
                for (int pitch : {48, 55})
                    addStaffNote(pitch, 2, 0, 960);
                addStaffNote(52, 2, 960, 480);
                const Score grandBefore = grand;
                const auto performanceBefore = performance;
                const QImage grandImage = renderGrandStaffScore(grand, performance);
                check("Grand staff preserves its guide and performed MIDI material",
                      sameMaterial(grandBefore, grand) &&
                          sameTimeline(buildTimeline(grandBefore), buildTimeline(grand)));
                bool allStaffAnchors = performance.notes.size() == performanceBefore.notes.size();
                for (std::size_t i = 0; i < performance.notes.size(); ++i)
                {
                    const auto &note = performance.notes[i];
                    const auto &original = performanceBefore.notes[i];
                    const auto &source = note.source;
                    allStaffAnchors &=
                        note.midiPitch == original.midiPitch && note.startTick == original.startTick &&
                        note.durationTicks == original.durationTicks && note.staff == original.staff &&
                        note.voice == original.voice && source.width > 0 && source.height > 0 && source.x >= 0 &&
                        source.y >= 0 && source.x + source.width <= grandImage.width() &&
                        source.y + source.height <= grandImage.height();
                }
                check("Every full-performance pitch has a bounded source anchor without changing timing",
                      allStaffAnchors && anchorsInside(grand, grandImage));
                const auto centerX = [](const SourceRect &source) { return source.x + source.width / 2; };
                check("Simultaneous chord tones share time X instead of being serialized",
                      std::abs(centerX(performance.notes[0].source) - centerX(performance.notes[1].source)) <
                              0.01 &&
                          std::abs(centerX(performance.notes[0].source) - centerX(performance.notes[2].source)) <
                              0.01 &&
                          std::abs(centerX(performance.notes[0].source) - centerX(performance.notes[8].source)) <
                              0.01);
                check("Two staves retain high and low voices at distinct vertical positions",
                      performance.notes[8].source.y > performance.notes[0].source.y + 80);
                check("Adjacent chord seconds receive only a one-notehead collision offset",
                      std::abs(performance.notes[3].source.x - performance.notes[4].source.x) == 14);
                check("Guide highlights match the original primary staff note",
                      std::abs(grand.notes[0].source.x - performance.notes[0].source.x) < 0.01 &&
                          std::abs(grand.notes[0].source.y - performance.notes[0].source.y) < 0.01);
                const QString grandPath = folder + "/staff-grand.png";
                check("Grand staff image with chord seconds and the left-hand rest gap saved",
                      grandImage.save(grandPath));
                screenshots.append(grandPath);

                for (int beamLevel = 1; beamLevel <= 3; ++beamLevel)
                {
                    Score beamScore;
                    beamScore.title = "SingLilt - Explicit beam polygons";
                    const int duration = 480 / (1 << beamLevel);
                    StaffPerformance beamed;
                    beamed.staffCount = 1;
                    beamed.durationTicks = duration * 4;
                    for (int noteIndex = 0; noteIndex < 4; ++noteIndex)
                    {
                        appendNote(beamScore, 5, 0, 0, duration);
                        StaffPerformanceNote note;
                        note.startTick = duration * noteIndex;
                        note.durationTicks = duration;
                        note.midiPitch = 67;
                        note.stemDirection = StaffStemDirection::Up;
                        for (int level = 1; level <= beamLevel; ++level)
                            note.beams.push_back({level, noteIndex == 0   ? StaffBeamKind::Begin
                                                         : noteIndex == 3 ? StaffBeamKind::End
                                                                          : StaffBeamKind::Continue});
                        beamed.notes.push_back(note);
                    }
                    beamed.timingFingerprint = staffTimingFingerprint(beamScore);
                    const Score beforeBeams = beamScore;
                    const auto beforePerformance = beamed;
                    auto plainScore = beamScore;
                    auto plain = beamed;
                    for (auto &note : plain.notes)
                        note.beams.clear();
                    const QImage flagsImage = renderGrandStaffScore(plainScore, plain);
                    const QImage beamsImage = renderGrandStaffScore(beamScore, beamed);
                    const auto &first = beamed.notes[0].source;
                    const auto &second = beamed.notes[1].source;
                    const double headY = first.y + first.height / 2;
                    const int middleX = qRound((first.x + first.width / 2 + second.x + second.width / 2) / 2);
                    bool polygonsVisible = true;
                    for (int level = 1; level <= beamLevel; ++level)
                    {
                        const QRect interior(middleX - 6, qRound(headY - 42 + (level - 1) * 7 + 1), 12, 3);
                        polygonsVisible &=
                            darkRegion(beamsImage, interior) >= 24 && darkRegion(flagsImage, interior) < 12;
                    }
                    check(
                        QString("Explicit %1-level beams fill real polygons between stem columns").arg(beamLevel),
                        polygonsVisible);
                    bool beamMetadataPreserved = beamed.notes.size() == beforePerformance.notes.size();
                    for (std::size_t i = 0; i < beamed.notes.size(); ++i)
                    {
                        const auto &note = beamed.notes[i];
                        const auto &before = beforePerformance.notes[i];
                        beamMetadataPreserved &=
                            note.startTick == before.startTick && note.durationTicks == before.durationTicks &&
                            note.midiPitch == before.midiPitch && note.pageIndex == before.pageIndex &&
                            note.stemDirection == before.stemDirection && note.beams.size() == before.beams.size();
                        for (std::size_t level = 0; level < note.beams.size(); ++level)
                            beamMetadataPreserved &= note.beams[level].level == before.beams[level].level &&
                                                     note.beams[level].kind == before.beams[level].kind;
                    }
                    check(QString("Beam %1 engraving does not alter pitch, ticks, page or source notation")
                              .arg(beamLevel),
                          beamMetadataPreserved && sameMaterial(beforeBeams, beamScore) &&
                              sameTimeline(buildTimeline(beforeBeams), buildTimeline(beamScore)));
                    auto down = beforePerformance;
                    auto downScore = beforeBeams;
                    for (auto &note : down.notes)
                        note.stemDirection = StaffStemDirection::Down;
                    const QImage downImage = renderGrandStaffScore(downScore, down);
                    const QRect lowerBeam(middleX - 6, qRound(headY + 42 - 3), 12, 3);
                    check(QString("Explicit downward stems put beam %1 below the unchanged heads").arg(beamLevel),
                          darkRegion(downImage, lowerBeam) >= 24 && darkRegion(beamsImage, lowerBeam) < 12);
                    const QString path = folder + QString("/staff-beam-level-%1.png").arg(beamLevel);
                    check(QString("Explicit beam %1 illustration saved").arg(beamLevel), beamsImage.save(path));
                    screenshots.append(path);
                    auto unclosed = beforePerformance;
                    auto unclosedScore = beforeBeams;
                    unclosed.notes.back().beams.clear();
                    check(QString("Unclosed beam %1 does not crash or discard written notes").arg(beamLevel),
                          !renderGrandStaffScore(unclosedScore, unclosed).isNull() &&
                              unclosed.notes.size() == beforePerformance.notes.size());
                }
                Score hookScore;
                appendNote(hookScore, 5, 0, 0, 60);
                StaffPerformance hooks;
                hooks.staffCount = 1;
                hooks.durationTicks = 60;
                StaffPerformanceNote hook;
                hook.midiPitch = 67;
                hook.durationTicks = 60;
                hook.stemDirection = StaffStemDirection::Up;
                hook.beams = {{1, StaffBeamKind::ForwardHook},
                              {2, StaffBeamKind::BackwardHook},
                              {3, StaffBeamKind::ForwardHook}};
                hooks.notes.push_back(hook);
                const QImage hookImage = renderGrandStaffScore(hookScore, hooks);
                const auto &hookAnchor = hooks.notes[0].source;
                const int hookStemX = qRound(hookAnchor.x + hookAnchor.width - 3.7);
                const double hookHeadY = hookAnchor.y + hookAnchor.height / 2;
                check("Forward and backward hooks create visible partial polygons, not independent flags",
                      darkRegion(hookImage, QRect(hookStemX + 6, qRound(hookHeadY - 41), 8, 3)) >= 16 &&
                          darkRegion(hookImage, QRect(hookStemX - 14, qRound(hookHeadY - 34), 8, 3)) >= 16);
                auto noStem = hooks;
                auto noStemScore = hookScore;
                noStem.notes[0].beams.clear();
                noStem.notes[0].stemDirection = StaffStemDirection::None;
                const QImage noStemImage = renderGrandStaffScore(noStemScore, noStem);
                check("Explicit stem none suppresses the stem rather than inferring a direction",
                      darkRegion(noStemImage, QRect(hookStemX - 1, qRound(hookHeadY - 30), 3, 16)) < 4);
                Score splitBeamScore;
                splitBeamScore.writtenMeasures = {{0, 480, 0, 4, 4, 0}, {480, 480, 1, 4, 4, 1}};
                StaffPerformance splitBeams;
                splitBeams.staffCount = 1;
                splitBeams.durationTicks = 960;
                for (int index = 0; index < 4; ++index)
                {
                    appendNote(splitBeamScore, 5, 0, 0, 240);
                    splitBeamScore.notes.back().measure = splitBeamScore.notes.back().pageIndex = index / 2;
                    StaffPerformanceNote note;
                    note.midiPitch = 67;
                    note.startTick = index * 240;
                    note.durationTicks = 240;
                    note.pageIndex = index / 2;
                    note.stemDirection = StaffStemDirection::Up;
                    note.beams = {{1, index == 0   ? StaffBeamKind::Begin
                                      : index == 3 ? StaffBeamKind::End
                                                   : StaffBeamKind::Continue}};
                    splitBeams.notes.push_back(note);
                }
                splitBeams.timingFingerprint = staffTimingFingerprint(splitBeamScore);
                const auto splitBeamBefore = splitBeams;
                const auto splitBeamPages = renderGrandStaffPages(splitBeamScore, splitBeams);
                bool splitBeamsStable = splitBeamPages.size() == 2;
                for (std::size_t index = 0; index < splitBeams.notes.size(); ++index)
                    splitBeamsStable &=
                        splitBeams.notes[index].pageIndex == splitBeamBefore.notes[index].pageIndex &&
                        splitBeams.notes[index].startTick == splitBeamBefore.notes[index].startTick &&
                        splitBeams.notes[index].durationTicks == splitBeamBefore.notes[index].durationTicks &&
                        splitBeams.notes[index].beams.front().kind ==
                            splitBeamBefore.notes[index].beams.front().kind;
                check(
                    "Explicit beam continuation at a physical page boundary retains page identity and raw timing",
                    splitBeamsStable && !splitBeamPages[0].isNull() && !splitBeamPages[1].isNull());

                Score overfull;
                overfull.title = "SingLilt - Explicit overfull measures";
                appendNote(overfull, 1, 0, 0, 1920);
                appendNote(overfull, 3, 0, 0, 720);
                appendNote(overfull, 2, 0, 0, 1920);
                overfull.notes[0].measure = overfull.notes[1].measure = 0;
                overfull.notes[2].measure = 1;
                const Score overfullBefore = overfull;
                const auto labelsAndTicksUnchanged = [&overfullBefore](const Score &rendered)
                {
                    if (!sameMaterial(overfullBefore, rendered))
                        return false;
                    for (std::size_t i = 0; i < rendered.notes.size(); ++i)
                        if (rendered.notes[i].measure != overfullBefore.notes[i].measure ||
                            rendered.notes[i].durationTicks != overfullBefore.notes[i].durationTicks)
                            return false;
                    return true;
                };
                auto overfullMono = overfull;
                const auto firstMono = renderStaffScore(overfullMono);
                const auto secondMono = renderStaffScore(overfullMono);
                check("Two renders preserve explicit overfull monophonic measures and their ticks",
                      labelsAndTicksUnchanged(overfullMono) && firstMono == secondMono);
                StaffPerformance overfullPerformance;
                overfullPerformance.durationTicks = 4560;
                overfullPerformance.timingFingerprint = staffTimingFingerprint(overfull);
                const auto addOverfullNote = [&](int pitch, int staff, int tick, int duration)
                {
                    StaffPerformanceNote note;
                    note.midiPitch = pitch;
                    note.staff = staff;
                    note.voice = staff == 1 ? "1" : "2";
                    note.startTick = tick;
                    note.durationTicks = duration;
                    overfullPerformance.notes.push_back(note);
                };
                addOverfullNote(60, 1, 0, 1920);
                addOverfullNote(67, 1, 0, 1920);
                addOverfullNote(64, 1, 1920, 720);
                addOverfullNote(62, 1, 2640, 1920);
                addOverfullNote(48, 2, 0, 2640);
                addOverfullNote(50, 2, 2640, 1920);
                const auto overfullPerformanceBefore = overfullPerformance;
                const QImage firstOverfull = renderGrandStaffScore(overfull, overfullPerformance);
                const QImage secondOverfull = renderGrandStaffScore(overfull, overfullPerformance);
                check("Two grand-staff renders preserve explicit overfull labels without rebuild drift",
                      labelsAndTicksUnchanged(overfull) && firstOverfull == secondOverfull &&
                          sameTimeline(buildTimeline(overfullBefore), buildTimeline(overfull)));
                bool allOverfullAnchors =
                    overfullPerformance.notes.size() == overfullPerformanceBefore.notes.size();
                for (std::size_t i = 0; i < overfullPerformance.notes.size(); ++i)
                {
                    const auto &note = overfullPerformance.notes[i];
                    const auto &before = overfullPerformanceBefore.notes[i];
                    const auto &source = note.source;
                    allOverfullAnchors &= note.startTick == before.startTick &&
                                          note.durationTicks == before.durationTicks &&
                                          note.midiPitch == before.midiPitch && note.staff == before.staff &&
                                          source.width > 0 && source.height > 0 && source.x >= 0 &&
                                          source.y >= 0 && source.x + source.width <= secondOverfull.width() &&
                                          source.y + source.height <= secondOverfull.height();
                }
                check("Overfull grand staff retains every complete voice anchor and duration", allOverfullAnchors);
                const auto &primaryAnchor = overfullPerformance.notes[0].source;
                const auto &otherAnchor = overfullPerformance.notes[4].source;
                check("Two explicit bars draw exactly two connected barlines, not a fictional third measure",
                      connectedBarlines(secondOverfull, static_cast<int>(primaryAnchor.x),
                                        static_cast<int>(primaryAnchor.y + primaryAnchor.height + 8),
                                        static_cast<int>(otherAnchor.y - 8)) == 2);
                const QString overfullPath = folder + "/staff-overfull-two-measures.png";
                check("Explicit overfull two-measure regression image saved", secondOverfull.save(overfullPath));
                screenshots.append(overfullPath);

                Score paged;
                paged.title = "SingLilt - Four physical pages";
                paged.writtenMeasures = {{0, 1920, 0, 4, 4, 0},
                                         {1920, 960, 1, 2, 4, 1},
                                         {2880, 1920, 2, 4, 4, 2},
                                         {4800, 1920, 3, 4, 4, 3}};
                for (int i = 0; i < 14; ++i)
                {
                    appendNote(paged, 1, 0, 0, 480);
                    paged.notes.back().measure = i < 4 ? 0 : i < 6 ? 1 : i < 10 ? 2 : 3;
                    paged.notes.back().pageIndex = paged.notes.back().measure;
                }
                StaffPerformance pagePerformance;
                pagePerformance.durationTicks = 6720;
                pagePerformance.primaryProgram = 40;
                pagePerformance.otherProgram = 35;
                pagePerformance.clefChanges = {{0, 1, false}, {0, 2, true}, {1920, 1, true}, {2400, 1, false}};
                for (int i = 0; i < 14; ++i)
                {
                    StaffPerformanceNote note;
                    note.startTick = i * 480;
                    note.durationTicks = 480;
                    note.midiPitch = 60;
                    note.sourceNoteIndex = i;
                    note.pageIndex = paged.notes[static_cast<std::size_t>(i)].pageIndex;
                    pagePerformance.notes.push_back(note);
                }
                StaffPerformanceNote heldBass;
                heldBass.midiPitch = 48;
                heldBass.staff = 2;
                heldBass.voice = "bass";
                heldBass.durationTicks = 6720;
                pagePerformance.notes.push_back(heldBass);
                pagePerformance.timingFingerprint = staffTimingFingerprint(paged);
                const Score pagedBefore = paged;
                const auto pagePerformanceBefore = pagePerformance;
                const auto beforePagedTimeline = buildTimeline(paged);
                check("Four-page fixture obeys written-measure page ownership before rendering",
                      beforePagedTimeline.valid());
                const auto pageImages = renderGrandStaffPages(paged, pagePerformance);
                const auto repeatedPages = renderGrandStaffPages(paged, pagePerformance);
                check("Four written page indices produce four independent bounded images",
                      pageImages.size() == 4 && repeatedPages == pageImages &&
                          std::all_of(pageImages.begin(), pageImages.end(), [](const QImage &page)
                                      { return page.width() == 1440 && page.height() < 4000; }));
                bool pagedAnchors = pagePerformance.notes.size() == pagePerformanceBefore.notes.size();
                for (std::size_t i = 0; i < pagePerformance.notes.size(); ++i)
                {
                    const auto &note = pagePerformance.notes[i];
                    const auto &before = pagePerformanceBefore.notes[i];
                    const auto &source = note.source;
                    const int expectedPage = note.startTick < 1920   ? 0
                                             : note.startTick < 2880 ? 1
                                             : note.startTick < 4800 ? 2
                                                                     : 3;
                    pagedAnchors &=
                        note.pageIndex == expectedPage && note.midiPitch == before.midiPitch &&
                        note.startTick == before.startTick && note.durationTicks == before.durationTicks &&
                        source.width > 0 && source.height > 0 && source.x >= 0 && source.y >= 0 &&
                        source.x + source.width <= pageImages[static_cast<std::size_t>(expectedPage)].width() &&
                        source.y + source.height <= pageImages[static_cast<std::size_t>(expectedPage)].height();
                }
                check("Every global voice keeps its attack page and page-relative click anchor", pagedAnchors);
                check("Page-local visual ties do not crop the global held bass or create invalid sound ties",
                      pagePerformance.notes.back().durationTicks == 6720 &&
                          buildStaffPerformancePlan(paged, buildTimeline(paged), pagePerformance).valid() &&
                          sameTimeline(beforePagedTimeline, buildTimeline(paged)) &&
                          sameMaterial(pagedBefore, paged));
                check("A mid-measure F-to-G clef change moves the same MIDI pitch to its correct staff position",
                      pagePerformance.notes[5].source.y > pagePerformance.notes[4].source.y + 60 &&
                          pagePerformance.notes[4].midiPitch == pagePerformance.notes[5].midiPitch);
                check("Selected hand instruments survive all page rendering",
                      pagePerformance.primaryProgram == 40 && pagePerformance.otherProgram == 35);
                for (std::size_t page = 0; page < pageImages.size(); ++page)
                {
                    const QString path = folder + QString("/staff-page-%1.png").arg(page + 1);
                    check(QString("Physical page %1 saved separately").arg(page + 1), pageImages[page].save(path));
                    screenshots.append(path);
                }

                for (int fifths = -7; fifths <= 7; ++fifths)
                {
                    Score key;
                    key.tonic = (fifths * 7 % 12 + 12) % 12;
                    for (int degree = 1; degree <= 7; ++degree)
                        appendNote(key, degree, 0, 0, 240);
                    const Score original = key;
                    const QImage image = renderStaffScore(key, {false, fifths, false});
                    check(QString("Key signature %1 stays bounded and preserves pitch").arg(fifths),
                          anchorsInside(key, image) && sameMaterial(original, key));
                }

                ScoreView view;
                view.resize(760, 520);
                view.setScore(trebleImage, treble);
                view.show();
                view.fitWidth();
                QApplication::processEvents();
                int clicked = -1;
                view.noteClicked = [&](int index) { clicked = index; };
                for (std::size_t i = 0; i < treble.notes.size(); ++i)
                {
                    view.setCurrent(static_cast<int>(i), true);
                    QApplication::processEvents();
                    const auto &anchor = treble.notes[i].source;
                    const QPoint local =
                        view.mapFromScene(QPointF(anchor.x + anchor.width / 2, anchor.y + anchor.height / 2));
                    QMouseEvent press(QEvent::MouseButtonPress, QPointF(local),
                                      QPointF(view.viewport()->mapToGlobal(local)), Qt::LeftButton, Qt::LeftButton,
                                      Qt::NoModifier);
                    QApplication::sendEvent(view.viewport(), &press);
                    check(QString("Staff note %1 click and playback-follow anchor").arg(i),
                          clicked == static_cast<int>(i) && view.currentNoteIndex() == static_cast<int>(i));
                }
                view.hide();

                Score invalid;
                appendNote(invalid, 1, 0, 0, 0);
                bool rejected = false;
                try
                {
                    renderStaffScore(invalid);
                }
                catch (const std::runtime_error &)
                {
                    rejected = true;
                }
                check("Invalid zero duration is rejected", rejected);
                Score oversized;
                appendNote(oversized, 1, 0, 0, TicksPerQuarter * 256);
                for (int i = 0; i < 9; ++i)
                    oversized.notes.push_back(oversized.notes.front());
                const Score oversizedBefore = oversized;
                rejected = false;
                try
                {
                    renderStaffScore(oversized);
                }
                catch (const std::runtime_error &)
                {
                    rejected = true;
                }
                check("Oversized layout is rejected before touching original anchors",
                      rejected && sameMaterial(oversizedBefore, oversized) &&
                          oversized.notes[0].source.width == 0 && oversized.notes[0].line == 0 &&
                          oversized.notes[0].measure == 0);
                rejected = false;
                try
                {
                    renderStaffScore(treble, {false, 8, false});
                }
                catch (const std::runtime_error &)
                {
                    rejected = true;
                }
                check("Key signatures beyond seven accidentals are rejected", rejected);
            }
            catch (const std::exception &error)
            {
                check(QString::fromUtf8(error.what()), false);
            }
            QSaveFile report(args.value("report"));
            const QByteArray bytes =
                QJsonDocument(QJsonObject{{"passed", passed}, {"checks", checks}, {"screenshots", screenshots}})
                    .toJson();
            const bool saved =
                report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() && report.commit();
            app.exit(passed && saved ? 0 : 2);
        });
}

} // namespace singlilt

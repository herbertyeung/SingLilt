// MusicXML parsing into written scores and complete staff voices.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT

#pragma once

#include "domain/Score.h"
#include "domain/StaffPerformance.h"
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <optional>
#include <vector>

namespace singlilt
{
struct MusicXmlClef
{
    int staff = 1;
    QString sign = "G";
    int line = 2;
    int octaveChange = 0;
};

struct MusicXmlAttributes
{
    std::int64_t startTick = 0;
    int measure = 0;
    int fifths = 0;
    QString mode = "major";
    int beats = 4;
    int beatUnit = 4;
    std::vector<MusicXmlClef> clefs{{}};
};

struct MusicXmlLyric
{
    QString number = "1";
    QString text;
    QString syllabic;
};

struct MusicXmlNoteEvent
{
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
    int measure = 0;
    int midiPitch = -1;
    QString step;
    int alter = 0;
    int octave = 4;
    bool rest = false;
    bool chord = false;
    bool tieStart = false;
    bool tieStop = false;
    bool tiedStart = false;
    bool tiedStop = false;
    QString type;
    int dots = 0;
    int actualNotes = 1;
    int normalNotes = 1;
    std::vector<MusicXmlLyric> lyrics;
    int pageIndex = 0;
    std::vector<StaffBeam> beams;
    StaffStemDirection stemDirection = StaffStemDirection::Auto;
};

struct MusicXmlTrack
{
    std::size_t partIndex = 0;
    QString partId;
    QString partName;
    int staff = 1;
    QString voice = "1";
    std::vector<MusicXmlNoteEvent> events;
};

struct MusicXmlMeasure
{
    QString number;
    std::int64_t startTick = 0;
    std::int64_t endTick = 0;
    bool implicit = false;
    int pageIndex = 0;
};

struct MusicXmlRepeat
{
    int firstMeasure = 0;
    int endMeasure = 0;
    int count = 2;
};

struct MusicXmlPart
{
    QString id;
    QString name;
    std::vector<MusicXmlMeasure> measures;
    std::vector<MusicXmlAttributes> attributes;
    std::vector<MusicXmlRepeat> repeats;
    QStringList conversionErrors;
    int staffCount = 1;
};

struct MusicXmlTempo
{
    std::int64_t startTick = 0;
    double quarterBpm = 90.0;
};

struct MusicXmlImportResult
{
    QString sourcePath;
    QByteArray sourceSha256;
    QString title;
    std::vector<MusicXmlPart> parts;
    std::vector<MusicXmlTrack> tracks;
    std::vector<MusicXmlTempo> tempos;
    QStringList warnings;
    QString error;
    bool valid() const;
};

struct MusicXmlScoreResult
{
    std::optional<Score> score;
    int keyFifths = 0;
    bool minorKey = false;
    MusicXmlClef clef;
    QStringList warnings;
    QString error;
    bool valid() const;
};

struct MusicXmlPerformanceResult
{
    MusicXmlScoreResult selectedMelody;
    std::optional<StaffPerformance> performance;
    QStringList warnings;
    QString error;
    bool valid() const;
};

// No source writes or external DTD/entity resolution occur during import.
MusicXmlImportResult importMusicXml(const QString &path);
MusicXmlImportResult parseMusicXml(const QByteArray &xml, const QString &sourceName = {});
// The caller must choose a part/staff/voice explicitly; polyphony is never flattened.
MusicXmlScoreResult musicXmlTrackToScore(const MusicXmlImportResult &imported, std::size_t trackIndex);
// Every pitched source event is retained; the chosen track supplies only a highest-note practice guide.
// Explicit OMR review mode retains unresolved raw ties and marks their entire chains for untied audition.
MusicXmlPerformanceResult musicXmlToPerformance(const MusicXmlImportResult &imported,
                                                std::size_t primaryTrackIndex,
                                                bool reviewUnresolvedSoundTies = false);
} // namespace singlilt

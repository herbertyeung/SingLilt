// Versioned authored-voice JSON payload.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#include "NumberedCompositionStore.h"
#include "ProjectStore.h"
#include "domain/NumberedEditing.h"
#include <QJsonArray>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace singlilt
{
namespace
{
int positiveInteger(const QJsonValue &value)
{
    const double number = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(number) || number < 1 || number > std::numeric_limits<int>::max() ||
        std::floor(number) != number)
        throw std::invalid_argument("ui.editor.invalid_composition");
    return int(number);
}
} // namespace
QJsonObject numberedCompositionToJson(const Score &metadata, const NumberedComposition &composition)
{
    validateNumberedComposition(metadata, composition);
    QJsonArray voices;
    for (const auto &voice : composition.voices)
    {
        auto score = metadata;
        score.notes = voice.notes;
        voices.append(QJsonObject{{"id", voice.id},
                                  {"name", QString::fromStdString(voice.name)},
                                  {"notes", scoreToJson(score).value("notes")}});
    }
    QJsonObject result{{"nextId", composition.nextId}, {"voices", voices}};
    if (!composition.structure.measures.empty())
    {
        QJsonArray measures, repeats;
        for (const auto &measure : composition.structure.measures)
            measures.append(QJsonObject{{"id", measure.id},
                                        {"beats", measure.beats},
                                        {"unit", measure.unit},
                                        {"bpm", measure.bpm},
                                        {"tonic", measure.tonic}});
        for (const auto &repeat : composition.structure.repeats)
            repeats.append(QJsonObject{{"first", repeat.first},
                                       {"last", repeat.last},
                                       {"ending", repeat.firstEnding},
                                       {"count", repeat.count}});
        result.insert(
            "structure",
            QJsonObject{{"nextId", composition.structure.nextId}, {"measures", measures}, {"repeats", repeats}});
    }
    return result;
}
NumberedComposition numberedCompositionFromJson(const Score &metadata, const QJsonObject &object)
{
    if (!object.value("voices").isArray() || object.value("voices").toArray().size() > MaximumNumberedVoices)
        throw std::invalid_argument("ui.editor.invalid_composition");
    NumberedComposition composition;
    composition.nextId = positiveInteger(object.value("nextId"));
    for (const auto &entry : object.value("voices").toArray())
    {
        if (!entry.isObject() || !entry.toObject().value("name").isString())
            throw std::invalid_argument("ui.editor.invalid_composition");
        const auto voice = entry.toObject();
        auto score = scoreToJson(metadata);
        score.insert("notes", voice.value("notes"));
        composition.voices.push_back({positiveInteger(voice.value("id")),
                                      voice.value("name").toString().toStdString(),
                                      scoreFromJson(score, object.contains("structure")).notes});
    }
    if (object.contains("structure"))
    {
        const auto structure = object.value("structure").toObject();
        const auto measures = structure.value("measures");
        const auto repeats = structure.value("repeats");
        if (!measures.isArray() || measures.toArray().isEmpty() || measures.toArray().size() > MaximumEditorBars ||
            !repeats.isArray() || repeats.toArray().size() > measures.toArray().size())
            throw std::invalid_argument("ui.editor.invalid_structure");
        const auto integer = [](const QJsonValue &value, int minimum)
        {
            const double number = value.toDouble(minimum - 1);
            if (!value.isDouble() || !std::isfinite(number) || number < minimum ||
                number > std::numeric_limits<int>::max() || std::floor(number) != number)
                throw std::invalid_argument("ui.editor.invalid_structure");
            return int(number);
        };
        composition.structure.nextId = positiveInteger(structure.value("nextId"));
        for (const auto &entry : measures.toArray())
        {
            const auto measure = entry.toObject();
            if (!measure.value("bpm").isDouble())
                throw std::invalid_argument("ui.editor.invalid_structure");
            composition.structure.measures.push_back(
                {positiveInteger(measure.value("id")), integer(measure.value("beats"), 0),
                 integer(measure.value("unit"), 0), measure.value("bpm").toDouble(),
                 integer(measure.value("tonic"), -1)});
        }
        for (const auto &entry : repeats.toArray())
        {
            const auto repeat = entry.toObject();
            composition.structure.repeats.push_back(
                {positiveInteger(repeat.value("first")), positiveInteger(repeat.value("last")),
                 integer(repeat.value("ending"), 0), positiveInteger(repeat.value("count"))});
        }
    }
    validateNumberedComposition(metadata, composition);
    if (!composition.structure.measures.empty())
    {
        compositionTimeline(metadata, composition);
        compositionPlaybackPlan(metadata, composition);
    }
    return composition;
}
} // namespace singlilt

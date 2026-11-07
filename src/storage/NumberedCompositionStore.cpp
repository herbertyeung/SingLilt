// Versioned authored-voice JSON payload.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#include "NumberedCompositionStore.h"
#include "ProjectStore.h"
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
    return {{"nextId", composition.nextId}, {"voices", voices}};
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
                                      voice.value("name").toString().toStdString(), scoreFromJson(score).notes});
    }
    validateNumberedComposition(metadata, composition);
    return composition;
}
} // namespace singlilt

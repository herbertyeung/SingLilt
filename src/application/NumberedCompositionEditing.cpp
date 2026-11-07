// Validated, copy-based composition structure edits.
// Copyright (c) 2026 Herbert Yeung
// Author: Herbert Yeung
// SPDX-License-Identifier: MIT
#include "NumberedCompositionEditing.h"
#include "domain/NumberedEditing.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace singlilt
{
int addNumberedVoice(NumberedComposition &composition, std::string name)
{
    if (composition.voices.size() >= MaximumNumberedVoices ||
        composition.nextId == std::numeric_limits<int>::max())
        throw std::invalid_argument("ui.editor.voice_limit");
    const int id = composition.nextId++;
    composition.voices.push_back({id, std::move(name), {}});
    return id;
}
void removeNumberedVoice(NumberedComposition &composition, int id)
{
    if (composition.voices.size() <= 1)
        throw std::invalid_argument("ui.editor.last_voice");
    numberedVoice(composition, id);
    std::erase_if(composition.voices, [id](const auto &voice) { return voice.id == id; });
}
void moveNumberedVoice(NumberedComposition &composition, int id, int direction)
{
    for (std::size_t i = 0; i < composition.voices.size(); ++i)
        if (composition.voices[i].id == id)
        {
            const auto target = std::int64_t(i) + direction;
            if (target >= 0 && target < std::int64_t(composition.voices.size()))
                std::swap(composition.voices[i], composition.voices[std::size_t(target)]);
            return;
        }
    throw std::invalid_argument("ui.editor.invalid_composition");
}
void editCompositionMeasure(Score &metadata, NumberedComposition &composition, int measure, bool remove)
{
    auto candidate = composition;
    const int count = compositionBars(metadata, composition);
    for (auto &voice : candidate.voices)
    {
        auto score = metadata;
        score.editorMinimumBars = count;
        score.notes = voice.notes;
        const bool changed = remove ? removeNumberedBar(score, measure) : insertNumberedBar(score, measure);
        if (!changed)
            throw std::invalid_argument("ui.editor.structure_unavailable");
        voice.notes = std::move(score.notes);
    }
    metadata.editorMinimumBars = std::max(1, count + (remove ? -1 : 1));
    composition = std::move(candidate);
}
} // namespace singlilt

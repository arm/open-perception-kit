/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "FrameInputs.h"

#include "Utils.h"

namespace amp::tracker::frameinputs {

EmbeddingBatch collectEmbeddings(const amp::Perception &perception, const Config &config) {
    EmbeddingBatch embeddings;

    if (!config.useEmbeddings) {
        return embeddings;
    }

    for (const auto &layer : perception.layers) {
        if (layer.contentType != config.embeddingContentType) {
            continue;
        }

        for (const auto &det : layer.detections) {
            if (const auto *embedding = std::get_if<amp::Perception::ObjectEmbedding>(&det)) {
                if (isValidEmbedding(embedding->values)) {
                    embeddings.insert_or_assign(embedding->parentUuid,
                                                std::cref(embedding->values));
                }
            }
        }
    }

    return embeddings;
}

DetectionBatch collectDetections(const amp::Perception &perception, const Config &config) {
    DetectionBatch detections;

    for (const auto &layer : perception.layers) {
        if (layer.contentType != config.contentType) {
            continue;
        }

        for (const auto &det : layer.detections) {
            if (const auto *rect = std::get_if<amp::Perception::Rect>(&det)) {
                detections.push_back(*rect);
            }
        }
    }

    return detections;
}

} // namespace amp::tracker::frameinputs

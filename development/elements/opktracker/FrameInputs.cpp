/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "FrameInputs.h"

#include "Utils.h"

namespace opk::tracker::frameinputs {

EmbeddingBatch collectEmbeddings(const perception::FrameResults &frameResults,
                                 const Config &config) {
    EmbeddingBatch embeddings;

    if (!config.useEmbeddings) {
        return embeddings;
    }

    frameResults.for_each<perception::metadata::ObjectEmbeddingsT>(
        [&embeddings, &config](const auto &payload) {
            if (!payload.layer || payload.layer->content_type != config.embeddingContentType) {
                return;
            }

            for (const auto &embedding : payload.embeddings) {
                if (!embedding || !embedding->object) {
                    continue;
                }
                if (isValidEmbedding(embedding->values)) {
                    embeddings.insert_or_assign(embedding->object->parent_id, &embedding->values);
                }
            }
        });

    return embeddings;
}

DetectionBatch collectDetections(const perception::FrameResults &frameResults,
                                 const Config &config) {
    DetectionBatch detections;

    frameResults.for_each<perception::metadata::BoxDetectionsT>(
        [&detections, &config](const auto &payload) {
            if (!payload.layer || payload.layer->content_type != config.contentType) {
                return;
            }

            for (const auto &detection : payload.detections) {
                if (detection && detection->object && detection->box) {
                    detections.push_back(detection.get());
                }
            }
        });

    return detections;
}

} // namespace opk::tracker::frameinputs

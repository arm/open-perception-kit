/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "FrameInputs.h"

#include "Utils.h"

namespace opk::tracker::frameinputs {

EmbeddingBatch collectEmbeddings(const open_perception_kit::FrameResults &frameResults,
                                 const Config &config) {
    EmbeddingBatch embeddings;

    if (!config.useEmbeddings) {
        return embeddings;
    }

    frameResults.for_each<open_perception_kit::metadata::ObjectEmbeddingsT>(
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

std::vector<open_perception_kit::metadata::BoxDetectionT>
collectDetections(const open_perception_kit::FrameResults &frameResults,
                  const Config &config,
                  std::unique_ptr<open_perception_kit::metadata::ImageSpaceT> &imageSpace) {
    std::vector<open_perception_kit::metadata::BoxDetectionT> detections;

    frameResults.for_each<open_perception_kit::metadata::BoxDetectionsT>(
        [&detections, &config, &imageSpace](const auto &payload) {
            if (!payload.layer || payload.layer->content_type != config.contentType) {
                return;
            }

            for (const auto &detection : payload.detections) {
                if (!detection || !detection->object || !detection->box || !detection->box->space)
                    continue;
                const auto &space = *detection->box->space;
                const auto projected = open_perception_kit::projectBoundingBox(
                    *detection->box,
                    imageSpace ? imageSpace->width : space.width,
                    imageSpace ? imageSpace->height : space.height);
                if (!projected)
                    continue;
                if (!imageSpace) {
                    imageSpace =
                        std::make_unique<open_perception_kit::metadata::ImageSpaceT>(space);
                    imageSpace->origin_x = 0;
                    imageSpace->origin_y = 0;
                    imageSpace->extent_width = space.width;
                    imageSpace->extent_height = space.height;
                }
                auto copy = *detection;
                copy.box->x = static_cast<float>((*projected)[0]);
                copy.box->y = static_cast<float>((*projected)[1]);
                copy.box->width = static_cast<float>((*projected)[2]);
                copy.box->height = static_cast<float>((*projected)[3]);
                copy.box->units = open_perception_kit::metadata::CoordinateUnits::Pixels;
                *copy.box->space = *imageSpace;
                detections.push_back(std::move(copy));
            }
        });

    return detections;
}

} // namespace opk::tracker::frameinputs

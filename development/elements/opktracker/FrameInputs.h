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

#pragma once

#include "Tracker.h"

namespace opk::tracker::frameinputs {

/**
 * @brief Selects unique embedding observations keyed by explicit subject ID.
 *
 * Applies optional space/producer filters, counts duplicates before validating
 * vectors, and resolves unique box/track subjects. Null entries are unavailable.
 *
 * @param frameResults Input frame results containing model outputs for the frame.
 * @param config Tracker configuration used to validate embedding usage.
 * @return EmbeddingBatch Mapping of subject ID to typed embedding view.
 */
EmbeddingBatch collectEmbeddings(const open_perception_kit::FrameResults &frameResults,
                                 const Config &config);

/**
 * @brief Collects and filters detections targeted for tracking.
 *
 * Extracts detection candidates from FrameResults, applies tracker-specific filtering,
 * and returns owned copies projected into a stable full-frame pixel basis.
 *
 * @param frameResults Input frame results for the current frame.
 * @param config Tracker configuration controlling detection selection.
 * @param imageSpace Established tracking basis, initialized by the first valid detection.
 * @return Owned detections in processing order; invalid spatial references are omitted.
 */
std::vector<open_perception_kit::metadata::BoxDetectionT>
collectDetections(const open_perception_kit::FrameResults &frameResults,
                  const Config &config,
                  std::unique_ptr<open_perception_kit::metadata::ImageSpaceT> &imageSpace);

} // namespace opk::tracker::frameinputs

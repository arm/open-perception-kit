/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Tracker.h"

namespace pek::tracker::frameinputs {

/**
 * @brief Collects valid embedding vectors keyed by their parent detection ID.
 *
 * Scans FrameResults embedding payloads, validates dimensions/content,
 * and returns non-owning vector views that participate in association/re-identification.
 *
 * @param frameResults Input frame results containing model outputs for the frame.
 * @param config Tracker configuration used to validate embedding usage.
 * @return EmbeddingBatch Mapping of parent ID to embedding vector view.
 */
EmbeddingBatch collectEmbeddings(const perception::FrameResults &frameResults,
                                 const Config &config);

/**
 * @brief Collects and filters detections targeted for tracking.
 *
 * Extracts detection candidates from FrameResults, applies tracker-specific filtering,
 * and returns selected detection views in processing order.
 *
 * @param frameResults Input frame results for the current frame.
 * @param config Tracker configuration controlling detection selection.
 * @return DetectionBatch Ordered detection view batch.
 */
DetectionBatch collectDetections(const perception::FrameResults &frameResults,
                                 const Config &config);

} // namespace pek::tracker::frameinputs

/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Tracker.h"

namespace pek::tracker::frameinputs {

/**
 * @brief Collects valid embedding vectors keyed by their parent detection UUID.
 *
 * Scans perception layers for embedding payloads, validates dimensions/content,
 * and returns non-owning references to embeddings that should participate in
 * association/re-identification.
 *
 * @param perception Input perception object containing model outputs for the frame.
 * @param config Tracker configuration used to validate embedding usage.
 * @return EmbeddingBatch Mapping of parent UUID to embedding reference.
 */
EmbeddingBatch collectEmbeddings(const pek::Perception &perception, const Config &config);

/**
 * @brief Collects and filters detections targeted for tracking.
 *
 * Extracts detection candidates from perception, applies tracker-specific filtering,
 * and returns selected detections in processing order.
 *
 * @param perception Input perception object for the current frame.
 * @param config Tracker configuration controlling detection selection.
 * @return DetectionBatch Ordered detection batch.
 */
DetectionBatch collectDetections(const pek::Perception &perception, const Config &config);

} // namespace pek::tracker::frameinputs

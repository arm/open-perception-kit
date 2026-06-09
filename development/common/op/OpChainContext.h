/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "mediaio/VideoFrame.h"
#include "pek/Perception.h"
#include "pek/TensorView.h"
#include "pek/Types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pek::op {

/**
 * @brief Execution context passed to each operation in an OpChain.
 *
 * OpChainContext carries the mutable data shared across operations during execution.
 * It contains named video frames for inter-operation media flow, inference tensor information,
 * and the final Perception output structure. Scheduler control is handled by OpChain and
 * process() return signals rather than by mutable context flags.
 */
struct OpChainContext {

    /**
     * @brief Named media video frames for backend-aware frame sharing.
     *
     * VideoFrame preserves backend lifetime, memory type, nanosecond timestamp,
     * and explicit mapping semantics across the OpChain execution.
     */
    std::map<std::string, std::shared_ptr<pek::mediaio::VideoFrame>> videoFrames;

    /**
     * @brief Retrieves a named media video frame by name.
     *
     * @param name Name of the video frame.
     * @return Pointer to the VideoFrame, or nullptr if not found.
     */
    pek::mediaio::VideoFrame *getVideoFrame(const std::string &name) {
        auto it = videoFrames.find(name);
        if (it == videoFrames.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    /**
     * @brief Retrieves a named media video frame by name.
     *
     * @param name Name of the video frame.
     * @return Pointer to the VideoFrame, or nullptr if not found.
     */
    const pek::mediaio::VideoFrame *getVideoFrame(const std::string &name) const {
        auto it = videoFrames.find(name);
        if (it == videoFrames.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    /**
     * @brief Image crops for the inference loop.
     *
     * Populated by preprocessing operations to define which regions of the frame
     * should be sent for inference. The inference operation processes each crop and
     * clears the vector when the loop completes.
     */
    std::vector<pek::PixelRect> inferenceImageCrops;
    /**
     * @brief UUIDs corresponding to each image crop for traceability.
     */
    std::vector<uint64_t> inferenceImageCropUuids;

    /**
     * @brief UUID of the source frame used for the last inference.
     */
    uint64_t inferenceSourceUuid = 0;
    /**
     * @brief Number of output tensors produced by the last inference.
     */
    size_t inferenceOutputTensorCount = 0;
    /**
     * @brief Output tensors from the last inference.
     *
     * Up to MaxTensorCount tensors, indexed by tensor index in the model.
     */
    std::array<pek::TensorView, pek::MaxTensorCount> inferenceOutputTensors{};
    /**
     * @brief Information about the last inference execution (timing, etc.).
     */
    pek::InferenceInfo inferenceInfo;

    /**
     * @brief Flag indicating whether a root Perception::Layer has been set.
     */
    bool hasRootLayer = false;
    /**
     * @brief Root Perception::Layer for hierarchical result storage.
     *
     * Used when operations organize results in a tree structure.
     */
    Perception::Layer rootLayer;

    /**
     * @brief The final Perception output object populated during execution.
     *
     * Postprocessing operations write detection results, classifications, segmentations,
     * and other perception data into this structure. May be null if not populated.
     */
    Perception *perception = nullptr;
};

} // namespace pek::op

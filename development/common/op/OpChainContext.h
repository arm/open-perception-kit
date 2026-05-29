/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/BitmapView.h"
#include "pek/Perception.h"
#include "pek/TensorView.h"
#include "pek/Types.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace pek::op {

/**
 * @brief Execution context passed to each operation in an OpChain.
 *
 * OpChainContext carries the mutable data shared across operations during execution.
 * It contains named bitmap views for inter-operation data flow, inference tensor information,
 * and the final Perception output structure. Scheduler control is handled by OpChain and
 * process() return signals rather than by mutable context flags.
 */
struct OpChainContext {

    /**
     * @brief Named bitmap views for inter-operation image sharing.
     *
     * Operations can store processed video frames or intermediate images here by name
     * (e.g., "frame", "inference_input") and retrieve them later in the chain.
     */
    std::map<std::string, pek::BitmapView> bitmapViews;

    /**
     * @brief Retrieves a named bitmap view by name.
     *
     * @param name Name of the bitmap view.
     * @return Pointer to the BitmapView, or nullptr if not found.
     */
    pek::BitmapView *getBitmapView(const std::string &name) {
        auto it = bitmapViews.find(name);
        if (it == bitmapViews.end()) {
            return nullptr;
        }
        return &it->second;
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
    pek::TensorView inferenceOutputTensors[pek::MaxTensorCount];
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

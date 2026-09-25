/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "opk/AttributeMap.h"
#include "opk/FrameResults.h"
#include "opk/Result.h"
#include "opk/TensorView.h"

#include <string_view>
#include <vector>

namespace opk {

/**
 * @brief Interface for parsing inference output tensors into perception metadata.
 */
struct TensorParser {

    /**
     * @brief Input package passed to parser implementations.
     */
    struct Input {

        /**
         * @brief Constructs parser input with immutable attributes.
         * @param attributes Attribute map shared with the parser.
         */
        Input(const opk::AttributeMap &attributes) : attributes(attributes) {}

        /// Output tensors produced by inference (null entries are allowed).
        opk::TensorView *tensors[opk::MaxTensorCount] = {nullptr};
        /// Immutable parser attributes.
        const opk::AttributeMap &attributes;
        /// Runtime inference information for parser decisions/diagnostics.
        opk::InferenceInfo inferenceInfo;
        /// Identity of the operation and implementation producing result payloads.
        open_perception_kit::metadata::ProducerInfoT producerInfo;
    };

    /**
     * @brief Virtual destructor for polymorphic use.
     */
    virtual ~TensorParser() = default;

    /**
     * @brief Returns the semantic content types this parser appends to FrameResults.
     */
    virtual std::vector<std::string_view> getProvidedContentTypes() const = 0;

    /**
     * @brief Parses tensors into frame results.
     * @param input Parser inputs, tensor array, and attributes.
     * @param results Destination frame results to fill/update.
     * @return Success or error.
     */
    virtual opk::Result<void> parse(const opk::TensorParser::Input &input,
                                    open_perception_kit::FrameResults &results) = 0;
};
} // namespace opk

/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/AttributeMap.h"
#include "pek/FrameResults.h"
#include "pek/Result.h"
#include "pek/TensorView.h"

#include <string_view>
#include <vector>

namespace pek {

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
        Input(const pek::AttributeMap &attributes) : attributes(attributes) {}

        /// Output tensors produced by inference (null entries are allowed).
        pek::TensorView *tensors[pek::MaxTensorCount] = {nullptr};
        /// Immutable parser attributes.
        const pek::AttributeMap &attributes;
        /// Runtime inference information for parser decisions/diagnostics.
        pek::InferenceInfo inferenceInfo;
        /// Identity of the operation and implementation producing result payloads.
        perception::metadata::ProducerInfoT producerInfo;
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
    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    perception::FrameResults &results) = 0;
};
} // namespace pek

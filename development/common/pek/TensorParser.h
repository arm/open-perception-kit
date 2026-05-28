/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/AttributeMap.h"
#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/TensorView.h"

namespace pek {

/**
 * @brief Interface for parsing inference output tensors into perception metadata.
 */
struct TensorParser {

    /**
     * @brief Input package passed to parser implementations.
     */
    struct Input {

        /// Optional/auxiliary layer context carried by the caller.
        Perception::Layer perceptionLayer;

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
    };

    /**
     * @brief Virtual destructor for polymorphic use.
     */
    virtual ~TensorParser() = default;

    /**
     * @brief Parses tensors into a perception layer.
     * @param input Parser inputs, tensor array, and attributes.
     * @param output Destination perception layer to fill/update.
     * @return Success or error.
     */
    virtual pek::Result<void> parse(const pek::TensorParser::Input &input,
                                    pek::Perception::Layer &output) = 0;
};
} // namespace pek
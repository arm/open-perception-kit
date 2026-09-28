/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include "opk/TensorParser.h"

namespace opk::stdop::postproc {

/// @brief Converts SmartTurn's Float32 [1,1] probability into a TurnDetections payload.
///
/// Emits one assessment per successful parse, including negative results and retries.
/// The parser is stateless: confirmation, deduplication, and silence timeouts belong
/// to the consumer. Window bounds describe real audio, excluding front padding.
struct SmartTurnParser : public opk::TensorParser {
    /// Semantic content type advertised by this parser and attached to each output layer.
    static constexpr std::string_view k_content_type = "turnDetection";

    /// @brief Reports the output content type for OpChain binding.
    /// @return A single entry containing k_content_type.
    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    /// @brief Validates one probability and appends its turn-completion assessment.
    ///
    /// Both probability and the threshold attribute (default 0.5) must be finite and
    /// within [0,1]. The decision is true when probability is greater than or equal
    /// to the threshold. Audio timing requires ordered sample bounds, a nonzero hop
    /// fitting the window, and either two ordered timestamps or neither timestamp.
    ///
    /// @param input Tensor 0 with Float32 shape [1,1], threshold attributes, audio
    ///        window metadata, and model/producer identity for the output layer.
    /// @param[in,out] results Receives one TurnDetections payload containing one
    ///        TurnDetection. Existing results are retained; validation errors add nothing.
    /// @return Success, or InvalidData for an invalid tensor, probability, threshold,
    ///         or audio window.
    opk::Result<void> parse(const opk::TensorParser::Input &input,
                            open_perception_kit::FrameResults &results) override;
};

} // namespace opk::stdop::postproc

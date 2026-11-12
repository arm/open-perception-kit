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

#include <string>
#include <vector>

namespace opk::stdop::postproc {

/** @brief Greedy CTC decoding of PaddleOCR recognition probabilities. */
struct PaddleOcrRecognitionParser : public opk::TensorParser {

    std::vector<opk::OutputCapability> getProvidedOutputs() const override {
        return {opk::outputCapability<open_perception_kit::metadata::ClassificationsT>()};
    }

    opk::Result<void> parse(const opk::TensorParser::Input &input,
                            open_perception_kit::FrameResults &results) override;

  private:
    // Immutable after the first successful load; blank, dictionary entries, space.
    std::vector<std::string> vocabulary;
};

} // namespace opk::stdop::postproc

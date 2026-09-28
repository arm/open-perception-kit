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

#include "opk/Result.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"

namespace opk::stdop::postproc {

struct ScrfdParser : public opk::TensorParser {
    static constexpr std::string_view k_content_type = "humanFace";

    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {k_content_type};
    }

    virtual opk::Result<void> parse(const opk::TensorParser::Input &input,
                                    open_perception_kit::FrameResults &results) override;
};

} // namespace opk::stdop::postproc

/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "postproc/DummyParser.h"
#include "Log.h"
#include "opk/TensorParser.h"
#include "opk/Types.h"

#include <cmath>
#include <string>

using namespace opk;
using namespace opk::stdop::postproc;

opk::Result<void> DummyParser::parse(const opk::TensorParser::Input &input,
                                     open_perception_kit::FrameResults &results) {
    (void)results;

    bool log = input.attributes.getBoolOrDefault("log", false);

    if (log) {
        std::string log = "DummyParser got tensors: \n";

        for (size_t i = 0; i < opk::MaxTensorCount; i++) {
            if (input.tensors[i] == nullptr)
                continue;
            log += input.tensors[i]->getShape().toString() + "\n";
        }

        opk::log::info("DummyParser {}", log);
    }

    return {};
}

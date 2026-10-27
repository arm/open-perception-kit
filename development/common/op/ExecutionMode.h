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

#include <optional>
#include <stdexcept>
#include <string_view>

namespace opk::op {

enum class ExecutionMode { Auto, Cpu, Gpu };
enum class ExecutionStage { Preprocessing, Inference, Postprocessing };

constexpr std::string_view toString(const ExecutionMode mode) {
    switch (mode) {
    case ExecutionMode::Auto:
        return "auto";
    case ExecutionMode::Cpu:
        return "cpu";
    case ExecutionMode::Gpu:
        return "gpu";
    }
    throw std::invalid_argument("Invalid execution mode");
}

struct ExecutionDefaults {
    std::optional<ExecutionMode> preprocessing{};
    std::optional<ExecutionMode> inference{};
    std::optional<ExecutionMode> postprocessing{};

    ExecutionMode mode(const ExecutionStage stage) const {
        switch (stage) {
        case ExecutionStage::Preprocessing:
            return preprocessing.value_or(ExecutionMode::Auto);
        case ExecutionStage::Inference:
            return inference.value_or(ExecutionMode::Auto);
        case ExecutionStage::Postprocessing:
            return postprocessing.value_or(ExecutionMode::Auto);
        }
        throw std::invalid_argument("Invalid execution stage");
    }
};

} // namespace opk::op

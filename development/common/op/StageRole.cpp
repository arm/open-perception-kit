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

#include "op/OpChainDescriptor.h"

#include <stdexcept>
#include <utility>

using opk::op::StageRole;

namespace {
constexpr std::pair<StageRole, std::string_view> StageRoles[] = {
    {StageRole::Controller, "controller"},
    {StageRole::Inference, "inference"},
    {StageRole::ImagePreprocess, "image-preprocess"},
    {StageRole::AudioPreprocess, "audio-preprocess"},
    {StageRole::InputPreprocess, "input-preprocess"},
    {StageRole::Postprocess, "postprocess"},
    {StageRole::IntermediateProcess, "intermediate-process"},
};
} // namespace

void opk::op::to_json(nlohmann::json &json, const StageRole role) {
    for (const auto &[candidate, name] : StageRoles) {
        if (role == candidate) {
            json = name;
            return;
        }
    }
    throw std::invalid_argument("Unknown stage role");
}

void opk::op::from_json(const nlohmann::json &json, StageRole &role) {
    const auto name = json.get<std::string>();
    for (const auto &[candidate, value] : StageRoles) {
        if (name == value) {
            role = candidate;
            return;
        }
    }
    throw std::invalid_argument("Unknown stage role: " + name);
}

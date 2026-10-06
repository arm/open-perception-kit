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

#include "op/Op.h"

#include <fmt/format.h>

using namespace opk::op;

open_perception_kit::metadata::ProducerInfoT
Op::producerInfo(std::string_view inferElementId,
                 std::string_view implementation,
                 std::string_view fallbackComponent) const {
    open_perception_kit::metadata::ProducerInfoT result;
    result.instance_id = fmt::format("{}/{}", inferElementId, instanceId);
    result.component = libName.empty() || opName.empty() ? fallbackComponent
                                                         : fmt::format("{}/{}", libName, opName);
    result.implementation = implementation;
    return result;
}

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

#include "open_perception_kit.h"

#include <fmt/format.h>

namespace voice_pipeline {

inline void printKeywords(const open_perception_kit::container::envelope &results) {
    results.for_each<open_perception_kit::metadata::ClassificationsT>([](const auto &payload) {
        if (!payload.layer || payload.layer->infer_element_id != "keywords" ||
            payload.layer->content_type != "keyword")
            return;
        for (const auto &classification : payload.classifications) {
            if (!classification)
                continue;
            for (const auto &candidate : classification->candidates) {
                if (candidate)
                    fmt::print("\r\033[2KKEYWORD {:?} ({:.1f}%)\n",
                               candidate->text,
                               static_cast<double>(candidate->confidence) * 100.0);
            }
        }
    });
}

} // namespace voice_pipeline

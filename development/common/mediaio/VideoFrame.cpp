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

/**
 * @file VideoFrame.cpp
 * @brief Backend-neutral video frame helper methods.
 */

#include "mediaio/VideoFrame.h"

namespace opk::mediaio {

bool VideoFrame::empty() const noexcept {
    return width() == 0 || height() == 0 || planes().empty();
}

size_t VideoFrame::planeCount() const noexcept {
    return planes().size();
}

const DataView *VideoFrame::plane(size_t index) const noexcept {
    const auto framePlanes = planes();
    if (index >= framePlanes.size()) {
        return nullptr;
    }
    return &framePlanes[index];
}

} // namespace opk::mediaio

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

#include <string>
#include <vector>

class TextDisplay {
  public:
    static void appendLines(std::vector<std::string> &lines,
                            const open_perception_kit::metadata::FrameContextT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const open_perception_kit::metadata::BoxDetectionsT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const open_perception_kit::metadata::ObjectTracksT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const open_perception_kit::metadata::ClassificationsT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const open_perception_kit::metadata::PoseEstimationsT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const open_perception_kit::metadata::SegmentationMasksT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const open_perception_kit::metadata::ObjectEmbeddingsT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const open_perception_kit::metadata::TrackTracesT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const open_perception_kit::metadata::PerformanceOverlayT &payload);

    template <typename Payload>
    static std::vector<std::string> formatLines(const Payload &payload) {
        std::vector<std::string> lines;
        appendLines(lines, payload);
        return lines;
    }

    template <typename Payload> static std::string formatText(const Payload &payload) {
        std::string text;
        for (const auto &line : formatLines(payload)) {
            if (!text.empty()) {
                text.push_back('\n');
            }
            text += line;
        }
        return text;
    }
};

/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

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

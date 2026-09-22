/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "perception.h"

#include <string>
#include <vector>

class TextDisplay {
  public:
    static void appendLines(std::vector<std::string> &lines,
                            const perception::metadata::FrameContextT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const perception::metadata::BoxDetectionsT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const perception::metadata::ObjectTracksT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const perception::metadata::ClassificationsT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const perception::metadata::PoseEstimationsT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const perception::metadata::SegmentationMasksT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const perception::metadata::ObjectEmbeddingsT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const perception::metadata::TrackTracesT &payload);
    static void appendLines(std::vector<std::string> &lines,
                            const perception::metadata::PerformanceOverlayT &payload);

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

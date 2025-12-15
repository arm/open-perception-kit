#pragma once

#include <string_view>

namespace amp {

    struct DetectionResultType {
        static constexpr std::string_view GenericObjectRect = "GenericObjectRect";
        static constexpr std::string_view GenericFaceRect = "GenericFaceRect";
    };

    struct NetworkId {
        static constexpr std::string_view YoloObjectDetection = "yolo-object-detection";
        static constexpr std::string_view UltraFace = "ultraface";
    };

}
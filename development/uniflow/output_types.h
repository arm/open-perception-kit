#pragma once

#include <variant>
#include <cstdint>
#include <type_traits>

#include "tensor_types.h"

namespace uflw {

    enum class DetectionType { Unknown, Box, Confidence, Label, ConfidenceLabel, ConfidenceLabelBox };

    struct Box { float x, y, w, h; };
    struct Confidence { float confidence; };
    struct Label { int labelIndex; };

    struct ConfidenceLabel : public Confidence, public Label {};
    struct ConfidenceLabelBox : public Box, public ConfidenceLabel {};

    using DetectionPayload = std::variant<std::monostate, Box, Confidence, Label, ConfidenceLabel, ConfidenceLabelBox>;

    struct Detection {

        DetectionPayload payload;

        DetectionType type() const {
            return std::visit([](auto const& p) -> DetectionType {
                using T = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<T, std::monostate>) return DetectionType::Unknown;
                else if constexpr (std::is_same_v<T, Box>) return DetectionType::Box;
                else if constexpr (std::is_same_v<T, Label>) return DetectionType::Label;
                else if constexpr (std::is_same_v<T, Confidence>) return DetectionType::Confidence;
                else if constexpr (std::is_same_v<T, ConfidenceLabel>) return DetectionType::ConfidenceLabel;
                else if constexpr (std::is_same_v<T, ConfidenceLabelBox>) return DetectionType::ConfidenceLabelBox;
                else return DetectionType::Unknown;
            }, payload);
        }
    };

}

#pragma once

#include <array>
#include <string>

extern const std::array<const char *, 1001U> theImageNetLabels;
extern const std::array<const char *, 80U> theCocoLbels;

namespace amp {

enum class LabelType { ImageNet, Coco };

struct Labels {

    static constexpr size_t getLabelCount(LabelType labelType) {
        switch (labelType) {
        case LabelType::ImageNet:
            return std::tuple_size_v<decltype(theImageNetLabels)>;
        case LabelType::Coco:
            return std::tuple_size_v<decltype(theCocoLbels)>;
        };
        return 0U;
    }

    static std::string getLabel(LabelType labelType, size_t index) {
        if (index >= getLabelCount(labelType))
            return "";

        switch (labelType) {
        case LabelType::ImageNet:
            return theImageNetLabels[index];
        case LabelType::Coco:
            return theCocoLbels[index];
        };
        return "?";
    }
};

} // namespace amp

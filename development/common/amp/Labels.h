#pragma once

#include <string>

extern const char *theImageNetLabels[];
extern const char *theCocoLbels[];

namespace amp {

enum class LabelType { ImageNet, Coco };

struct Labels {

    static size_t getLabelCount(LabelType labelType) {

        switch (labelType) {
        case LabelType::ImageNet:
            return 79;
        case LabelType::Coco:
            return 1000;
        };
        return 0;
    }

    static std::string getLabel(LabelType labelType, size_t index) {
        size_t count = getLabelCount(labelType);
        if (index >= count)
            return "";

        switch (labelType) {
        case LabelType::ImageNet:
            return theImageNetLabels[index];
        case LabelType::Coco:
            return theCocoLbels[index];
        };
        return std::string_view("?");
    }
};

} // namespace amp

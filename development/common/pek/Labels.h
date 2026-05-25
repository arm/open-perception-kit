/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <array>
#include <string>

/** @brief ImageNet class labels (index-aligned, including background). */
extern const std::array<const char *, 1001U> theImageNetLabels;
/** @brief COCO class labels (index-aligned). */
extern const std::array<const char *, 80U> theCocoLbels;

namespace pek {

/**
 * @brief Supported predefined label sets.
 */
enum class LabelType { ImageNet, Coco };

/**
 * @brief Helper accessors for built-in label sets.
 */
struct Labels {

    /**
     * @brief Returns the number of labels in the selected set.
     * @param labelType Label set selector.
     * @return Label count for the selected set.
     */
    static constexpr size_t getLabelCount(LabelType labelType) {
        switch (labelType) {
        case LabelType::ImageNet:
            return std::tuple_size_v<decltype(theImageNetLabels)>;
        case LabelType::Coco:
            return std::tuple_size_v<decltype(theCocoLbels)>;
        };
        return 0U;
    }

    /**
     * @brief Returns label text by index.
     * @param labelType Label set selector.
     * @param index Label index.
     * @return Label string, or empty string when index is out of range.
     */
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

} // namespace pek

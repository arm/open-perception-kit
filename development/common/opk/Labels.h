/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <array>
#include <string>

/** @brief ImageNet class labels (index-aligned, including background). */
extern const std::array<const char *, 1001U> imageNetLabels;
/** @brief COCO class labels (index-aligned). */
extern const std::array<const char *, 80U> cocoLabels;

namespace opk::resources {

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
            return std::tuple_size_v<decltype(imageNetLabels)>;
        case LabelType::Coco:
            return std::tuple_size_v<decltype(cocoLabels)>;
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
            return imageNetLabels[index];
        case LabelType::Coco:
            return cocoLabels[index];
        };
        return "?";
    }
};

} // namespace opk::resources

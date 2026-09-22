/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

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

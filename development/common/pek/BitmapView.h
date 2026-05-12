/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Shape.h"
#include "pek/Types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace pek {

//
// non-owning view around pixel data (of different formats)
//
struct BitmapView {

    BitmapView() {}

    BitmapView(
        uint8_t *data, pek::DataKind dataKind, size_t width, size_t height, size_t stride = 0) {
        this->data = data;
        this->dataKind = dataKind;
        this->width = width;
        this->height = height;
        this->stride = stride;
    }

    uint8_t *data = nullptr;
    size_t width = 0, height = 0, stride = 0;
    pek::DataKind dataKind = pek::DataKind::Unknown;
};

} // namespace pek

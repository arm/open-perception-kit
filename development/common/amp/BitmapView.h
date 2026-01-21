#pragma once

#include "amp/Shape.h"
#include "amp/Types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace amp {

//
// non-owning view around pixel data (of different format)
//
struct BitmapView {

    BitmapView(uint8_t *data, size_t width, size_t height, size_t stride = 0) {
        this->data = data;
        this->width = width;
        this->height = height;
        this->stride = stride;
    }

    uint8_t *data = nullptr;
    size_t width = 0, height = 0, stride = 0;
};

} // namespace amp

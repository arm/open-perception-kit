/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <vector>

#include <assert.h>
#include <stdint.h>

namespace amp {

struct Bitmap {

    enum class Type { Uint8, Uint32 };

    Bitmap() {}

    Bitmap(Type type, size_t width, size_t height) {
        realloc(type, width, height);
    }

    void set8(size_t x, size_t y, uint8_t value) {
        assert(type == Type::Uint8);
        pixels[y * width + x] = value;
    }

    uint8_t get8(size_t x, size_t y) const {
        assert(type == Type::Uint8);
        return pixels[y * width + x];
    }

    void realloc(Type type, size_t width, size_t height) {
        size_t size = width * height;
        if (type == Type::Uint32)
            size *= 4;
        pixels.resize(size);

        this->type = type;
        this->width = width;
        this->height = height;
    }

    Type getType() const {
        return type;
    }
    size_t getWidth() const {
        return width;
    }
    size_t getHeight() const {
        return height;
    }
    uint8_t *getMutableData() {
        return pixels.data();
    }
    const uint8_t *getData() const {
        return pixels.data();
    }
    bool empty() const {
        return pixels.empty();
    }

  private:
    Type type = Type::Uint8;
    std::vector<uint8_t> pixels;
    size_t width = 0, height = 0;
};

} // namespace amp

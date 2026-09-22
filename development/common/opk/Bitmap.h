/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <span>
#include <vector>

#include <assert.h>
#include <stdint.h>

namespace opk {

/**
 * @brief Owning bitmap buffer used for persisted/generated pixel maps.
 */
struct Bitmap {

    /**
     * @brief Backing storage element format.
     */
    enum class Type { Uint8, Uint32 };

    /** @brief Constructs an empty bitmap. */
    Bitmap() = default;

    /**
     * @brief Constructs and allocates a bitmap of the given type and dimensions.
     */
    Bitmap(Type type, size_t width, size_t height) {
        realloc(type, width, height);
    }

    /**
     * @brief Writes one 8-bit pixel value.
     */
    void set8(size_t x, size_t y, uint8_t value) {
        assert(type == Type::Uint8);
        pixels[y * width + x] = value;
    }

    /**
     * @brief Reads one 8-bit pixel value.
     */
    uint8_t get8(size_t x, size_t y) const {
        assert(type == Type::Uint8);
        return pixels[y * width + x];
    }

    /**
     * @brief Reallocates bitmap storage and updates metadata.
     */
    void realloc(Type type, size_t width, size_t height) {
        size_t size = width * height;
        if (type == Type::Uint32)
            size *= 4;
        pixels.resize(size);

        this->type = type;
        this->width = width;
        this->height = height;
    }

    /** @brief Returns bitmap element format. */
    Type getType() const {
        return type;
    }
    /** @brief Returns bitmap width in pixels. */
    size_t getWidth() const {
        return width;
    }
    /** @brief Returns bitmap height in pixels. */
    size_t getHeight() const {
        return height;
    }
    /** @brief Returns mutable byte pointer to backing storage. */
    uint8_t *getMutableData() {
        return pixels.data();
    }
    /** @brief Returns immutable byte pointer to backing storage. */
    const uint8_t *getData() const {
        return pixels.data();
    }
    /** @brief Returns immutable view over backing storage bytes. */
    std::span<const uint8_t> getPixels() const {
        return pixels;
    }
    /** @brief Returns true when storage is empty. */
    bool empty() const {
        return pixels.empty();
    }

  private:
    Type type = Type::Uint8;
    std::vector<uint8_t> pixels;
    size_t width = 0, height = 0;
};

} // namespace opk

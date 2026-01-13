#pragma once

namespace amp {

struct Map8 {
    std::vector<uint8_t> map;
    size_t width = 0, height = 0;

    inline uint8_t &at(size_t x, size_t y) {
        assert(x < width);
        assert(x < width);
        return map.data()[width * y + x];
    }
};

} // namespace amp

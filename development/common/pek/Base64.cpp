/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/Base64.h"

#include <array>

namespace pek {

std::string base64Encode(std::span<const uint8_t> data) {
    static constexpr std::array<char, 65> table =
        std::to_array("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
    std::string out;
    out.reserve(((data.size() + 2U) / 3U) * 4U);

    size_t index = 0;
    while (index + 3U <= data.size()) {
        const uint32_t value = (uint32_t(data[index]) << 16U) | (uint32_t(data[index + 1U]) << 8U) |
                               uint32_t(data[index + 2U]);
        out.push_back(table[(value >> 18U) & 0x3FU]);
        out.push_back(table[(value >> 12U) & 0x3FU]);
        out.push_back(table[(value >> 6U) & 0x3FU]);
        out.push_back(table[value & 0x3FU]);
        index += 3U;
    }

    if (const size_t remaining = data.size() - index; remaining == 1U) {
        const uint32_t value = uint32_t(data[index]) << 16U;
        out.push_back(table[(value >> 18U) & 0x3FU]);
        out.push_back(table[(value >> 12U) & 0x3FU]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2U) {
        const uint32_t value = (uint32_t(data[index]) << 16U) | (uint32_t(data[index + 1U]) << 8U);
        out.push_back(table[(value >> 18U) & 0x3FU]);
        out.push_back(table[(value >> 12U) & 0x3FU]);
        out.push_back(table[(value >> 6U) & 0x3FU]);
        out.push_back('=');
    }

    return out;
}

} // namespace pek

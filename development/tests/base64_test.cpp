/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "opk/Base64.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

TEST(Base64, EncodesRfc4648Vectors) {
    const std::array<std::pair<std::string, std::string>, 7> cases{{
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    }};

    for (const auto &[input, expected] : cases) {
        const auto bytes =
            std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(input.data()), input.size());
        EXPECT_EQ(opk::base64Encode(bytes), expected) << input;
    }
}

TEST(Base64, EncodesBinaryBytes) {
    const std::vector<uint8_t> input{0x00U, 0xFFU, 0x10U, 0x80U};
    EXPECT_EQ(opk::base64Encode(input), "AP8QgA==");
}

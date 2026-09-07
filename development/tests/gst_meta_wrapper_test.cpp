/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "gst/GstMetaWrapper.h"

#include <array>
#include <span>
#include <string_view>

namespace {

struct TestMetaTraits {
    using Payload = int;

    static const std::string_view api_name() {
        return "com_arm_pek_test_meta_API_v1";
    }

    static const std::string_view meta_name() {
        return "com_arm_pek_test_meta";
    }

    static const std::span<const gchar *> tags() {
        static std::array<const gchar *, 1> tags = {nullptr};
        return tags;
    }
};

using TestMeta = pek::Meta<TestMetaTraits>;

} // namespace

TEST(GstMetaWrapper, NullSourceDoesNotAddDestinationMeta) {
    gst_init(nullptr, nullptr);
    GstBuffer *destination = gst_buffer_new();
    ASSERT_NE(destination, nullptr);

    EXPECT_FALSE(TestMeta::info()->transform_func(destination, nullptr, nullptr, 0, nullptr));
    EXPECT_EQ(TestMeta::get(destination), nullptr);

    gst_buffer_unref(destination);
}

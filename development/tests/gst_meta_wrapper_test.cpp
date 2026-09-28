/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include "gst/GstMetaWrapper.h"

#include <array>
#include <span>
#include <string_view>

namespace {

struct TestMetaTraits {
    using Payload = int;

    static const std::string_view api_name() {
        return "com_arm_opk_test_meta_API_v1";
    }

    static const std::string_view meta_name() {
        return "com_arm_opk_test_meta";
    }

    static const std::span<const gchar *> tags() {
        static std::array<const gchar *, 1> tags = {nullptr};
        return tags;
    }
};

using TestMeta = opk::Meta<TestMetaTraits>;

} // namespace

TEST(GstMetaWrapper, NullSourceDoesNotAddDestinationMeta) {
    gst_init(nullptr, nullptr);
    GstBuffer *destination = gst_buffer_new();
    ASSERT_NE(destination, nullptr);

    EXPECT_FALSE(TestMeta::info()->transform_func(destination, nullptr, nullptr, 0, nullptr));
    EXPECT_EQ(TestMeta::get(destination), nullptr);

    gst_buffer_unref(destination);
}

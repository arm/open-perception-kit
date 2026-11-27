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

    static Payload clone(const Payload &payload) {
        return payload;
    }

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

template <class T>
concept HasMutableGet = requires(GstBuffer *buffer) { T::get(buffer); };
template <class T>
concept HasCachedRead = requires(GstBuffer *buffer) { T::readCached(buffer); };
template <class T>
concept AcceptsSharedPayload = requires(
    GstBuffer *buffer, std::shared_ptr<typename T::Payload> payload) { T::add(buffer, payload); };

static_assert(!HasMutableGet<TestMeta>);
static_assert(!HasCachedRead<TestMeta>);
static_assert(!AcceptsSharedPayload<TestMeta>);
static_assert(std::is_same_v<decltype(TestMeta::read(nullptr)), std::shared_ptr<const int>>);

} // namespace

TEST(GstMetaWrapper, NullSourceDoesNotAddDestinationMeta) {
    gst_init(nullptr, nullptr);
    GstBuffer *destination = gst_buffer_new();
    ASSERT_NE(destination, nullptr);

    EXPECT_FALSE(TestMeta::info()->transform_func(destination, nullptr, nullptr, 0, nullptr));
    EXPECT_FALSE(TestMeta::has(destination));

    gst_buffer_unref(destination);
}

TEST(GstMetaWrapper, CacheDisabledMetadataHasNoCacheStorageAndStillCopiesOnWrite) {
    gst_init(nullptr, nullptr);
    struct UncachedLayout {
        GstMeta meta;
        std::shared_ptr<int> payload;
    };
    EXPECT_EQ(TestMeta::info()->size, sizeof(UncachedLayout));

    g_autoptr(GstBuffer) original = gst_buffer_new();
    ASSERT_NE(TestMeta::add(original, 7), nullptr);
    g_autoptr(GstBuffer) copy = gst_buffer_copy(original);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(TestMeta::read(copy), TestMeta::read(original));
    const auto result = TestMeta::mutate<bool>(copy, [](int &value) {
        value = 9;
        return true;
    });
    ASSERT_TRUE(std::holds_alternative<bool>(result));
    EXPECT_EQ(*TestMeta::read(original), 7);
    EXPECT_EQ(*TestMeta::read(copy), 9);
    EXPECT_TRUE(TestMeta::remove(copy));
    EXPECT_FALSE(TestMeta::has(copy));
}

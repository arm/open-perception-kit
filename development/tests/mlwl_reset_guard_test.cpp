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
#include "V3dResetGuard.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

namespace {

TEST(MlwlResetGuard, AcceptsUnchangedGenerationIncludingZero) {
    for (const uint64_t generation :
         {uint64_t{0}, uint64_t{7}, std::numeric_limits<uint64_t>::max()}) {
        opk::mlwl::V3dResetGuard guard([generation] { return generation; });
        EXPECT_NO_THROW(guard.check());
        EXPECT_NO_THROW(guard.check());
    }
}

TEST(MlwlResetGuard, RejectsChangedGeneration) {
    uint64_t generation = 5;
    opk::mlwl::V3dResetGuard guard([&] { return generation; });
    EXPECT_NO_THROW(guard.check());
    ++generation;
    EXPECT_THROW(guard.check(), opk::mlwl::V3dReset);
}

TEST(MlwlResetGuard, RejectsWrappedCounter) {
    uint64_t generation = std::numeric_limits<uint64_t>::max();
    opk::mlwl::V3dResetGuard guard([&] { return generation; });
    generation = 0;
    EXPECT_THROW(guard.check(), opk::mlwl::V3dReset);
}

TEST(MlwlResetGuard, QueryFailureIsNotTreatedAsNoReset) {
    bool readable = true;
    opk::mlwl::V3dResetGuard guard([&]() -> uint64_t {
        if (!readable) {
            throw std::runtime_error("counter read failed");
        }
        return 2;
    });
    readable = false;
    EXPECT_THROW(guard.check(), std::runtime_error);
}

TEST(MlwlResetGuard, RequiresReadableInitialCounter) {
    EXPECT_THROW((opk::mlwl::V3dResetGuard(
                     []() -> uint64_t { throw std::runtime_error("unsupported counter"); })),
                 std::runtime_error);
}

} // namespace

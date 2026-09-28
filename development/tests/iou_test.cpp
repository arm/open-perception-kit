/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "algo/IoU.h"

namespace {

struct Box {
    float x;
    float y;
    float width;
    float height;
};

} // namespace

TEST(IoUUtil, IdenticalBoxesReturnOne) {
    const Box a{10.0f, 10.0f, 20.0f, 20.0f};
    const Box b{10.0f, 10.0f, 20.0f, 20.0f};

    const float iou = opk::algo::computeIoU(a, b);
    EXPECT_NEAR(iou, 1.0f, 1e-6f);
}

TEST(IoUUtil, PartialOverlapComputesExpectedValue) {
    const Box a{0.0f, 0.0f, 10.0f, 10.0f};
    const Box b{5.0f, 5.0f, 10.0f, 10.0f};

    const float iou = opk::algo::computeIoU(a, b);
    EXPECT_NEAR(iou, 25.0f / 175.0f, 1e-6f);
}

TEST(IoUUtil, DisjointBoxesReturnZero) {
    const Box a{0.0f, 0.0f, 10.0f, 10.0f};
    const Box b{20.0f, 20.0f, 5.0f, 5.0f};

    const float iou = opk::algo::computeIoU(a, b);
    EXPECT_FLOAT_EQ(iou, 0.0f);
}

TEST(IoUUtil, EdgeTouchingBoxesReturnZero) {
    const Box a{0.0f, 0.0f, 10.0f, 10.0f};
    const Box b{10.0f, 0.0f, 4.0f, 4.0f};

    const float iou = opk::algo::computeIoU(a, b);
    EXPECT_FLOAT_EQ(iou, 0.0f);
}

TEST(IoUUtil, ContainedBoxComputesExpectedRatio) {
    const Box outer{0.0f, 0.0f, 10.0f, 10.0f};
    const Box inner{2.0f, 2.0f, 4.0f, 4.0f};

    const float iou = opk::algo::computeIoU(outer, inner);
    EXPECT_NEAR(iou, 16.0f / 100.0f, 1e-6f);
}

TEST(IoUUtil, DegenerateBoxesReturnZero) {
    const Box a{0.0f, 0.0f, 0.0f, 10.0f};
    const Box b{0.0f, 0.0f, 10.0f, 10.0f};

    const float iou = opk::algo::computeIoU(a, b);
    EXPECT_FLOAT_EQ(iou, 0.0f);
}

TEST(IoUUtil, ScalarOverloadMatchesBoxOverload) {
    const Box a{1.0f, 2.0f, 3.0f, 4.0f};
    const Box b{2.0f, 3.0f, 5.0f, 6.0f};

    const float boxIoU = opk::algo::computeIoU(a, b);
    const float scalarIoU =
        opk::algo::computeIoU(a.x, a.y, a.width, a.height, b.x, b.y, b.width, b.height);

    EXPECT_NEAR(boxIoU, scalarIoU, 1e-6f);
}

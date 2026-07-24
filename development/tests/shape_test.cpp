/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "pek/JsonSchemas.h"
#include "pek/Shape.h"

TEST(Shape, RejectsExcessiveRankWithoutChangingValue) {
    pek::Shape shape(2, 3);
    const std::vector<int64_t> excessiveDimensions(pek::Shape::MaxRank + 1, 1);

    EXPECT_FALSE(shape.setFrom(excessiveDimensions));
    EXPECT_EQ(shape, pek::Shape(2, 3));
}

TEST(Shape, AcceptsMaximumRank) {
    pek::Shape shape;
    const std::vector<size_t> maximumDimensions(pek::Shape::MaxRank, 1);

    ASSERT_TRUE(shape.setFrom(maximumDimensions));
    EXPECT_EQ(shape.rank, pek::Shape::MaxRank);
    EXPECT_EQ(shape.dims[pek::Shape::MaxRank - 1], 1);
}

TEST(Shape, AcceptsDynamicAndPositiveDimensions) {
    pek::Shape shape;

    ASSERT_TRUE(shape.setFrom(std::vector<int64_t>{-1, 2}));
    EXPECT_EQ(shape, pek::Shape(-1, 2));
}

TEST(Shape, RejectsUnrepresentableDimensionsWithoutChangingValue) {
    const std::vector<std::vector<int64_t>> invalidDimensions = {
        {0},
        {-2},
        {static_cast<int64_t>(std::numeric_limits<int>::max()) + 1},
    };

    for (const auto &dimensions : invalidDimensions) {
        pek::Shape shape(2, 3);
        EXPECT_FALSE(shape.setFrom(dimensions));
        EXPECT_EQ(shape, pek::Shape(2, 3));
    }

    pek::Shape shape(2, 3);
    const std::vector<size_t> overflowingUnsignedDimension = {
        static_cast<size_t>(std::numeric_limits<int>::max()) + 1,
    };
    EXPECT_FALSE(shape.setFrom(overflowingUnsignedDimension));
    EXPECT_EQ(shape, pek::Shape(2, 3));
}

TEST(Shape, AppliesDynamicDimensionsAfterCompleteValidation) {
    pek::Shape shape(-1, 3);

    ASSERT_TRUE(shape.applyDimensionsForDynamic(pek::Shape(2, 3)));
    EXPECT_EQ(shape, pek::Shape(2, 3));
}

TEST(Shape, DynamicApplicationFailureDoesNotPartiallyModifyValue) {
    {
        pek::Shape shape(-1, 3);
        EXPECT_FALSE(shape.applyDimensionsForDynamic(pek::Shape(2, 4)));
        EXPECT_EQ(shape, pek::Shape(-1, 3));
    }

    {
        pek::Shape shape;
        ASSERT_TRUE(shape.setFrom(std::vector<int>{-1, -1}));
        const pek::Shape original = shape;
        pek::Shape invalidSource(2, 3);
        invalidSource.dims[1] = 0;

        EXPECT_FALSE(shape.applyDimensionsForDynamic(invalidSource));
        EXPECT_EQ(shape, original);
    }
}

TEST(ShapeJson, EmptyArrayRepresentsUnsetShape) {
    pek::Shape shape(2, 3);

    nlohmann::json::array().get_to(shape);

    EXPECT_EQ(shape.rank, 0U);
    EXPECT_TRUE(shape.isInvalid());
}

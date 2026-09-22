/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace opk {

/**
 * @brief Fixed-capacity tensor shape descriptor.
 */
struct Shape {

    /**
     * @brief Constructs an empty shape.
     */
    explicit Shape() {}

    /**
     * @brief Constructs a shape from up to 8 dimension values.
     * @tparam Args Integer-like dimension argument types.
     * @param initDims Dimension values in order.
     */
    template <typename... Args> explicit Shape(Args... initDims) {
        static_assert(sizeof...(initDims) <= 8, "Max 8 dimensions");

        int tmp[] = {initDims...};
        rank = 0;

        for (int i = 0; i < (int)sizeof...(initDims); ++i) {
            dims[i] = tmp[i];
            if (tmp[i] > 0)
                rank = i + 1;
        }
    }

    /**
     * @brief Compares two shapes for exact rank and dimension equality.
     * @param o Shape to compare with.
     * @return true when both shapes are identical.
     */
    bool operator==(const Shape &o) const {
        if (rank != o.rank)
            return false;
        for (size_t i = 0; i < rank; i++)
            if (dims[i] != o.dims[i])
                return false;
        return true;
    }

    /**
     * @brief Returns the product of all dimensions.
     * @return Total value count, or 0 for an empty shape.
     */
    size_t getFullValueCount() const {
        if (!rank)
            return 0;
        size_t c = 1;
        for (size_t i = 0; i < rank; i++)
            c *= this->dims[i];
        return c;
    }

    /**
     * @brief Sets shape dimensions from a size_t vector.
     * @param dimensions Dimension values. Maximum supported size is 8.
     * @return true when the dimensions fit, false when the size exceeds 8.
     */
    bool setFrom(const std::vector<size_t> &dimensions) {
        if (dimensions.size() > 8)
            return false;
        this->rank = dimensions.size();
        for (size_t i = 0; i < dimensions.size(); i++)
            this->dims[i] = static_cast<int>(dimensions[i]);
        return true;
    }

    /**
     * @brief Sets shape dimensions from an int64_t vector.
     * @param dimensions Dimension values. Maximum supported size is 8.
     * @return true when the dimensions fit, false when the size exceeds 8.
     */
    bool setFrom(const std::vector<int64_t> &dimensions) {
        if (dimensions.size() > 8)
            return false;
        this->rank = dimensions.size();
        for (size_t i = 0; i < dimensions.size(); i++)
            this->dims[i] = static_cast<int>(dimensions[i]);
        return true;
    }

    /**
     * @brief Stored dimension values.
     *
     * Values greater than 0 are static dimensions. A value of -1 marks a dynamic
     * dimension placeholder.
     */
    int dims[8] = {0};

    /**
     * @brief Number of active entries in dims.
     */
    size_t rank = 0;

    /**
     * @brief Returns a compact textual representation.
     * @return String in the form "[d0,d1,...]" or "[empty]".
     */
    std::string toString() const {
        if (rank == 0)
            return "[empty]";
        std::string ret;
        for (size_t i = 0; i < rank; i++) {
            if (false == ret.empty())
                ret += ",";
            ret += std::to_string(dims[i]);
        }
        return std::string("[") + ret + "]";
    }

    /**
     * @brief Checks whether the shape is invalid.
     * @return true when rank is 0 or a dimension is 0 or below -1.
     */
    bool isInvalid() const {
        for (size_t i = 0; i < rank; i++)
            if (dims[i] == 0 || dims[i] < -1)
                return true;
        return (rank == 0);
    }

    /**
     * @brief Checks whether the shape is valid.
     * @return true when isInvalid() is false.
     */
    bool isValid() const {
        return !isInvalid();
    }

    /**
     * @brief Returns true when at least one dimension is dynamic.
     * @return true if any dimension equals -1.
     */
    bool hasDynamicDimension() const {
        for (size_t i = 0; i < rank; i++)
            if (dims[i] == -1)
                return true;
        return false;
    }

    /**
     * @brief Applies dimensions from another shape to fill dynamic entries.
     *
     * Static dimensions must match exactly. Dynamic dimensions (-1) are replaced
     * with the corresponding values from @p other.
     *
     * @param other Source shape.
     * @return true if application succeeds, false on incompatibility.
     */
    bool applyDimensionsForDynamic(const Shape &other) {
        if (false == hasDynamicDimension())
            return false;
        if (rank != other.rank)
            return false;
        for (size_t i = 0; i < rank; i++) {
            if (dims[i] == -1) {
                if (other.dims[i] <= 0)
                    return false;
                dims[i] = other.dims[i];
            } else {
                if (dims[i] != other.dims[i])
                    return false;
            }
        }
        return true;
    }
};
} // namespace opk

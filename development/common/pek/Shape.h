/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <type_traits>

namespace pek {

/**
 * @brief Fixed-capacity tensor shape descriptor.
 */
struct Shape {

    /**
     * @brief Constructs an empty shape.
     */
    explicit Shape() {}

    /**
     * @brief Constructs a shape from up to MaxRank dimension values.
     * @tparam Args Integer-like dimension argument types.
     * @param initDims Dimension values in order.
     */
    template <typename... Args> explicit Shape(Args... initDims) {
        static_assert(sizeof...(initDims) <= MaxRank, "Too many dimensions");

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
     * @brief Sets shape dimensions from an integer container.
     * @tparam DimsT Container with size() and indexed integral dimension values.
     * @param dims Dimension values. Each value must be -1 or a positive representable int.
     * @return true when the non-empty dimensions fit, false without modifying the shape otherwise.
     */
    template <typename DimsT> bool setFrom(const DimsT &dims) {
        using DimT = std::remove_cv_t<std::remove_reference_t<decltype(dims[0])>>;
        static_assert(std::is_integral_v<DimT>, "Shape dimensions must be integral");

        if (dims.size() == 0 || dims.size() > MaxRank)
            return false;

        for (size_t i = 0; i < dims.size(); ++i) {
            const DimT dimension = dims[i];
            if (dimension == 0 || dimension > std::numeric_limits<int>::max())
                return false;
            if constexpr (std::is_signed_v<DimT>) {
                if (dimension < -1)
                    return false;
            }
        }

        for (size_t i = 0; i < dims.size(); ++i)
            this->dims[i] = static_cast<int>(dims[i]);
        this->rank = dims.size();
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
     * @brief Maximum number of dimensions representable by this type.
     */
    static constexpr size_t MaxRank = sizeof(dims) / sizeof(dims[0]);

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
        if (rank == 0 || rank > MaxRank || rank != other.rank)
            return false;

        bool hasDynamic = false;
        for (size_t i = 0; i < rank; i++) {
            if (dims[i] == -1) {
                hasDynamic = true;
                if (other.dims[i] <= 0)
                    return false;
            } else if (dims[i] <= 0 || dims[i] != other.dims[i])
                return false;
        }

        if (!hasDynamic)
            return false;

        for (size_t i = 0; i < rank; i++)
            if (dims[i] == -1)
                dims[i] = other.dims[i];
        return true;
    }
};
} // namespace pek

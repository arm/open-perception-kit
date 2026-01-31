#pragma once

#include "assert.h"

#include <cstdint>
#include <string>
#include <vector>

namespace amp {

struct Shape {

    explicit Shape() {}

    template <typename... Args> explicit Shape(Args... dims) {
        static_assert(sizeof...(dims) <= 8, "Max 8 dimensions");

        int tmp[] = {dims...};
        dimensionCount = 0;

        for (int i = 0; i < (int)sizeof...(dims); ++i) {
            valueCount[i] = tmp[i];
            if (tmp[i] > 0)
                dimensionCount = i + 1;
        }
    }

    bool operator==(const Shape &o) const {
        if (dimensionCount != o.dimensionCount)
            return false;
        for (size_t i = 0; i < dimensionCount; i++)
            if (valueCount[i] != o.valueCount[i])
                return false;
        return true;
    }

    size_t getFullValueCount() const {
        if (!dimensionCount)
            return 0;
        size_t c = 1;
        for (size_t i = 0; i < dimensionCount; i++)
            c *= this->valueCount[i];
        return c;
    }

    void setFrom(const std::vector<size_t> &dims) {
        assert(dims.size() <= 8);
        this->dimensionCount = dims.size();
        for (size_t i = 0; i < dims.size() && i < 8; i++)
            this->valueCount[i] = dims[i];
    }

    void setFrom(const std::vector<int64_t> &dims) {
        assert(dims.size() <= 8);
        this->dimensionCount = dims.size();
        for (size_t i = 0; i < dims.size() && i < 8; i++)
            this->valueCount[i] = dims[i];
    }

    int valueCount[8] = {0};
    size_t dimensionCount = 0;

    std::string toString() const {
        if (dimensionCount == 0)
            return "[empty]";
        std::string ret;
        for (size_t i = 0; i < dimensionCount; i++) {
            if (false == ret.empty())
                ret += ",";
            ret += std::to_string(valueCount[i]);
        }
        return std::string("[") + ret + "]";
    }

    bool isInvalid() const {
        for (size_t i = 0; i < dimensionCount; i++)
            if (valueCount[i] == 0 || valueCount[i] < -1)
                return true;
        return (dimensionCount == 0);
    }

    bool isValid() const {
        return !isInvalid();
    }

    // dynamic dimension is marked as -1
    bool hasDynamicDimension() const {
        for (size_t i = 0; i < dimensionCount; i++)
            if (valueCount[i] == -1)
                return true;
        return false;
    }

    // try to apply a shape to another shape
    // - static dimensions must match
    // - dynamic dimensions are overwritten
    bool applyDimensionsForDynamic(const Shape &other) {
        if (false == hasDynamicDimension())
            return false;
        if (dimensionCount != other.dimensionCount)
            return false;
        for (size_t i = 0; i < dimensionCount; i++) {
            if (valueCount[i] == -1) {
                assert(other.valueCount[i] > 0);
                valueCount[i] = other.valueCount[i];
            } else {
                if (valueCount[i] != other.valueCount[i])
                    return false;
            }
        }
        return true;
    }
};
} // namespace amp

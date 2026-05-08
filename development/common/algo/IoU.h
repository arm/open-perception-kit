/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <algorithm>
#include <concepts>
#include <type_traits>

namespace pek::algo {

template <typename T>
concept Arithmetic = std::is_arithmetic_v<std::remove_cvref_t<T>>;

template <typename Box>
concept AxisAlignedBox = requires(Box box) {
    { box.x } -> Arithmetic;
    { box.y } -> Arithmetic;
    { box.width } -> Arithmetic;
    { box.height } -> Arithmetic;
};

template <Arithmetic T> T computeIoU(T ax, T ay, T aw, T ah, T bx, T by, T bw, T bh) {
    const T x1 = std::max(ax, bx);
    const T y1 = std::max(ay, by);
    const T x2 = std::min(ax + aw, bx + bw);
    const T y2 = std::min(ay + ah, by + bh);

    const T intersectionWidth = std::max(T(0), x2 - x1);
    const T intersectionHeight = std::max(T(0), y2 - y1);
    const T intersectionArea = intersectionWidth * intersectionHeight;

    if (intersectionArea <= T(0)) {
        return T(0);
    }

    const T areaA = aw * ah;
    const T areaB = bw * bh;
    const T unionArea = areaA + areaB - intersectionArea;

    if (unionArea <= T(0)) {
        return T(0);
    }

    return intersectionArea / unionArea;
}

template <AxisAlignedBox Box> auto computeIoU(const Box &a, const Box &b) {
    using CoordType = std::common_type_t<decltype(a.x),
                                         decltype(a.y),
                                         decltype(a.width),
                                         decltype(a.height),
                                         decltype(b.x),
                                         decltype(b.y),
                                         decltype(b.width),
                                         decltype(b.height)>;
    return computeIoU<CoordType>(static_cast<CoordType>(a.x),
                                 static_cast<CoordType>(a.y),
                                 static_cast<CoordType>(a.width),
                                 static_cast<CoordType>(a.height),
                                 static_cast<CoordType>(b.x),
                                 static_cast<CoordType>(b.y),
                                 static_cast<CoordType>(b.width),
                                 static_cast<CoordType>(b.height));
}

} // namespace pek::algo

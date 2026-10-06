/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "preproc/CpuImageKernels.h"
#include "preproc/CpuImageKernelsI420.h"
#include "preproc/CpuImageKernelsNv12.h"
#include "preproc/CpuImageKernelsYuy2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include <vector>

using opk::DataKind;
using opk::Dtype;
using opk::ImageOpDesc;
using opk::PixelRect;
using opk::RawImagePixelFormat;
using opk::Sampling;
using opk::YuvColorMatrix;
using opk::YuvRange;
using Kernel = bool (*)(const ImageOpDesc &, const ImageOpDesc &, Sampling);

struct Rgb {
    float r;
    float g;
    float b;
};

struct Fixture {
    RawImagePixelFormat format;
    size_t width = 5;
    size_t height = 3;
    size_t stride0 = 0;
    size_t stride1 = 0;
    size_t stride2 = 0;
    std::vector<uint8_t> p0;
    std::vector<uint8_t> p1;
    std::vector<uint8_t> p2;
};

struct Variant {
    const char *name;
    DataKind kind;
    Dtype type;
    Kernel full;
    Kernel rect;
};

struct ConversionCase {
    PixelRect sourceRect;
    size_t dstWidth;
    size_t dstHeight;
    bool letterbox = false;
    bool callRect = false;
    YuvColorMatrix matrix = YuvColorMatrix::Bt601;
    YuvRange range = YuvRange::Limited;
};

#define V(name, kind, type, full, rect)                                                            \
    Variant {                                                                                      \
        name, kind, type, full, rect                                                               \
    }

std::vector<Variant> variants(RawImagePixelFormat format) {
    using I = opk::stdop::preproc::ImageOps;
    using P420 = opk::stdop::preproc::ImageOpsI420;
    using PNv12 = opk::stdop::preproc::ImageOpsNv12;
    using PYuy2 = opk::stdop::preproc::ImageOpsYuy2;
    switch (format) {
    case RawImagePixelFormat::Bgra:
        return {
            V("rgb-chw-f32",
              DataKind::ImageRgbChw,
              Dtype::Float32,
              I::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw,
              I::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw),
            V("rgb-chw-f16",
              DataKind::ImageRgbChw,
              Dtype::Float16,
              I::StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Chw,
              I::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw),
            V("rgb-hwc-u8",
              DataKind::ImageRgbHwc,
              Dtype::Uint8,
              I::StretchBlit_Bgra8_Hwc_Full_Rgb8_Full_Hwc,
              I::StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc),
            V("rgb-hwc-f32",
              DataKind::ImageRgbHwc,
              Dtype::Float32,
              I::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Hwc,
              I::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc),
            V("rgb-hwc-f16",
              DataKind::ImageRgbHwc,
              Dtype::Float16,
              I::StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Hwc,
              I::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc),
            V("gray-u8",
              DataKind::ImageGray,
              Dtype::Uint8,
              I::StretchBlit_Bgra8_Hwc_Full_Gray8_Full,
              I::StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect),
            V("gray-f32",
              DataKind::ImageGray,
              Dtype::Float32,
              I::StretchBlit_Bgra8_Hwc_Full_Grayf32_Full,
              I::StretchBlit_Bgra8_Hwc_Rect_Grayf32_Rect),
        };
    case RawImagePixelFormat::Rgb:
        return {
            V("rgb-chw-f32",
              DataKind::ImageRgbChw,
              Dtype::Float32,
              I::StretchBlit_Rgb8_Hwc_Full_Rgbf32_Full_Chw,
              I::StretchBlit_Rgb8_Hwc_Rect_Rgbf32_Rect_Chw),
            V("rgb-chw-f16",
              DataKind::ImageRgbChw,
              Dtype::Float16,
              I::StretchBlit_Rgb8_Hwc_Full_Rgbf16_Full_Chw,
              I::StretchBlit_Rgb8_Hwc_Rect_Rgbf16_Rect_Chw),
            V("rgb-hwc-u8",
              DataKind::ImageRgbHwc,
              Dtype::Uint8,
              I::StretchBlit_Rgb8_Hwc_Full_Rgb8_Full_Hwc,
              I::StretchBlit_Rgb8_Hwc_Rect_Rgb8_Rect_Hwc),
            V("rgb-hwc-f32",
              DataKind::ImageRgbHwc,
              Dtype::Float32,
              I::StretchBlit_Rgb8_Hwc_Full_Rgbf32_Full_Hwc,
              I::StretchBlit_Rgb8_Hwc_Rect_Rgbf32_Rect_Hwc),
            V("rgb-hwc-f16",
              DataKind::ImageRgbHwc,
              Dtype::Float16,
              I::StretchBlit_Rgb8_Hwc_Full_Rgbf16_Full_Hwc,
              I::StretchBlit_Rgb8_Hwc_Rect_Rgbf16_Rect_Hwc),
            V("gray-u8",
              DataKind::ImageGray,
              Dtype::Uint8,
              I::StretchBlit_Rgb8_Hwc_Full_Gray8_Full,
              I::StretchBlit_Rgb8_Hwc_Rect_Gray8_Rect),
            V("gray-f32",
              DataKind::ImageGray,
              Dtype::Float32,
              I::StretchBlit_Rgb8_Hwc_Full_Grayf32_Full,
              I::StretchBlit_Rgb8_Hwc_Rect_Grayf32_Rect),
        };
    case RawImagePixelFormat::I420:
        return {
            V("rgb-chw-f32",
              DataKind::ImageRgbChw,
              Dtype::Float32,
              P420::StretchBlit_I420_Full_Rgbf32_Full_Chw,
              P420::StretchBlit_I420_Rect_Rgbf32_Rect_Chw),
            V("rgb-chw-f16",
              DataKind::ImageRgbChw,
              Dtype::Float16,
              P420::StretchBlit_I420_Full_Rgbf16_Full_Chw,
              P420::StretchBlit_I420_Rect_Rgbf16_Rect_Chw),
            V("rgb-hwc-u8",
              DataKind::ImageRgbHwc,
              Dtype::Uint8,
              P420::StretchBlit_I420_Full_Rgb8_Full_Hwc,
              P420::StretchBlit_I420_Rect_Rgb8_Rect_Hwc),
            V("rgb-hwc-f32",
              DataKind::ImageRgbHwc,
              Dtype::Float32,
              P420::StretchBlit_I420_Full_Rgbf32_Full_Hwc,
              P420::StretchBlit_I420_Rect_Rgbf32_Rect_Hwc),
            V("rgb-hwc-f16",
              DataKind::ImageRgbHwc,
              Dtype::Float16,
              P420::StretchBlit_I420_Full_Rgbf16_Full_Hwc,
              P420::StretchBlit_I420_Rect_Rgbf16_Rect_Hwc),
            V("gray-u8",
              DataKind::ImageGray,
              Dtype::Uint8,
              P420::StretchBlit_I420_Full_Gray8_Full,
              P420::StretchBlit_I420_Rect_Gray8_Rect),
            V("gray-f32",
              DataKind::ImageGray,
              Dtype::Float32,
              P420::StretchBlit_I420_Full_Grayf32_Full,
              P420::StretchBlit_I420_Rect_Grayf32_Rect),
        };
    case RawImagePixelFormat::Nv12:
        return {
            V("rgb-chw-f32",
              DataKind::ImageRgbChw,
              Dtype::Float32,
              PNv12::StretchBlit_Nv12_Full_Rgbf32_Full_Chw,
              PNv12::StretchBlit_Nv12_Rect_Rgbf32_Rect_Chw),
            V("rgb-chw-f16",
              DataKind::ImageRgbChw,
              Dtype::Float16,
              PNv12::StretchBlit_Nv12_Full_Rgbf16_Full_Chw,
              PNv12::StretchBlit_Nv12_Rect_Rgbf16_Rect_Chw),
            V("rgb-hwc-u8",
              DataKind::ImageRgbHwc,
              Dtype::Uint8,
              PNv12::StretchBlit_Nv12_Full_Rgb8_Full_Hwc,
              PNv12::StretchBlit_Nv12_Rect_Rgb8_Rect_Hwc),
            V("rgb-hwc-f32",
              DataKind::ImageRgbHwc,
              Dtype::Float32,
              PNv12::StretchBlit_Nv12_Full_Rgbf32_Full_Hwc,
              PNv12::StretchBlit_Nv12_Rect_Rgbf32_Rect_Hwc),
            V("rgb-hwc-f16",
              DataKind::ImageRgbHwc,
              Dtype::Float16,
              PNv12::StretchBlit_Nv12_Full_Rgbf16_Full_Hwc,
              PNv12::StretchBlit_Nv12_Rect_Rgbf16_Rect_Hwc),
            V("gray-u8",
              DataKind::ImageGray,
              Dtype::Uint8,
              PNv12::StretchBlit_Nv12_Full_Gray8_Full,
              PNv12::StretchBlit_Nv12_Rect_Gray8_Rect),
            V("gray-f32",
              DataKind::ImageGray,
              Dtype::Float32,
              PNv12::StretchBlit_Nv12_Full_Grayf32_Full,
              PNv12::StretchBlit_Nv12_Rect_Grayf32_Rect),
        };
    case RawImagePixelFormat::Yuy2:
        return {
            V("rgb-chw-f32",
              DataKind::ImageRgbChw,
              Dtype::Float32,
              PYuy2::StretchBlit_Yuy2_Full_Rgbf32_Full_Chw,
              PYuy2::StretchBlit_Yuy2_Rect_Rgbf32_Rect_Chw),
            V("rgb-chw-f16",
              DataKind::ImageRgbChw,
              Dtype::Float16,
              PYuy2::StretchBlit_Yuy2_Full_Rgbf16_Full_Chw,
              PYuy2::StretchBlit_Yuy2_Rect_Rgbf16_Rect_Chw),
            V("rgb-hwc-u8",
              DataKind::ImageRgbHwc,
              Dtype::Uint8,
              PYuy2::StretchBlit_Yuy2_Full_Rgb8_Full_Hwc,
              PYuy2::StretchBlit_Yuy2_Rect_Rgb8_Rect_Hwc),
            V("rgb-hwc-f32",
              DataKind::ImageRgbHwc,
              Dtype::Float32,
              PYuy2::StretchBlit_Yuy2_Full_Rgbf32_Full_Hwc,
              PYuy2::StretchBlit_Yuy2_Rect_Rgbf32_Rect_Hwc),
            V("rgb-hwc-f16",
              DataKind::ImageRgbHwc,
              Dtype::Float16,
              PYuy2::StretchBlit_Yuy2_Full_Rgbf16_Full_Hwc,
              PYuy2::StretchBlit_Yuy2_Rect_Rgbf16_Rect_Hwc),
            V("gray-u8",
              DataKind::ImageGray,
              Dtype::Uint8,
              PYuy2::StretchBlit_Yuy2_Full_Gray8_Full,
              PYuy2::StretchBlit_Yuy2_Rect_Gray8_Rect),
            V("gray-f32",
              DataKind::ImageGray,
              Dtype::Float32,
              PYuy2::StretchBlit_Yuy2_Full_Grayf32_Full,
              PYuy2::StretchBlit_Yuy2_Rect_Grayf32_Rect),
        };
    default:
        return {};
    }
}

#undef V

void fillPackedRgbFixture(Fixture &f) {
    const size_t bpp = f.format == RawImagePixelFormat::Bgra ? 4 : 3;
    f.stride0 = f.width * bpp + 3;
    f.p0.assign(f.stride0 * f.height, 0xcd);
    for (size_t y = 0; y < f.height; ++y) {
        for (size_t x = 0; x < f.width; ++x) {
            const auto r = static_cast<uint8_t>(13 + x * 41 + y * 17);
            const auto g = static_cast<uint8_t>(237 - x * 29 - y * 11);
            const auto b = static_cast<uint8_t>(5 + x * 19 + y * 53);
            auto *p = f.p0.data() + y * f.stride0 + x * bpp;
            if (f.format == RawImagePixelFormat::Bgra) {
                p[0] = b;
                p[1] = g;
                p[2] = r;
                p[3] = static_cast<uint8_t>(91 + x);
            } else {
                p[0] = r;
                p[1] = g;
                p[2] = b;
            }
        }
    }
}

Fixture makeFixture(RawImagePixelFormat format) {
    Fixture f{};
    f.format = format;
    if (format == RawImagePixelFormat::Bgra || format == RawImagePixelFormat::Rgb) {
        fillPackedRgbFixture(f);
        return f;
    }

    f.stride0 = f.width + 2;
    f.p0.assign(f.stride0 * f.height, 0xcd);
    constexpr std::array<uint8_t, 15> ys{
        0, 16, 64, 128, 235, 255, 32, 96, 160, 224, 12, 48, 112, 192, 244};
    for (size_t y = 0; y < f.height; ++y) {
        for (size_t x = 0; x < f.width; ++x) {
            f.p0[y * f.stride0 + x] = ys[y * f.width + x];
        }
    }

    const size_t cw = (f.width + 1) / 2;
    const size_t ch = (f.height + 1) / 2;
    constexpr std::array<uint8_t, 6> us{0, 64, 128, 192, 240, 255};
    constexpr std::array<uint8_t, 6> vs{255, 224, 160, 96, 32, 0};
    if (format == RawImagePixelFormat::I420) {
        f.stride1 = cw + 2;
        f.stride2 = cw + 3;
        f.p1.assign(f.stride1 * ch, 0xcd);
        f.p2.assign(f.stride2 * ch, 0xcd);
        for (size_t y = 0; y < ch; ++y) {
            for (size_t x = 0; x < cw; ++x) {
                f.p1[y * f.stride1 + x] = us[y * cw + x];
                f.p2[y * f.stride2 + x] = vs[y * cw + x];
            }
        }
    } else if (format == RawImagePixelFormat::Nv12) {
        f.stride1 = cw * 2 + 2;
        f.p1.assign(f.stride1 * ch, 0xcd);
        for (size_t y = 0; y < ch; ++y) {
            for (size_t x = 0; x < cw; ++x) {
                f.p1[y * f.stride1 + x * 2] = us[y * cw + x];
                f.p1[y * f.stride1 + x * 2 + 1] = vs[y * cw + x];
            }
        }
    } else {
        f.stride0 = cw * 4 + 3;
        f.p0.assign(f.stride0 * f.height, 0xcd);
        for (size_t y = 0; y < f.height; ++y) {
            for (size_t pair = 0; pair < cw; ++pair) {
                auto *p = f.p0.data() + y * f.stride0 + pair * 4;
                const size_t x0 = pair * 2;
                const size_t x1 = std::min(x0 + 1, f.width - 1);
                p[0] = ys[y * f.width + x0];
                p[1] = us[(y % ch) * cw + pair];
                p[2] = ys[y * f.width + x1];
                p[3] = vs[(y % ch) * cw + pair];
            }
        }
    }
    return f;
}

ImageOpDesc
sourceDesc(const Fixture &f, const PixelRect &rect, YuvColorMatrix matrix, YuvRange range) {
    ImageOpDesc src;
    src.surfaceWidth = f.width;
    src.surfaceHeight = f.height;
    src.rect = rect;
    src.format = f.format;
    src.type = Dtype::Uint8;
    src.mean = {0.11f, 0.22f, 0.33f, 0.0f};
    src.std = {0.71f, 0.83f, 0.59f, 1.0f};
    src.yuvMatrix = matrix;
    src.yuvRange = range;
    src.planes[0] = {f.p0.data(), nullptr, f.p0.size(), f.stride0};
    src.planeCount = 1;
    if (f.format == RawImagePixelFormat::I420) {
        src.planes[1] = {f.p1.data(), nullptr, f.p1.size(), f.stride1};
        src.planes[2] = {f.p2.data(), nullptr, f.p2.size(), f.stride2};
        src.planeCount = 3;
    } else if (f.format == RawImagePixelFormat::Nv12) {
        src.planes[1] = {f.p1.data(), nullptr, f.p1.size(), f.stride1};
        src.planeCount = 2;
    }
    return src;
}

Rgb yuvToRgb(uint8_t y, uint8_t u, uint8_t v, YuvColorMatrix matrix, YuvRange range) {
    float kr = 0.299f;
    float kb = 0.114f;
    if (matrix == YuvColorMatrix::Bt709) {
        kr = 0.2126f;
        kb = 0.0722f;
    }
    if (matrix == YuvColorMatrix::Bt2020) {
        kr = 0.2627f;
        kb = 0.0593f;
    }
    const float kg = 1.0f - kr - kb;
    const float yf =
        range == YuvRange::Full ? y / 255.0f : (static_cast<float>(y) - 16.0f) / 219.0f;
    const float chromaScale = range == YuvRange::Full ? 255.0f : 224.0f;
    const float cb = (static_cast<float>(u) - 128.0f) / chromaScale;
    const float cr = (static_cast<float>(v) - 128.0f) / chromaScale;
    return {
        std::clamp(yf + 2.0f * (1.0f - kr) * cr, 0.0f, 1.0f),
        std::clamp(
            yf - 2.0f * kb * (1.0f - kb) / kg * cb - 2.0f * kr * (1.0f - kr) / kg * cr, 0.0f, 1.0f),
        std::clamp(yf + 2.0f * (1.0f - kb) * cb, 0.0f, 1.0f),
    };
}

Rgb sample(const Fixture &f, size_t x, size_t y, YuvColorMatrix matrix, YuvRange range) {
    if (f.format == RawImagePixelFormat::Bgra) {
        const auto *p = f.p0.data() + y * f.stride0 + x * 4;
        return {p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f};
    }
    if (f.format == RawImagePixelFormat::Rgb) {
        const auto *p = f.p0.data() + y * f.stride0 + x * 3;
        return {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f};
    }
    uint8_t yy = 0;
    uint8_t u = 0;
    uint8_t v = 0;
    if (f.format == RawImagePixelFormat::I420) {
        yy = f.p0[y * f.stride0 + x];
        u = f.p1[(y / 2) * f.stride1 + x / 2];
        v = f.p2[(y / 2) * f.stride2 + x / 2];
    } else if (f.format == RawImagePixelFormat::Nv12) {
        yy = f.p0[y * f.stride0 + x];
        const size_t uv = (y / 2) * f.stride1 + (x / 2) * 2;
        u = f.p1[uv];
        v = f.p1[uv + 1];
    } else {
        const size_t pair = y * f.stride0 + (x / 2) * 4;
        yy = f.p0[pair + (x % 2 ? 2 : 0)];
        u = f.p0[pair + 1];
        v = f.p0[pair + 3];
    }
    return yuvToRgb(yy, u, v, matrix, range);
}

uint8_t byte(float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

uint8_t rawY(const Fixture &f, size_t x, size_t y) {
    if (f.format == RawImagePixelFormat::I420 || f.format == RawImagePixelFormat::Nv12) {
        return f.p0[y * f.stride0 + x];
    }
    const size_t pair = y * f.stride0 + (x / 2) * 4;
    return f.p0[pair + (x % 2 ? 2 : 0)];
}

bool isYuv(RawImagePixelFormat format) {
    using enum RawImagePixelFormat;
    return format == I420 || format == Nv12 || format == Yuy2;
}

std::array<float, 3>
expectedGray(Rgb rgb, const ImageOpDesc &src, Dtype type, bool useRawY, uint8_t y) {
    if (useRawY && type == Dtype::Uint8) {
        if (src.yuvRange == YuvRange::Full)
            return {static_cast<float>(y), 0, 0};
        const int limited = std::clamp(static_cast<int>(y) - 16, 0, 219);
        return {static_cast<float>((limited * 255 + 109) / 219), 0, 0};
    }
    if (useRawY) {
        const float gray = std::clamp(
            src.yuvRange == YuvRange::Full ? y / 255.0f : (static_cast<float>(y) - 16.0f) / 219.0f,
            0.0f,
            1.0f);
        return {(gray - src.mean.r) / src.std.r, 0, 0};
    }
    if (type == Dtype::Uint8) {
        const auto r = static_cast<uint16_t>(byte(rgb.r));
        const auto g = static_cast<uint16_t>(byte(rgb.g));
        const auto b = static_cast<uint16_t>(byte(rgb.b));
        return {static_cast<float>((77u * r + 150u * g + 29u * b + 128u) >> 8), 0, 0};
    }
    const float gray = 0.299f * rgb.r + 0.587f * rgb.g + 0.114f * rgb.b;
    return {(gray - src.mean.r) / src.std.r, 0, 0};
}

std::array<float, 3>
expectedValues(Rgb rgb, const ImageOpDesc &src, const Variant &variant, bool useRawY, uint8_t y) {
    if (variant.kind == DataKind::ImageGray)
        return expectedGray(rgb, src, variant.type, useRawY, y);
    if (variant.type == Dtype::Uint8) {
        return {static_cast<float>(byte(rgb.r)),
                static_cast<float>(byte(rgb.g)),
                static_cast<float>(byte(rgb.b))};
    }
    return {(rgb.r - src.mean.r) / src.std.r,
            (rgb.g - src.mean.g) / src.std.g,
            (rgb.b - src.mean.b) / src.std.b};
}

[[noreturn]] void fail(const std::string &message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

bool contains(const PixelRect &rect, size_t x, size_t y) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

std::array<float, 3> expectedPixel(const Fixture &fixture,
                                   const ImageOpDesc &src,
                                   const ImageOpDesc &dst,
                                   const Variant &variant,
                                   size_t x,
                                   size_t y) {
    const PixelRect inner = dst.keepAspectRatio
                                ? PixelRect{0, 1, 6, 4}
                                : PixelRect{0, 0, dst.surfaceWidth, dst.surfaceHeight};
    if (!contains(inner, x, y))
        return expectedValues({0.23f, 0.37f, 0.61f}, src, variant, false, 0);

    const size_t sx = src.rect.x + ((x - inner.x) * src.rect.width) / inner.width;
    const size_t sy = src.rect.y + ((y - inner.y) * src.rect.height) / inner.height;
    const bool useRawY = isYuv(fixture.format);
    return expectedValues(sample(fixture, sx, sy, src.yuvMatrix, src.yuvRange),
                          src,
                          variant,
                          useRawY,
                          useRawY ? rawY(fixture, sx, sy) : 0);
}

template <typename T> T outputValue(const std::vector<uint8_t> &output, size_t index) {
    T value;
    std::memcpy(&value, output.data() + index * sizeof(T), sizeof(T));
    return value;
}

float outputValue(const std::vector<uint8_t> &output, size_t index, Dtype type) {
    using enum Dtype;
    switch (type) {
    case Uint8:
        return output[index];
    case Float16:
        return static_cast<float>(outputValue<opk::Float16>(output, index));
    case Float32:
        return outputValue<float>(output, index);
    default:
        fail("unsupported output type");
    }
}

float tolerance(Dtype type) {
    using enum Dtype;
    switch (type) {
    case Uint8:
        return 0.0f;
    case Float16:
        return 0.002f;
    case Float32:
        return 0.00001f;
    default:
        fail("unsupported output type");
    }
}

size_t outputIndex(const ImageOpDesc &dst, size_t x, size_t y, size_t channel) {
    if (dst.kind == DataKind::ImageRgbChw)
        return channel * dst.surfaceWidth * dst.surfaceHeight + y * dst.surfaceWidth + x;
    return (y * dst.surfaceWidth + x) * dst.getChannelCount() + channel;
}

void checkValue(float actual,
                float expected,
                float allowedDifference,
                const std::string &label,
                size_t x,
                size_t y,
                size_t channel) {
    if (std::abs(actual - expected) <= allowedDifference)
        return;
    fail(std::format(
        "{} pixel={},{} channel={} actual={} expected={}", label, x, y, channel, actual, expected));
}

void checkOutput(const Fixture &fixture,
                 const ImageOpDesc &src,
                 const ImageOpDesc &dst,
                 const Variant &variant,
                 const std::vector<uint8_t> &output,
                 const std::string &label) {
    const auto allowedDifference = tolerance(variant.type);
    for (size_t y = 0; y < dst.surfaceHeight; ++y) {
        for (size_t x = 0; x < dst.surfaceWidth; ++x) {
            const auto expected = expectedPixel(fixture, src, dst, variant, x, y);
            for (size_t channel = 0; channel < dst.getChannelCount(); ++channel) {
                const auto actual =
                    outputValue(output, outputIndex(dst, x, y, channel), variant.type);
                checkValue(actual, expected[channel], allowedDifference, label, x, y, channel);
            }
        }
    }
}

int runCase(const Fixture &fixture, const Variant &variant, const ConversionCase &test) {
    auto src = sourceDesc(fixture, test.sourceRect, test.matrix, test.range);
    ImageOpDesc dst;
    dst.surfaceWidth = test.dstWidth;
    dst.surfaceHeight = test.dstHeight;
    dst.rect = {0, 0, test.dstWidth, test.dstHeight};
    dst.kind = variant.kind;
    dst.type = variant.type;
    dst.keepAspectRatio = test.letterbox;
    dst.letterboxRed = 0.23f;
    dst.letterboxGreen = 0.37f;
    dst.letterboxBlue = 0.61f;
    const size_t byteCount = test.dstWidth * test.dstHeight * dst.getChannelCount() *
                             opk::getValueTypeByteSize(variant.type);
    std::vector<uint8_t> output(byteCount, 0xa5);
    dst.planes[0] = {nullptr, output.data(), output.size(), 0};
    dst.planeCount = 1;
    const auto label = std::format("{}/{}", static_cast<int>(fixture.format), variant.name);
    if (const auto kernel = test.callRect ? variant.rect : variant.full;
        !kernel(src, dst, Sampling::Nearest)) {
        fail(label + " kernel returned false");
    }
    checkOutput(fixture, src, dst, variant, output, label);
    return 1;
}

int runStandardCases(const Fixture &fixture, const std::vector<Variant> &formatVariants) {
    const std::array tests{
        ConversionCase{{0, 0, 5, 3}, 5, 3, false, false},
        ConversionCase{{0, 0, 5, 3}, 5, 3, false, true},
        ConversionCase{{1, 0, 4, 3}, 3, 2, false, false},
        ConversionCase{{0, 0, 5, 3}, 6, 6, true, false},
    };
    int cases = 0;
    for (const auto &variant : formatVariants) {
        for (const auto &test : tests)
            cases += runCase(fixture, variant, test);
    }
    return cases;
}

int runYuvMatrixCases(const Fixture &fixture, const std::vector<Variant> &formatVariants) {
    if (!isYuv(fixture.format))
        return 0;

    const auto variant = std::ranges::find_if(formatVariants, [](const Variant &candidate) {
        return candidate.kind == DataKind::ImageRgbHwc && candidate.type == Dtype::Float32;
    });
    if (variant == formatVariants.end())
        fail("missing YUV test variant");

    int cases = 0;
    for (const auto matrix :
         {YuvColorMatrix::Bt601, YuvColorMatrix::Bt709, YuvColorMatrix::Bt2020}) {
        for (const auto range : {YuvRange::Limited, YuvRange::Full}) {
            cases += runCase(fixture, *variant, {{0, 0, 5, 3}, 5, 3, false, false, matrix, range});
        }
    }
    return cases;
}

int rejectUndersizedPlane(const Fixture &fixture, const Variant &variant) {
    auto invalid = sourceDesc(fixture, {0, 0, 5, 3}, YuvColorMatrix::Bt601, YuvRange::Limited);
    invalid.planes[0].byteCount = 1;
    ImageOpDesc dst;
    dst.surfaceWidth = 5;
    dst.surfaceHeight = 3;
    dst.rect = {0, 0, 5, 3};
    dst.kind = variant.kind;
    dst.type = variant.type;
    dst.planeCount = 1;
    std::vector<uint8_t> output(5 * 3 * 3 * sizeof(float));
    dst.planes[0] = {nullptr, output.data(), output.size(), 0};
    if (variant.full(invalid, dst, Sampling::Nearest))
        fail("undersized plane accepted");
    return 1;
}

int rejectConflictingPlaneAccess(const Fixture &fixture, const Variant &variant) {
    auto src = sourceDesc(fixture, {0, 0, 5, 3}, YuvColorMatrix::Bt601, YuvRange::Limited);
    ImageOpDesc dst;
    dst.surfaceWidth = 5;
    dst.surfaceHeight = 3;
    dst.rect = {0, 0, 5, 3};
    dst.kind = variant.kind;
    dst.type = variant.type;
    dst.planeCount = 1;
    std::vector<uint8_t> output(5 * 3 * 3 * sizeof(float));
    dst.planes[0] = {nullptr, output.data(), output.size(), 0};

    src.planes[0].mutableData = output.data();
    if (variant.full(src, dst, Sampling::Nearest))
        fail("writable source plane accepted");

    src.planes[0].mutableData = nullptr;
    dst.planes[0].data = output.data();
    if (variant.full(src, dst, Sampling::Nearest))
        fail("readable destination plane accepted");
    return 2;
}

int main() {
    using enum RawImagePixelFormat;
    const std::array formats{
        Bgra,
        Rgb,
        I420,
        Nv12,
        Yuy2,
    };
    int cases = 0;
    for (const auto format : formats) {
        const auto fixture = makeFixture(format);
        const auto formatVariants = variants(format);
        cases += runStandardCases(fixture, formatVariants);
        cases += runYuvMatrixCases(fixture, formatVariants);
        cases += rejectUndersizedPlane(fixture, formatVariants.front());
        cases += rejectConflictingPlaneAccess(fixture, formatVariants.front());
    }
    std::cout << "PASS conversion-cases=" << cases << '\n';
}

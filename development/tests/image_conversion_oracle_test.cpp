/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "preproc/CpuImageKernels.h"
#include "preproc/CpuImageKernelsI420.h"
#include "preproc/CpuImageKernelsNv12.h"
#include "preproc/CpuImageKernelsYuy2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using pek::DataKind;
using pek::Dtype;
using pek::Float16;
using pek::ImageOpDesc;
using pek::PixelRect;
using pek::RawImagePixelFormat;
using pek::Sampling;
using pek::YuvColorMatrix;
using pek::YuvRange;
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

#define V(name, kind, type, full, rect)                                                            \
    Variant {                                                                                      \
        name, kind, type, full, rect                                                               \
    }

std::vector<Variant> variants(RawImagePixelFormat format) {
    using I = pek::stdop::preproc::ImageOps;
    using P420 = pek::stdop::preproc::ImageOpsI420;
    using PNv12 = pek::stdop::preproc::ImageOpsNv12;
    using PYuy2 = pek::stdop::preproc::ImageOpsYuy2;
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

Fixture makeFixture(RawImagePixelFormat format) {
    Fixture f{};
    f.format = format;
    if (format == RawImagePixelFormat::Bgra || format == RawImagePixelFormat::Rgb) {
        const size_t bpp = format == RawImagePixelFormat::Bgra ? 4 : 3;
        f.stride0 = f.width * bpp + 3;
        f.p0.assign(f.stride0 * f.height, 0xcd);
        for (size_t y = 0; y < f.height; ++y) {
            for (size_t x = 0; x < f.width; ++x) {
                const uint8_t r = static_cast<uint8_t>(13 + x * 41 + y * 17);
                const uint8_t g = static_cast<uint8_t>(237 - x * 29 - y * 11);
                const uint8_t b = static_cast<uint8_t>(5 + x * 19 + y * 53);
                auto *p = f.p0.data() + y * f.stride0 + x * bpp;
                if (format == RawImagePixelFormat::Bgra) {
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

ImageOpDesc sourceDesc(const Fixture &f, PixelRect rect, YuvColorMatrix matrix, YuvRange range) {
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
    float kr = 0.299f, kb = 0.114f;
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
    uint8_t yy = 0, u = 0, v = 0;
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

size_t channels(DataKind kind) {
    return kind == DataKind::ImageGray ? 1 : 3;
}

size_t typeBytes(Dtype type) {
    return type == Dtype::Float32 ? 4 : type == Dtype::Float16 ? 2 : 1;
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
    return format == RawImagePixelFormat::I420 || format == RawImagePixelFormat::Nv12 ||
           format == RawImagePixelFormat::Yuy2;
}

std::array<float, 3> expectedValues(
    Rgb rgb, const ImageOpDesc &src, const Variant &variant, bool useRawY = false, uint8_t y = 0) {
    if (variant.kind == DataKind::ImageGray) {
        if (useRawY) {
            if (variant.type == Dtype::Uint8) {
                if (src.yuvRange == YuvRange::Full) {
                    return {static_cast<float>(y), 0, 0};
                }
                const int limited = std::clamp(static_cast<int>(y) - 16, 0, 219);
                return {static_cast<float>((limited * 255 + 109) / 219), 0, 0};
            }
            const float gray = std::clamp(src.yuvRange == YuvRange::Full
                                              ? y / 255.0f
                                              : (static_cast<float>(y) - 16.0f) / 219.0f,
                                          0.0f,
                                          1.0f);
            return {(gray - src.mean.r) / src.std.r, 0, 0};
        }
        if (variant.type == Dtype::Uint8) {
            const auto r = static_cast<uint16_t>(byte(rgb.r));
            const auto g = static_cast<uint16_t>(byte(rgb.g));
            const auto b = static_cast<uint16_t>(byte(rgb.b));
            return {static_cast<float>((77u * r + 150u * g + 29u * b + 128u) >> 8), 0, 0};
        }
        const float gray = 0.299f * rgb.r + 0.587f * rgb.g + 0.114f * rgb.b;
        return {(gray - src.mean.r) / src.std.r, 0, 0};
    }
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

void checkOutput(const Fixture &fixture,
                 const ImageOpDesc &src,
                 const Variant &variant,
                 const std::vector<uint8_t> &output,
                 size_t dstWidth,
                 size_t dstHeight,
                 bool letterbox,
                 const std::string &label) {
    const PixelRect inner =
        letterbox ? PixelRect{0, 1, 6, 4} : PixelRect{0, 0, dstWidth, dstHeight};
    for (size_t y = 0; y < dstHeight; ++y) {
        for (size_t x = 0; x < dstWidth; ++x) {
            Rgb rgb{0.23f, 0.37f, 0.61f};
            bool fromSource = false;
            size_t sx = 0, sy = 0;
            if (!letterbox || (x >= inner.x && x < inner.x + inner.width && y >= inner.y &&
                               y < inner.y + inner.height)) {
                const auto &mapRect = letterbox ? inner : PixelRect{0, 0, dstWidth, dstHeight};
                sx = src.rect.x + ((x - mapRect.x) * src.rect.width) / mapRect.width;
                sy = src.rect.y + ((y - mapRect.y) * src.rect.height) / mapRect.height;
                rgb = sample(fixture, sx, sy, src.yuvMatrix, src.yuvRange);
                fromSource = true;
            }
            const auto expected = expectedValues(rgb,
                                                 src,
                                                 variant,
                                                 fromSource && isYuv(fixture.format),
                                                 fromSource ? rawY(fixture, sx, sy) : 0);
            for (size_t c = 0; c < channels(variant.kind); ++c) {
                const size_t index = variant.kind == DataKind::ImageRgbChw
                                         ? c * dstWidth * dstHeight + y * dstWidth + x
                                         : (y * dstWidth + x) * channels(variant.kind) + c;
                float actual = 0.0f;
                if (variant.type == Dtype::Uint8) {
                    actual = output[index];
                } else if (variant.type == Dtype::Float32) {
                    actual = reinterpret_cast<const float *>(output.data())[index];
                } else {
                    actual =
                        static_cast<float>(reinterpret_cast<const Float16 *>(output.data())[index]);
                }
                const float tolerance = variant.type == Dtype::Uint8     ? 0.0f
                                        : variant.type == Dtype::Float16 ? 0.002f
                                                                         : 0.00001f;
                if (std::abs(actual - expected[c]) > tolerance) {
                    fail(label + " pixel=" + std::to_string(x) + "," + std::to_string(y) +
                         " channel=" + std::to_string(c) + " actual=" + std::to_string(actual) +
                         " expected=" + std::to_string(expected[c]));
                }
            }
        }
    }
}

int runCase(const Fixture &fixture,
            const Variant &variant,
            PixelRect sourceRect,
            size_t dstWidth,
            size_t dstHeight,
            bool letterbox,
            bool callRect,
            YuvColorMatrix matrix = YuvColorMatrix::Bt601,
            YuvRange range = YuvRange::Limited) {
    auto src = sourceDesc(fixture, sourceRect, matrix, range);
    ImageOpDesc dst;
    dst.surfaceWidth = dstWidth;
    dst.surfaceHeight = dstHeight;
    dst.rect = {0, 0, dstWidth, dstHeight};
    dst.kind = variant.kind;
    dst.type = variant.type;
    dst.keepAspectRatio = letterbox;
    dst.letterboxRed = 0.23f;
    dst.letterboxGreen = 0.37f;
    dst.letterboxBlue = 0.61f;
    const size_t byteCount =
        dstWidth * dstHeight * channels(variant.kind) * typeBytes(variant.type);
    std::vector<uint8_t> output(byteCount, 0xa5);
    dst.planes[0] = {nullptr, output.data(), output.size(), 0};
    dst.planeCount = 1;
    const std::string label = std::to_string(static_cast<int>(fixture.format)) + "/" + variant.name;
    if (!(callRect ? variant.rect(src, dst, Sampling::Nearest)
                   : variant.full(src, dst, Sampling::Nearest))) {
        fail(label + " kernel returned false");
    }
    checkOutput(fixture, src, variant, output, dstWidth, dstHeight, letterbox, label);
    return 1;
}

int main() {
    const std::array formats{
        RawImagePixelFormat::Bgra,
        RawImagePixelFormat::Rgb,
        RawImagePixelFormat::I420,
        RawImagePixelFormat::Nv12,
        RawImagePixelFormat::Yuy2,
    };
    int cases = 0;
    for (const auto format : formats) {
        const auto fixture = makeFixture(format);
        const auto vs = variants(format);
        for (const auto &variant : vs) {
            cases += runCase(fixture, variant, {0, 0, 5, 3}, 5, 3, false, false);
            cases += runCase(fixture, variant, {0, 0, 5, 3}, 5, 3, false, true);
            cases += runCase(fixture, variant, {1, 0, 4, 3}, 3, 2, false, false);
            cases += runCase(fixture, variant, {0, 0, 5, 3}, 6, 6, true, false);
        }
        if (format == RawImagePixelFormat::I420 || format == RawImagePixelFormat::Nv12 ||
            format == RawImagePixelFormat::Yuy2) {
            const auto it = std::find_if(vs.begin(), vs.end(), [](const Variant &v) {
                return v.kind == DataKind::ImageRgbHwc && v.type == Dtype::Float32;
            });
            for (const auto matrix :
                 {YuvColorMatrix::Bt601, YuvColorMatrix::Bt709, YuvColorMatrix::Bt2020}) {
                for (const auto range : {YuvRange::Limited, YuvRange::Full}) {
                    cases += runCase(fixture, *it, {0, 0, 5, 3}, 5, 3, false, false, matrix, range);
                }
            }
        }
        auto invalid = sourceDesc(fixture, {0, 0, 5, 3}, YuvColorMatrix::Bt601, YuvRange::Limited);
        invalid.planes[0].byteCount = 1;
        const auto &variant = vs.front();
        std::vector<float> out(5 * 3 * 3);
        ImageOpDesc dst;
        dst.surfaceWidth = 5;
        dst.surfaceHeight = 3;
        dst.rect = {0, 0, 5, 3};
        dst.kind = variant.kind;
        dst.type = variant.type;
        dst.planeCount = 1;
        dst.planes[0] = {
            nullptr, reinterpret_cast<uint8_t *>(out.data()), out.size() * sizeof(float), 0};
        if (variant.full(invalid, dst, Sampling::Nearest)) {
            fail("undersized plane accepted");
        }
        ++cases;
    }
    std::cout << "PASS conversion-cases=" << cases << '\n';
}

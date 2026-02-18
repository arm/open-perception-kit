#include "Tools.h"
#include "amp/Result.h"

#include "amp/String.h"
#include "fmt/core.h"
#include <cstdint>
#include <dlfcn.h>

using namespace amp;

std::string Tools::getLocalIp() {
    // HTTP server must bind to 0.0.0.0 (all interfaces) to accept connections
    // from host machine through Docker port mapping
    return "0.0.0.0";
}

void Tools::abort() {
    fmt::print("Amp is aborting the pipeline..\n");
    ::abort();
}

Result<DynamicLibraryHandle> Tools::DynamicLibraryOpen(const std::string &name) {

    std::vector<std::string> names;
    names.push_back(name);
    if (amp::utf8::endsWith(name, ".so") == false) {
        names.push_back(name + ".so");
    }

    std::string loadErrors;
    void *handle = nullptr;

    for (const auto &n : names) {
        dlerror();

        handle = dlopen(n.c_str(), RTLD_NOW);

        const char *err = dlerror();
        if (!err)
            err = "unknown error";
        loadErrors += fmt::format("ERROR while loading library [{}]: {}\n", n, err);

        if (handle)
            break;
    }

    if (!handle) {
        fmt::print("{}", loadErrors);
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::SystemFailure, loadErrors));
    }

    return (DynamicLibraryHandle)handle;
}

void Tools::DynamicLibraryClose(DynamicLibraryHandle handle) {
    if (!handle) {
        return;
    }
    dlclose((void *)handle);
}

Result<void *> Tools::DynamicLibraryGetSymbolRaw(DynamicLibraryHandle handle,
                                                 const std::string &symbolName) {
    if (!handle) {
        return tl::make_unexpected(
            AMP_ERROR(amp::ErrorFlag::SystemFailure, "Null dynamic library handle"));
    }

    dlerror(); // clear old errors

    void *sym = dlsym((void *)handle, symbolName.c_str());

    if (const char *err = dlerror(); err != nullptr) {
        std::string errorInfo = fmt::format("Symbol [{}] lookup error: {}", symbolName, err);
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::SystemFailure, errorInfo));
    }

    return sym;
}

std::atomic<uint64_t> amp::Uuid::counter_{1};

// ---

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#pragma GCC diagnostic pop

bool Tools::savePngFromBgra(const std::string &path, const uint8_t *bgra, int width, int height) {
    if (!bgra || width <= 0 || height <= 0)
        return false;

    std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4);

    for (int i = 0; i < width * height; ++i) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2]; // R
        rgba[i * 4 + 1] = bgra[i * 4 + 1]; // G
        rgba[i * 4 + 2] = bgra[i * 4 + 0]; // B
        rgba[i * 4 + 3] = bgra[i * 4 + 3]; // A
    }

    // stride = bytes per row
    const int stride = width * 4;
    return stbi_write_png(path.c_str(), width, height, 4, rgba.data(), stride) != 0;
}

static inline int clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(v, hi));
}

bool Tools::savePngCropFromBgra(const std::string &path,
                                const uint8_t *src_bgra,
                                int src_width,
                                int src_height,
                                int crop_x,
                                int crop_y,
                                int crop_w,
                                int crop_h,
                                int src_stride_bytes)

{
    if (!src_bgra || src_width <= 0 || src_height <= 0)
        return false;
    if (src_stride_bytes <= 0)
        src_stride_bytes = src_width * 4;

    // If crop dims are non-positive, fail early.
    if (crop_w <= 0 || crop_h <= 0)
        return false;

    // Clamp crop rectangle to image bounds.
    int x0 = clamp_int(crop_x, 0, src_width);
    int y0 = clamp_int(crop_y, 0, src_height);
    int x1 = clamp_int(crop_x + crop_w, 0, src_width);
    int y1 = clamp_int(crop_y + crop_h, 0, src_height);

    int out_w = x1 - x0;
    int out_h = y1 - y0;
    if (out_w <= 0 || out_h <= 0)
        return false;

    std::vector<uint8_t> rgba(size_t(out_w) * size_t(out_h) * 4);

    // Convert only the cropped region: BGRA -> RGBA
    for (int row = 0; row < out_h; ++row) {
        const uint8_t *src_row =
            src_bgra + (size_t(y0 + row) * size_t(src_stride_bytes)) + (size_t(x0) * 4);
        uint8_t *dst_row = rgba.data() + (size_t(row) * size_t(out_w) * 4);

        for (int col = 0; col < out_w; ++col) {
            const uint8_t b = src_row[col * 4 + 0];
            const uint8_t g = src_row[col * 4 + 1];
            const uint8_t r = src_row[col * 4 + 2];
            const uint8_t a = src_row[col * 4 + 3];

            dst_row[col * 4 + 0] = r;
            dst_row[col * 4 + 1] = g;
            dst_row[col * 4 + 2] = b;
            dst_row[col * 4 + 3] = a;
        }
    }

    const int out_stride = out_w * 4;
    return stbi_write_png(path.c_str(), out_w, out_h, 4, rgba.data(), out_stride) != 0;
}

/*bool Tools::savePngCropFromBgraF32(const std::string &path,
                                const uint8_t *src_bgra_f32,
                                int src_width,
                                int src_height,
                                int crop_x,
                                int crop_y,
                                int crop_w,
                                int crop_h,
                                int src_stride_bytes)

{
    float* src_bgra = (float*)src_bgra_f32;

    if (!src_bgra || src_width <= 0 || src_height <= 0)
        return false;
    if (src_stride_bytes <= 0)
        src_stride_bytes = src_width;

    // If crop dims are non-positive, fail early.
    if (crop_w <= 0 || crop_h <= 0)
        return false;

    // Clamp crop rectangle to image bounds.
    int x0 = clamp_int(crop_x, 0, src_width);
    int y0 = clamp_int(crop_y, 0, src_height);
    int x1 = clamp_int(crop_x + crop_w, 0, src_width);
    int y1 = clamp_int(crop_y + crop_h, 0, src_height);

    int out_w = x1 - x0;
    int out_h = y1 - y0;
    if (out_w <= 0 || out_h <= 0)
        return false;

    std::vector<uint8_t> rgba(size_t(out_w) * size_t(out_h) * 4);

    // Convert only the cropped region: BGRA -> RGBA
    for (int row = 0; row < out_h; ++row) {
        const float *src_row =
            src_bgra + (size_t(y0 + row) * size_t(src_stride_bytes)) + (size_t(x0) * 4);
        uint8_t *dst_row = rgba.data() + (size_t(row) * size_t(out_w) * 4);

        for (int col = 0; col < out_w; ++col) {
            const float b = src_row[col * 4 + 0];
            const float g = src_row[col * 4 + 1];
            const float r = src_row[col * 4 + 2];
            const float a = src_row[col * 4 + 3];

            dst_row[col * 4 + 0] = (uint8_t)(r * 255);
            dst_row[col * 4 + 1] = (uint8_t)(g * 255);
            dst_row[col * 4 + 2] = (uint8_t)(b * 255);
            dst_row[col * 4 + 3] = (uint8_t)(a * 255);
        }
    }

    const int out_stride = out_w * 4;
    return stbi_write_png(path.c_str(), out_w, out_h, 4, rgba.data(), out_stride) != 0;
}*/

bool Tools::savePngFromRgbChwF32(const std::string &path,
                                 const float *src_rgb_chw_f32,
                                 size_t width,
                                 size_t height) {
    if (!src_rgb_chw_f32 || width == 0 || height == 0)
        return false;

    std::vector<uint8_t> rgba(width * height * 4);

    auto to_u8 = [](float v) -> uint8_t {
        v = std::clamp(v, 0.0f, 1.0f);
        return static_cast<uint8_t>(std::lround(v * 255.0f));
    };

    const size_t plane = width * height;

    for (size_t i = 0; i < plane; ++i) {
        const float r = src_rgb_chw_f32[0 * plane + i];
        const float g = src_rgb_chw_f32[1 * plane + i];
        const float b = src_rgb_chw_f32[2 * plane + i];

        rgba[i * 4 + 0] = to_u8(r);
        rgba[i * 4 + 1] = to_u8(g);
        rgba[i * 4 + 2] = to_u8(b);
        rgba[i * 4 + 3] = 255;
    }

    return stbi_write_png(path.c_str(), (int)width, (int)height, 4, rgba.data(), (int)width * 4) !=
           0;
}

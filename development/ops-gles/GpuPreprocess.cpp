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

#include "GpuPreprocess.h"
#include "LinuxVideo.h"
#include "Log.h"
#include "PreprocessShader.h"

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl31.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace opk::gles {
namespace {
constexpr GLuint workgroupWidth{8};
constexpr GLuint workgroupHeight{8};
constexpr GLuint64 gpuWaitNanoseconds{1'000'000'000};
constexpr float byteMaximum{255.0F};
constexpr float limitedLumaOffset{16.0F};
constexpr float limitedLumaRange{219.0F};
constexpr float chromaCenter{128.0F};
constexpr float limitedChromaRange{224.0F};
constexpr float normalizationEpsilon{1e-12F};
constexpr float chromaCoefficientScale{2.0F};
constexpr GLbitfield tensorMapFlags{GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT |
                                    GL_MAP_COHERENT_BIT_EXT};

enum class SourceFormat : GLint { Rgb, Nv12, I420, Yuy2 };

void require(const bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void checkGl(const std::string_view operation) {
    const GLenum error{glGetError()};
    require(error == GL_NO_ERROR, fmt::format("{} failed (GL error {:#x})", operation, error));
}

bool hasExtension(const char *const extensions, const std::string_view name) {
    const std::string list{extensions ? extensions : ""};
    return (" " + list + " ").find(" " + std::string(name) + " ") != std::string::npos;
}

class CurrentContext {
  public:
    CurrentContext(const EGLDisplay display, const EGLContext context)
        : previousDisplay{eglGetCurrentDisplay()}, previousContext{eglGetCurrentContext()},
          previousDraw{eglGetCurrentSurface(EGL_DRAW)},
          previousRead{eglGetCurrentSurface(EGL_READ)}, previousApi{eglQueryAPI()},
          activeDisplay{display} {
        require(eglBindAPI(EGL_OPENGL_ES_API), "Cannot select the OpenGL ES API");
        if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
            eglBindAPI(previousApi);
            throw std::runtime_error("Cannot make the GPU preprocessing context current");
        }
    }
    ~CurrentContext() {
        eglMakeCurrent(activeDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglBindAPI(previousApi);
        if (previousDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(previousDisplay, previousDraw, previousRead, previousContext);
        }
    }
    CurrentContext(const CurrentContext &) = delete;
    CurrentContext &operator=(const CurrentContext &) = delete;

  private:
    EGLDisplay previousDisplay;
    EGLContext previousContext;
    EGLSurface previousDraw;
    EGLSurface previousRead;
    EGLenum previousApi;
    EGLDisplay activeDisplay;
};

GLuint compileProgram() {
    const std::string source{
        fmt::format("#version 310 es\n#define WORKGROUP_WIDTH {}\n#define WORKGROUP_HEIGHT {}\n",
                    workgroupWidth,
                    workgroupHeight) +
        preprocessShaderBody};
    const char *const text{source.c_str()};
    const GLuint shader{glCreateShader(GL_COMPUTE_SHADER)};
    require(shader != 0, "Cannot create preprocessing shader");
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint compiled{GL_FALSE};
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint size{0};
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string log(std::max(size, 1), '\0');
        glGetShaderInfoLog(shader, size, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("GPU preprocessing shader: " + log);
    }
    const GLuint program{glCreateProgram()};
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    GLint linked{GL_FALSE};
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(program);
        throw std::runtime_error("Cannot link GPU preprocessing shader");
    }
    return program;
}

struct GpuWork {
    bool complete{false};
    ~GpuWork() {
        // Cover errors after uploads or dispatch, before the normal completion fence.
        if (!complete) {
            glFinish();
        }
    }
};

EGLDisplay defaultDisplay() {
    static std::mutex displayMutex;
    static EGLDisplay display{EGL_NO_DISPLAY};
    const std::lock_guard lock{displayMutex};
    if (display == EGL_NO_DISPLAY) {
        require(hasExtension(eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS),
                             "EGL_MESA_platform_surfaceless"),
                "EGL has no offscreen display platform");
        const EGLDisplay candidate{
            eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)};
        require(eglInitialize(candidate, nullptr, nullptr), "Cannot initialize offscreen EGL");
        // The default display is process-owned. Terminating it at module teardown
        // could invalidate another component's contexts; release only our GL objects.
        display = candidate;
    }
    return display;
}

bool isSoftwareRenderer(std::string renderer) {
    std::transform(renderer.begin(),
                   renderer.end(),
                   renderer.begin(),
                   [](const unsigned char letter) { return std::tolower(letter); });
    constexpr std::array softwareRenderers{"llvmpipe",
                                           "softpipe",
                                           "swrast",
                                           "swiftshader",
                                           "software",
                                           "microsoft basic render",
                                           "warp"};
    return std::any_of(
        softwareRenderers.begin(), softwareRenderers.end(), [&renderer](const char *const name) {
            return renderer.find(name) != std::string::npos;
        });
}
} // namespace

struct GpuPreprocess::State {
    std::unique_ptr<LinuxDisplay> linuxDisplay;
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLContext context{EGL_NO_CONTEXT};
    GLuint program{0};
    GLuint tensor{0};
    struct UploadPlane {
        GLuint texture{0};
        uint32_t width{0};
        uint32_t height{0};
        GLint storage{0};
    };
    std::array<UploadPlane, MaxImagePlaneCount> uploadPlanes{};
    uint8_t *mapped{nullptr};
    std::vector<uint8_t> readback;
    size_t bytes{0};
    GLint maximumTextureSize{0};
    bool uploaded{false};
    std::string deviceIdentity;

    ~State() {
        if (context != EGL_NO_CONTEXT) {
            try {
                const CurrentContext current{display, context};
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, tensor);
                if (mapped) {
                    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                }
                for (const auto &plane : uploadPlanes) {
                    glDeleteTextures(1, &plane.texture);
                }
                glDeleteBuffers(1, &tensor);
                glDeleteProgram(program);
            } catch (const std::exception &) {
                // Context destruction also releases objects if making it current failed.
            }
            eglDestroyContext(display, context);
        }
    }

    void initialize(const size_t tensorBytes, const std::string &path, const bool requestReadback) {
        if (path.empty()) {
            display = defaultDisplay();
        } else {
            linuxDisplay = std::make_unique<LinuxDisplay>(path);
            display = linuxDisplay->get();
        }
        const char *const extensions{eglQueryString(display, EGL_EXTENSIONS)};
        require(hasExtension(extensions, "EGL_KHR_surfaceless_context"),
                "EGL lacks surfaceless contexts");
        constexpr EGLint requiredMajor{3};
        constexpr EGLint requiredMinor{1};
        const std::array contextAttributes{EGL_CONTEXT_MAJOR_VERSION,
                                           requiredMajor,
                                           EGL_CONTEXT_MINOR_VERSION,
                                           requiredMinor,
                                           EGL_NONE};
        const std::array configAttributes{
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
        EGLConfig config{nullptr};
        EGLint configCount{0};
        require(eglChooseConfig(display, configAttributes.data(), &config, 1, &configCount) &&
                    configCount,
                "Cannot choose an OpenGL ES configuration");
        const EGLenum previousApi{eglQueryAPI()};
        require(eglBindAPI(EGL_OPENGL_ES_API), "Cannot select the OpenGL ES API");
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes.data());
        eglBindAPI(previousApi);
        require(context != EGL_NO_CONTEXT, "Cannot create an OpenGL ES 3.1 context");
        const CurrentContext current{display, context};
        const std::string renderer{reinterpret_cast<const char *>(glGetString(GL_RENDERER))};
        require(!isSoftwareRenderer(renderer),
                "GPU preprocessing rejects software renderer " + renderer);
        deviceIdentity = fmt::format("gles-preprocess-v1/{}/{}/{}/{}",
                                     reinterpret_cast<const char *>(glGetString(GL_VENDOR)),
                                     renderer,
                                     reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                                     eglQueryString(display, EGL_VERSION));
        GLint64 maximumTensorBytes{0};
        glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maximumTensorBytes);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
        require(tensorBytes > 0 && tensorBytes <= static_cast<size_t>(maximumTensorBytes),
                "Model tensor exceeds the GPU storage limit");
        program = compileProgram();
        bytes = tensorBytes;
        glGenBuffers(1, &tensor);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, tensor);
        if (!requestReadback &&
            hasExtension(reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS)),
                         "GL_EXT_buffer_storage")) {
            const auto bufferStorage{reinterpret_cast<PFNGLBUFFERSTORAGEEXTPROC>(
                eglGetProcAddress("glBufferStorageEXT"))};
            if (bufferStorage) {
                bufferStorage(GL_SHADER_STORAGE_BUFFER,
                              static_cast<GLsizeiptr>(bytes),
                              nullptr,
                              tensorMapFlags);
                if (glGetError() == GL_NO_ERROR) {
                    mapped = static_cast<uint8_t *>(glMapBufferRange(GL_SHADER_STORAGE_BUFFER,
                                                                     0,
                                                                     static_cast<GLsizeiptr>(bytes),
                                                                     tensorMapFlags));
                }
            }
        }
        if (!mapped) {
            // Mutable storage supports drivers without persistent coherent mappings.
            // Its temporary map is copied before unmapping; inference never sees that map.
            while (glGetError() != GL_NO_ERROR) {
            }
            glDeleteBuffers(1, &tensor);
            glGenBuffers(1, &tensor);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, tensor);
            glBufferData(
                GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_DYNAMIC_READ);
            readback.resize(bytes);
        }
        checkGl("Allocate GPU tensor");
    }
};

GpuPreprocess::GpuPreprocess(const size_t tensorBytes,
                             const std::string &renderDevice,
                             const bool requestReadback) {
    std::vector<std::string> candidates{renderDevice.empty()
                                            ? LinuxDisplay::renderDevices()
                                            : std::vector<std::string>{renderDevice}};
    if (renderDevice.empty()) {
        candidates.emplace_back("");
    }
    std::string failure{"No hardware GPU is available"};
    for (const auto &path : candidates) {
        auto candidate{std::make_unique<State>()};
        try {
            candidate->initialize(tensorBytes, path, requestReadback);
            state = std::move(candidate);
            return;
        } catch (const std::exception &error) {
            failure = error.what();
        }
    }
    throw std::runtime_error(failure);
}

GpuPreprocess::~GpuPreprocess() = default;

std::span<uint8_t> GpuPreprocess::run(op::OpChainContext &context,
                                      const std::string &sourceName,
                                      const PixelRect &crop,
                                      const PixelRect &inner,
                                      const ModelInput &modelInput) {
    const auto *const source{context.getVideoFrame(sourceName)};
    require(source && !source->empty(), "GPU preprocessing requires a video frame");
    const auto &frame{*source};
    require(frame.width() <= static_cast<uint32_t>(state->maximumTextureSize) &&
                frame.height() <= static_cast<uint32_t>(state->maximumTextureSize),
            "Video dimensions exceed the GPU texture limit");
    size_t outputWidth{0}, outputHeight{0};
    require(modelInput.tryGetImageTensorSize(outputWidth, outputHeight),
            "Invalid model image shape");
    require(outputWidth <= static_cast<size_t>(state->maximumTextureSize) &&
                outputHeight <= static_cast<size_t>(state->maximumTextureSize),
            "Model dimensions exceed the GPU texture limit");
    require(!crop.isEmpty() && crop.x < frame.width() && crop.y < frame.height() &&
                crop.width <= frame.width() - crop.x && crop.height <= frame.height() - crop.y,
            "Image crop is outside the video frame");
    const CurrentContext current{state->display, state->context};
    std::unique_ptr<ImportedVideo> imported;
    GpuWork work;
    SourceFormat sourceFormat{SourceFormat::Rgb};
    size_t expectedPlanes{1};
    constexpr size_t nv12Planes{2};
    constexpr size_t i420Planes{3};
    switch (frame.format()) {
    case RawImagePixelFormat::Bgra:
        break;
    case RawImagePixelFormat::Rgb:
        break;
    case RawImagePixelFormat::Nv12:
        sourceFormat = SourceFormat::Nv12;
        expectedPlanes = nv12Planes;
        break;
    case RawImagePixelFormat::I420:
        sourceFormat = SourceFormat::I420;
        expectedPlanes = i420Planes;
        break;
    case RawImagePixelFormat::Yuy2:
        sourceFormat = SourceFormat::Yuy2;
        break;
    default:
        throw std::runtime_error("Unsupported GPU video format");
    }
    require(frame.planeCount() == expectedPlanes, "Unexpected video plane count");
    for (const auto &plane : frame.planes()) {
        require(!plane.sync().hasAcquireFence() && !plane.sync().hasReleaseFence(),
                "GPU preprocessing requires implicit source synchronization");
    }
    imported = ImportedVideo::tryImport(frame, state->display);
    state->uploaded = !imported;
    const mediaio::VideoFrame *readable{nullptr};
    constexpr size_t bgraBytesPerPixel{4};
    constexpr size_t rgbBytesPerPixel{3};
    constexpr size_t chromaPairBytes{2};
    constexpr uint32_t chromaSubsampling{2};
    if (imported) {
        imported->bind();
    } else {
        readable = context.getReadableVideoFrame(sourceName);
        require(readable, "Cannot map the video for GPU upload");
    }
    for (size_t index{0}; index < expectedPlanes; ++index) {
        const auto &plane{state->uploaded ? readable->planes()[index] : frame.planes()[index]};
        const auto width{index == 0 ? frame.width() : (frame.width() + 1) / chromaSubsampling};
        const auto height{index == 0 ? frame.height() : (frame.height() + 1) / chromaSubsampling};
        const size_t pixelBytes{frame.format() == RawImagePixelFormat::Bgra  ? bgraBytesPerPixel
                                : frame.format() == RawImagePixelFormat::Rgb ? rgbBytesPerPixel
                                : frame.format() == RawImagePixelFormat::Yuy2 ||
                                        (frame.format() == RawImagePixelFormat::Nv12 && index == 1)
                                    ? chromaPairBytes
                                    : 1};
        const size_t rowBytes{static_cast<size_t>(width) * pixelBytes};
        require(plane.strideBytes() >= rowBytes &&
                    plane.byteSize() >=
                        static_cast<size_t>(height - 1) * plane.strideBytes() + rowBytes,
                "Video plane is smaller than its layout");
        require(frame.format() != RawImagePixelFormat::Yuy2 || frame.width() % chromaPairBytes == 0,
                "YUY2 GPU input requires an even width");
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
        if (state->uploaded) {
            require(plane.data() && plane.canRead(), "Video plane is not CPU-readable");
            auto &texture{state->uploadPlanes[index]};
            if (texture.texture == 0) {
                glGenTextures(1, &texture.texture);
            }
            glBindTexture(GL_TEXTURE_2D, texture.texture);
            const auto layout{pixelBytes == bgraBytesPerPixel  ? GL_RGBA
                              : pixelBytes == rgbBytesPerPixel ? GL_RGB
                              : pixelBytes == chromaPairBytes  ? GL_RG
                                                               : GL_RED};
            const auto storage{pixelBytes == bgraBytesPerPixel  ? GL_RGBA8
                               : pixelBytes == rgbBytesPerPixel ? GL_RGB8
                               : pixelBytes == chromaPairBytes  ? GL_RG8
                                                                : GL_R8};
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            if (texture.width != width || texture.height != height || texture.storage != storage) {
                glTexImage2D(GL_TEXTURE_2D,
                             0,
                             storage,
                             static_cast<GLsizei>(width),
                             static_cast<GLsizei>(height),
                             0,
                             layout,
                             GL_UNSIGNED_BYTE,
                             nullptr);
                checkGl("Allocate upload texture");
                texture.width = width;
                texture.height = height;
                texture.storage = storage;
            }
            if (plane.strideBytes() % pixelBytes == 0) {
                glPixelStorei(GL_UNPACK_ROW_LENGTH,
                              static_cast<GLint>(plane.strideBytes() / pixelBytes));
                glTexSubImage2D(GL_TEXTURE_2D,
                                0,
                                0,
                                0,
                                static_cast<GLsizei>(width),
                                static_cast<GLsizei>(height),
                                layout,
                                GL_UNSIGNED_BYTE,
                                plane.data());
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            } else {
                const auto *const data{static_cast<const uint8_t *>(plane.data())};
                for (uint32_t row = 0; row < height; ++row) {
                    glTexSubImage2D(GL_TEXTURE_2D,
                                    0,
                                    0,
                                    static_cast<GLint>(row),
                                    static_cast<GLsizei>(width),
                                    1,
                                    layout,
                                    GL_UNSIGNED_BYTE,
                                    data + static_cast<size_t>(row) * plane.strideBytes());
                }
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        checkGl("Prepare video plane");
    }
    glUseProgram(state->program);
    glUniform1i(glGetUniformLocation(state->program, "swapRedBlue"),
                state->uploaded && frame.format() == RawImagePixelFormat::Bgra);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, state->tensor);
    glUniform2i(glGetUniformLocation(state->program, "outputSize"),
                static_cast<GLint>(outputWidth),
                static_cast<GLint>(outputHeight));
    glUniform4i(glGetUniformLocation(state->program, "crop"),
                static_cast<GLint>(crop.x),
                static_cast<GLint>(crop.y),
                static_cast<GLint>(crop.width),
                static_cast<GLint>(crop.height));
    glUniform4i(glGetUniformLocation(state->program, "inner"),
                static_cast<GLint>(inner.x),
                static_cast<GLint>(inner.y),
                static_cast<GLint>(inner.width),
                static_cast<GLint>(inner.height));
    glUniform1i(glGetUniformLocation(state->program, "sourceFormat"),
                static_cast<GLint>(sourceFormat));
    glUniform1i(glGetUniformLocation(state->program, "channelsFirst"),
                modelInput.dataKind == DataKind::ImageRgbChw);
    glUniform3f(glGetUniformLocation(state->program, "mean"),
                modelInput.mean.r,
                modelInput.mean.g,
                modelInput.mean.b);
    glUniform3f(glGetUniformLocation(state->program, "inverseStd"),
                1.0F / std::max(modelInput.std.r, normalizationEpsilon),
                1.0F / std::max(modelInput.std.g, normalizationEpsilon),
                1.0F / std::max(modelInput.std.b, normalizationEpsilon));
    glUniform3f(glGetUniformLocation(state->program, "padding"),
                std::clamp(modelInput.letterboxRed, 0.0F, 1.0F),
                std::clamp(modelInput.letterboxGreen, 0.0F, 1.0F),
                std::clamp(modelInput.letterboxBlue, 0.0F, 1.0F));
    if (sourceFormat != SourceFormat::Rgb) {
        require(frame.yuvColorMatrix() != YuvColorMatrix::Unknown &&
                    frame.yuvRange() != YuvRange::Unknown,
                "GPU preprocessing needs known YUV colorimetry and range");
        const auto coefficients{getYuvToRgbCoefficients(frame.yuvColorMatrix())};
        const float greenWeight{1.0F - coefficients.kr - coefficients.kb};
        const float redFromCr{chromaCoefficientScale * (1.0F - coefficients.kr)};
        const float blueFromCb{chromaCoefficientScale * (1.0F - coefficients.kb)};
        glUniform4f(glGetUniformLocation(state->program, "yuvCoefficients"),
                    redFromCr,
                    blueFromCb,
                    coefficients.kb * blueFromCb / greenWeight,
                    coefficients.kr * redFromCr / greenWeight);
        const bool limited{frame.yuvRange() == YuvRange::Limited};
        glUniform4f(glGetUniformLocation(state->program, "yuvRange"),
                    limited ? limitedLumaOffset : 0.0F,
                    1.0F / (limited ? limitedLumaRange : byteMaximum),
                    chromaCenter,
                    1.0F / (limited ? limitedChromaRange : byteMaximum));
    }
    glDispatchCompute(static_cast<GLuint>((outputWidth + workgroupWidth - 1) / workgroupWidth),
                      static_cast<GLuint>((outputHeight + workgroupHeight - 1) / workgroupHeight),
                      1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    checkGl("Dispatch preprocessing");
    const GLsync fence{glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)};
    require(fence != nullptr, "Cannot fence GPU preprocessing");
    const GLenum completion{
        glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, gpuWaitNanoseconds)};
    glDeleteSync(fence);
    require(completion == GL_ALREADY_SIGNALED || completion == GL_CONDITION_SATISFIED,
            "GPU preprocessing did not complete before the timeout");
    work.complete = true;
    if (state->mapped) {
        return {state->mapped, state->bytes};
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, state->tensor);
    const auto *const data{glMapBufferRange(
        GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(state->bytes), GL_MAP_READ_BIT)};
    require(data, "Cannot read back the GPU tensor");
    std::memcpy(state->readback.data(), data, state->bytes);
    require(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER), "GPU tensor readback became invalid");
    return state->readback;
}
std::string GpuPreprocess::identity() const {
    return state->deviceIdentity;
}
bool GpuPreprocess::inputCopied() const {
    return state->uploaded;
}
bool GpuPreprocess::tensorCopied() const {
    return !state->mapped;
}
} // namespace opk::gles

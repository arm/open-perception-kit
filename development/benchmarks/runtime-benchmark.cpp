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

#include "gst/FrameResultsMeta.h"

#include <gst/allocators/gstdmabuf.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

namespace {

using Buffer = std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)>;
using Memory = std::unique_ptr<GstMemory, decltype(&gst_memory_unref)>;

struct Options {
    gchar *pipeline = nullptr;
    gchar *heap = nullptr;
    gchar *format = nullptr;
    gint width = 640;
    gint height = 480;
    gint frames = 1000;
    gint warmup = 100;
    gboolean shared = FALSE;

    ~Options() {
        g_free(pipeline);
        g_free(heap);
        g_free(format);
    }
};

Memory allocateDmaBuf(GstAllocator *allocator, const char *heap, gsize size) {
    const int heapFd = open(heap, O_RDONLY | O_CLOEXEC);
    if (heapFd < 0) {
        throw std::runtime_error(std::string("Cannot open DMA heap: ") + heap);
    }
    dma_heap_allocation_data allocation{};
    allocation.len = size;
    allocation.fd_flags = O_RDWR | O_CLOEXEC;
    const int result = ioctl(heapFd, DMA_HEAP_IOCTL_ALLOC, &allocation);
    close(heapFd);
    if (result < 0) {
        throw std::runtime_error("DMA heap allocation failed");
    }
    Memory memory{gst_dmabuf_allocator_alloc(allocator, static_cast<int>(allocation.fd), size),
                  gst_memory_unref};
    if (!memory) {
        close(static_cast<int>(allocation.fd));
        throw std::runtime_error("Cannot wrap DMA-BUF memory");
    }
    return memory;
}

Buffer makeInput(const GstVideoInfo &video, GstAllocator *allocator, GstMemory *backing) {
    Buffer buffer{gst_buffer_new(), gst_buffer_unref};
    if (backing) {
        const int fd = dup(gst_dmabuf_memory_get_fd(backing));
        if (fd < 0) {
            throw std::runtime_error("Cannot duplicate DMA-BUF descriptor");
        }
        GstMemory *memory = gst_dmabuf_allocator_alloc(allocator, fd, video.size);
        if (!memory) {
            close(fd);
            throw std::runtime_error("Cannot wrap input DMA-BUF");
        }
        gst_buffer_append_memory(buffer.get(), memory);
    } else {
        GstMemory *memory = gst_allocator_alloc(nullptr, video.size, nullptr);
        if (!memory) {
            throw std::runtime_error("Cannot allocate input memory");
        }
        gst_buffer_append_memory(buffer.get(), memory);
    }
    // Drawing may change a reused allocation. Every iteration starts with identical pixels.
    GstMapInfo map;
    if (!gst_buffer_map(buffer.get(), &map, GST_MAP_WRITE)) {
        throw std::runtime_error("Input memory is not CPU-mappable");
    }
    std::memset(map.data, 128, map.size);
    gst_buffer_unmap(buffer.get(), &map);
    auto layout = video;
    if (!gst_buffer_add_video_meta_full(buffer.get(),
                                        GST_VIDEO_FRAME_FLAG_NONE,
                                        GST_VIDEO_INFO_FORMAT(&video),
                                        video.width,
                                        video.height,
                                        GST_VIDEO_INFO_N_PLANES(&video),
                                        layout.offset,
                                        layout.stride)) {
        throw std::runtime_error("Cannot attach video layout");
    }
    return buffer;
}

double cpuSeconds() {
    timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) {
        throw std::runtime_error("Cannot read process CPU time");
    }
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_nsec) / 1e9;
}

nlohmann::json measure(const Options &options) {
    GstVideoInfo video;
    gst_video_info_init(&video);
    const char *format = options.format ? options.format : "NV12";
    if (!gst_video_info_set_format(
            &video, gst_video_format_from_string(format), options.width, options.height)) {
        throw std::runtime_error("Invalid video format or dimensions");
    }
    video.fps_n = 30;
    video.fps_d = 1;
    const std::unique_ptr<GstAllocator, decltype(&gst_object_unref)> allocator{
        gst_dmabuf_allocator_new(), gst_object_unref};
    std::vector<Memory> backing;
    if (options.heap) {
        for (int i = 0; i < 4; ++i) {
            backing.push_back(allocateDmaBuf(allocator.get(), options.heap, video.size));
        }
    }
    const std::unique_ptr<GstHarness, decltype(&gst_harness_teardown)> harness{
        gst_harness_new_parse(options.pipeline), gst_harness_teardown};
    if (!harness) {
        throw std::runtime_error("Cannot construct pipeline");
    }
    gst_harness_use_systemclock(harness.get());
    gst_harness_set_src_caps(harness.get(), gst_video_info_to_caps(&video));
    gst_harness_play(harness.get());

    std::vector<double> elapsed;
    elapsed.reserve(options.frames);
    double cpu = 0;
    gint sameMemory = 0;
    gint dmaOutput = 0;
    gint results = 0;
    for (std::int64_t index = -static_cast<std::int64_t>(options.warmup); index < options.frames;
         ++index) {
        const auto sequence = static_cast<std::uint64_t>(index + options.warmup);
        auto input =
            makeInput(video,
                      allocator.get(),
                      backing.empty() ? nullptr : backing[sequence % backing.size()].get());
        const auto pts = gst_util_uint64_scale(index + options.warmup, GST_SECOND, 30);
        GST_BUFFER_PTS(input.get()) = pts;
        GST_BUFFER_DURATION(input.get()) = GST_SECOND / 30;
        const auto originalMemory =
            reinterpret_cast<std::uintptr_t>(gst_buffer_peek_memory(input.get(), 0));
        const Buffer shared{options.shared ? gst_buffer_ref(input.get()) : nullptr,
                            gst_buffer_unref};

        const double cpuStart = cpuSeconds();
        const auto start = std::chrono::steady_clock::now();
        const auto flow = gst_harness_push(harness.get(), input.release());
        Buffer output{gst_harness_try_pull(harness.get()), gst_buffer_unref};
        const auto end = std::chrono::steady_clock::now();
        const double cpuEnd = cpuSeconds();
        if (flow != GST_FLOW_OK || !output || GST_BUFFER_PTS(output.get()) != pts) {
            throw std::runtime_error(
                "Pipeline failed, buffered, or dropped a frame; use a synchronous chain");
        }
        if (index < 0) {
            continue;
        }
        elapsed.push_back(std::chrono::duration<double>(end - start).count());
        cpu += cpuEnd - cpuStart;
        if (gst_buffer_n_memory(output.get()) == 1) {
            auto *memory = gst_buffer_peek_memory(output.get(), 0);
            sameMemory += reinterpret_cast<std::uintptr_t>(memory) == originalMemory;
            dmaOutput += gst_is_dmabuf_memory(memory);
        }
        results += static_cast<bool>(opk::FrameResultsMeta::read(output.get()));
    }
    const double total = std::accumulate(elapsed.begin(), elapsed.end(), 0.0);
    std::sort(elapsed.begin(), elapsed.end());
    const std::unique_ptr<gchar, decltype(&g_free)> version{gst_version_string(), g_free};
    return {{"pipeline", options.pipeline},
            {"gstreamer", version.get()},
            {"memory", options.heap ? options.heap : "system"},
            {"format", format},
            {"width", options.width},
            {"height", options.height},
            {"frames", options.frames},
            {"warmup", options.warmup},
            {"shared_input", static_cast<bool>(options.shared)},
            {"dma_allocations", backing.size()},
            {"mean_ms", 1000 * total / options.frames},
            {"p50_ms", 1000 * elapsed[elapsed.size() / 2]},
            {"p95_ms", 1000 * elapsed[(elapsed.size() - 1) * 95 / 100]},
            {"cpu_ms_per_frame", 1000 * cpu / options.frames},
            {"processing_fps", options.frames / total},
            {"same_memory_frames", sameMemory},
            {"dmabuf_output_frames", dmaOutput},
            {"results_frames", results}};
}

} // namespace

int main(int argc, char **argv) {
    Options options;
    GOptionEntry entries[] = {
        {"pipeline",
         0,
         0,
         G_OPTION_ARG_STRING,
         &options.pipeline,
         "Synchronous element chain, without a source or sink",
         "CHAIN"},
        {"dma-heap",
         0,
         0,
         G_OPTION_ARG_STRING,
         &options.heap,
         "Allocate real DMA-BUF input; default is system memory",
         "DEVICE"},
        {"format", 0, 0, G_OPTION_ARG_STRING, &options.format, "Raw format (NV12)", "FORMAT"},
        {"width", 0, 0, G_OPTION_ARG_INT, &options.width, "Frame width (640)", "PIXELS"},
        {"height", 0, 0, G_OPTION_ARG_INT, &options.height, "Frame height (480)", "PIXELS"},
        {"frames", 0, 0, G_OPTION_ARG_INT, &options.frames, "Measured frames (1000)", "COUNT"},
        {"warmup", 0, 0, G_OPTION_ARG_INT, &options.warmup, "Untimed frames (100)", "COUNT"},
        {"shared-input",
         0,
         0,
         G_OPTION_ARG_NONE,
         &options.shared,
         "Retain an input reference, as a tee consumer might",
         nullptr},
        {},
    };
    const std::unique_ptr<GOptionContext, decltype(&g_option_context_free)> context{
        g_option_context_new("- benchmark OPK frame processing"), g_option_context_free};
    g_option_context_add_main_entries(context.get(), entries, nullptr);
    g_option_context_add_group(context.get(), gst_init_get_option_group());
    GError *error = nullptr;
    if (!g_option_context_parse(context.get(), &argc, &argv, &error)) {
        std::cerr << error->message << '\n';
        g_clear_error(&error);
        return 1;
    }
    if (!options.pipeline || options.width <= 0 || options.height <= 0 || options.frames <= 0 ||
        options.warmup < 0) {
        std::cerr
            << "Provide --pipeline, positive dimensions/frame count, and nonnegative warmup\n";
        return 1;
    }
    try {
        const auto report = measure(options);
        std::cout << report.dump() << '\n';
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

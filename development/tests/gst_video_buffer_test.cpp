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

#include <gtest/gtest.h>

#include "gst/FrameResultsMeta.h"
#include "gst/Tools.h"
#include "mediaio/GstVideoFrame.h"
#include "op/OpChainContext.h"

#include <array>
#include <memory>
#include <string>
#include <unistd.h>

namespace {

constexpr guint testFrameWidth = 4;
constexpr guint testFrameHeight = 4;
constexpr gsize testFrameBytesPerPixel = 4;
constexpr gsize testFrameBytes = testFrameBytesPerPixel * testFrameWidth * testFrameHeight;
constexpr guint8 testFramePixelValue =
    0x5a; // filled in for every pixel results in dark grey with 35% opacity, but it could be
          // anything, the exact value does not matter.
constexpr GstClockTime testFramePts = GST_SECOND;
constexpr guint testFrameFrameRate = 30;
constexpr GstClockTime testFrameDuration = GST_SECOND / testFrameFrameRate;
constexpr auto testFrameDmaCaps = "video/x-raw(memory:DMABuf),format=BGRA,width=4,height=4";
constexpr auto testFrameCpuCaps = "video/x-raw,format=BGRA,width=4,height=4";

using BufferRef = std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)>;
using CapsRef = std::unique_ptr<GstCaps, decltype(&gst_caps_unref)>;

class GstVideoBuffer : public testing::Test {
  protected:
    GstElement *testExecutionContext = nullptr;
    GstBus *errorMessageBus = nullptr;
    GstBuffer *inputBuffer = nullptr;
    GstBuffer *outputBuffer = nullptr;
    std::shared_ptr<open_perception_kit::FrameResults> results;

    void SetUp() override {
        gst_init(nullptr, nullptr);
        // Identity element is a pass-through element forwarding buffers unchanged
        // It only provides the context (via GST_BASE_TRANSFORM), no data is passed through it.
        testExecutionContext = gst_element_factory_make("identity", nullptr);
        ASSERT_NE(testExecutionContext, nullptr);
        errorMessageBus = gst_bus_new();
        gst_element_set_bus(testExecutionContext, errorMessageBus);
        inputBuffer = gst_buffer_new();

        // A file-backed FD tests GstDmaBufMemory ownership and copying without a GPU or camera.
        // It does not test kernel DMA synchronization or device import.
        gchar *path = nullptr;
        const int fd = g_file_open_tmp(nullptr, &path, nullptr);
        ASSERT_GE(fd, 0);
        unlink(path);
        g_free(path);
        if (ftruncate(fd, testFrameBytes) != 0) {
            close(fd);
            FAIL() << "Could not size the DMA-BUF test backing file";
        }
        auto *const allocator = gst_dmabuf_allocator_new();
        auto *const memory = gst_dmabuf_allocator_alloc(allocator, fd, testFrameBytes);
        gst_object_unref(allocator);
        ASSERT_NE(memory, nullptr);
        gst_buffer_append_memory(inputBuffer, memory);
        ASSERT_EQ(gst_buffer_memset(inputBuffer, 0, testFramePixelValue, testFrameBytes),
                  testFrameBytes);
        GST_BUFFER_PTS(inputBuffer) = testFramePts;
        GST_BUFFER_DURATION(inputBuffer) = testFrameDuration;
        GST_BUFFER_FLAG_SET(inputBuffer, GST_BUFFER_FLAG_LIVE);
        ASSERT_NE(gst_buffer_add_video_meta(inputBuffer,
                                            GST_VIDEO_FRAME_FLAG_NONE,
                                            GST_VIDEO_FORMAT_BGRA,
                                            testFrameWidth,
                                            testFrameHeight),
                  nullptr);
        results = std::make_shared<open_perception_kit::FrameResults>();
        open_perception_kit::appendPerformanceOverlay(*results, {"test frame"});
        ASSERT_NE(opk::FrameResultsMeta::add(inputBuffer, results), nullptr);
    }

    void TearDown() override {
        if (outputBuffer == inputBuffer)
            outputBuffer = nullptr; // prepare_output_buffer borrows the input when reusing it.
        gst_clear_buffer(&outputBuffer);
        gst_clear_buffer(&inputBuffer);
        gst_clear_object(&testExecutionContext);
        gst_clear_object(&errorMessageBus);
    }

    GstFlowReturn prepare(const bool copyPixels = false, const bool writePixels = false) {
        return gst::Tools::prepareVideoBuffer(GST_BASE_TRANSFORM(testExecutionContext),
                                              inputBuffer,
                                              &outputBuffer,
                                              copyPixels,
                                              writePixels);
    }

    void expectFrameData() const {
        ASSERT_NE(outputBuffer, nullptr);
        EXPECT_EQ(GST_BUFFER_PTS(outputBuffer), testFramePts);
        EXPECT_EQ(GST_BUFFER_DURATION(outputBuffer), testFrameDuration);
        EXPECT_TRUE(GST_BUFFER_FLAG_IS_SET(outputBuffer, GST_BUFFER_FLAG_LIVE));
        EXPECT_EQ(opk::FrameResultsMeta::read(outputBuffer), results);
        const auto *const videoMeta = gst_buffer_get_video_meta(outputBuffer);
        ASSERT_NE(videoMeta, nullptr);
        EXPECT_EQ(videoMeta->width, testFrameWidth);
        EXPECT_EQ(videoMeta->height, testFrameHeight);
        EXPECT_EQ(videoMeta->stride[0], testFrameWidth * testFrameBytesPerPixel);
        std::array<guint8, testFrameBytes> testFramePixels{};
        ASSERT_EQ(
            gst_buffer_extract(outputBuffer, 0, testFramePixels.data(), testFramePixels.size()),
            testFramePixels.size());
        for (const auto pixel : testFramePixels)
            EXPECT_EQ(pixel, testFramePixelValue);
    }

    void expectError(const GQuark domain, const gint code, const char *const messageText) const {
        EXPECT_EQ(outputBuffer, nullptr);
        auto *const message = gst_bus_pop_filtered(errorMessageBus, GST_MESSAGE_ERROR);
        ASSERT_NE(message, nullptr);
        GError *error = nullptr;
        gst_message_parse_error(message, &error, nullptr);
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(error->domain, domain);
        EXPECT_EQ(error->code, code);
        EXPECT_NE(std::string(error->message).find(messageText), std::string::npos);
        g_error_free(error);
        gst_message_unref(message);
    }
};

TEST_F(GstVideoBuffer, ReusesWritableDmaBuffer) {
    ASSERT_EQ(prepare(false, true), GST_FLOW_OK);
    EXPECT_EQ(outputBuffer, inputBuffer);
    EXPECT_TRUE(gst_is_dmabuf_memory(gst_buffer_peek_memory(outputBuffer, 0)));
    expectFrameData();
    GstVideoInfo videoInfo{};
    ASSERT_TRUE(gst_video_info_set_format(
        &videoInfo, GST_VIDEO_FORMAT_BGRA, testFrameWidth, testFrameHeight));
    const auto retained{opk::mediaio::gst::GstVideoFrame::retainDmaBuf(outputBuffer, videoInfo)};
    ASSERT_NE(retained, nullptr);
    ASSERT_EQ(retained->planeCount(), 1U);
    EXPECT_TRUE(retained->planes()[0].hasDmaBuf());
    EXPECT_FALSE(retained->planes()[0].hasHostData());
    EXPECT_EQ(retained->planes()[0].fd(),
              gst_dmabuf_memory_get_fd(gst_buffer_peek_memory(outputBuffer, 0)));
    const auto readable{retained->map(opk::AccessMode::Read)};
    ASSERT_NE(readable, nullptr);
    EXPECT_TRUE(readable->planes()[0].hasHostData());
    opk::op::OpChainContext context;
    context.videoFrames["test"] = retained;
    const auto *const cached{context.getReadableVideoFrame("test")};
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(context.getReadableVideoFrame("test"), cached);
    EXPECT_FALSE(context.getVideoFrame("test")->planes()[0].hasHostData());

    // The allocation can include padding beyond the negotiated image dimensions.
    constexpr guint paddingPixels{1};
    constexpr guint visibleWidth{testFrameWidth - paddingPixels};
    constexpr guint visibleHeight{testFrameHeight - paddingPixels};
    GstVideoInfo visibleInfo{};
    ASSERT_TRUE(gst_video_info_set_format(
        &visibleInfo, GST_VIDEO_FORMAT_BGRA, visibleWidth, visibleHeight));
    const auto paddedFrame{
        opk::mediaio::gst::GstVideoFrame::retainDmaBuf(outputBuffer, visibleInfo)};
    ASSERT_NE(paddedFrame, nullptr);
    EXPECT_EQ(paddedFrame->width(), visibleWidth);
    EXPECT_EQ(paddedFrame->height(), visibleHeight);
    ASSERT_EQ(paddedFrame->planeCount(), 1U);
    EXPECT_EQ(paddedFrame->planes()[0].fd(), retained->planes()[0].fd());
    EXPECT_EQ(paddedFrame->planes()[0].strideBytes(), retained->planes()[0].strideBytes());
    EXPECT_FALSE(paddedFrame->planes()[0].hasHostData());
    const auto readablePaddedFrame{paddedFrame->map(opk::AccessMode::Read)};
    ASSERT_NE(readablePaddedFrame, nullptr);
    EXPECT_EQ(readablePaddedFrame->width(), visibleWidth);
    EXPECT_EQ(readablePaddedFrame->height(), visibleHeight);

    constexpr gsize halfFrameBytes{testFrameBytes / 2};
    BufferRef splitBuffer{gst_buffer_new(), gst_buffer_unref};
    auto *const memory{gst_buffer_peek_memory(outputBuffer, 0)};
    gst_buffer_append_memory(splitBuffer.get(), gst_memory_share(memory, 0, halfFrameBytes));
    gst_buffer_append_memory(splitBuffer.get(),
                             gst_memory_share(memory, halfFrameBytes, halfFrameBytes));
    const auto splitFrame{
        opk::mediaio::gst::GstVideoFrame::retainDmaBuf(splitBuffer.get(), videoInfo)};
    ASSERT_NE(splitFrame, nullptr);
    EXPECT_TRUE(splitFrame->planes().empty());
    EXPECT_NE(splitFrame->map(opk::AccessMode::Read), nullptr);

    GstVideoInfo mismatchedInfo{};
    ASSERT_TRUE(gst_video_info_set_format(
        &mismatchedInfo, GST_VIDEO_FORMAT_BGRA, testFrameWidth + 1, testFrameHeight));
    EXPECT_EQ(opk::mediaio::gst::GstVideoFrame::retainDmaBuf(outputBuffer, mismatchedInfo),
              nullptr);

    outputBuffer = nullptr; // The retained frame now keeps the original allocation alive.
    gst_clear_buffer(&inputBuffer);
    EXPECT_EQ(context.getReadableVideoFrame("test"), cached);
    EXPECT_EQ(*static_cast<const guint8 *>(cached->planes()[0].data()), testFramePixelValue);
}

TEST_F(GstVideoBuffer, SharesMemoryAndRetainsMetadataWhenHeaderIsShared) {
    auto *const memory = gst_buffer_peek_memory(inputBuffer, 0);
    const int fd = gst_dmabuf_memory_get_fd(memory);
    const int secondFd = dup(fd);
    ASSERT_GE(secondFd, 0);
    auto *const secondMemory =
        gst_dmabuf_allocator_alloc(memory->allocator, secondFd, testFrameBytes);
    ASSERT_NE(secondMemory, nullptr);
    gst_buffer_append_memory(inputBuffer, secondMemory);
    BufferRef otherOwner{gst_buffer_ref(inputBuffer), gst_buffer_unref};
    ASSERT_EQ(prepare(), GST_FLOW_OK);
    ASSERT_NE(outputBuffer, inputBuffer);
    ASSERT_EQ(gst_buffer_n_memory(outputBuffer), gst_buffer_n_memory(inputBuffer));
    for (guint i = 0; i < gst_buffer_n_memory(inputBuffer); ++i)
        EXPECT_EQ(gst_buffer_peek_memory(outputBuffer, i), gst_buffer_peek_memory(inputBuffer, i));
    EXPECT_EQ(gst_dmabuf_memory_get_fd(gst_buffer_peek_memory(outputBuffer, 0)), fd);
    otherOwner.reset();
    gst_clear_buffer(&inputBuffer);
    expectFrameData(); // The original buffer is gone; output must still own the allocation.
}

TEST_F(GstVideoBuffer, RejectsDrawingWithSharedHeader) {
    const BufferRef otherOwner{gst_buffer_ref(inputBuffer), gst_buffer_unref};
    EXPECT_EQ(prepare(false, true), GST_FLOW_ERROR);
    expectError(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_WRITE, "copy-input=true");
}

TEST_F(GstVideoBuffer, RejectsDrawingWithSharedMemory) {
    const BufferRef otherOwner{gst_buffer_copy(inputBuffer), gst_buffer_unref};
    ASSERT_TRUE(gst_buffer_is_writable(inputBuffer));
    EXPECT_EQ(prepare(false, true), GST_FLOW_ERROR);
    expectError(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_WRITE, "copy-input=true");
}

TEST_F(GstVideoBuffer, RejectsDrawingWithReadOnlyMemory) {
    GST_MINI_OBJECT_FLAG_SET(gst_buffer_peek_memory(inputBuffer, 0), GST_MEMORY_FLAG_READONLY);
    EXPECT_EQ(prepare(false, true), GST_FLOW_ERROR);
    expectError(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_WRITE, "copy-input=true");
}

TEST_F(GstVideoBuffer, RejectsNonShareableMemoryInsteadOfSilentlyCopying) {
    GST_MINI_OBJECT_FLAG_SET(gst_buffer_peek_memory(inputBuffer, 0), GST_MEMORY_FLAG_NO_SHARE);
    const BufferRef otherOwner{gst_buffer_ref(inputBuffer), gst_buffer_unref};
    EXPECT_EQ(prepare(), GST_FLOW_ERROR);
    expectError(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ, "copy-input=true");
}

TEST_F(GstVideoBuffer, ExplicitCopyRetainsFrameDataAndSeparatesPixels) {
    GST_MINI_OBJECT_FLAG_SET(gst_buffer_peek_memory(inputBuffer, 0), GST_MEMORY_FLAG_READONLY);
    const BufferRef otherOwner{gst_buffer_ref(inputBuffer), gst_buffer_unref};
    ASSERT_EQ(prepare(true, true), GST_FLOW_OK);
    ASSERT_NE(outputBuffer, inputBuffer);
    EXPECT_TRUE(opk::mediaio::gst::GstVideoFrame::hasDirectCpuAddress(outputBuffer));
    EXPECT_FALSE(opk::mediaio::gst::GstVideoFrame::hasDmaBufContent(outputBuffer));
    expectFrameData();

    ASSERT_EQ(gst_buffer_memset(outputBuffer, 0, 0, testFrameBytes), testFrameBytes);
    guint8 originalPixel = 0;
    ASSERT_EQ(gst_buffer_extract(inputBuffer, 0, &originalPixel, sizeof(originalPixel)),
              sizeof(originalPixel));
    EXPECT_EQ(originalPixel, testFramePixelValue);
}

TEST_F(GstVideoBuffer, AcceptsOrdinarySystemMemory) {
    gst_buffer_remove_all_memory(inputBuffer);
    gst_buffer_append_memory(inputBuffer, gst_allocator_alloc(nullptr, testFrameBytes, nullptr));
    ASSERT_EQ(gst_buffer_memset(inputBuffer, 0, testFramePixelValue, testFrameBytes),
              testFrameBytes);
    ASSERT_EQ(prepare(), GST_FLOW_OK);
    EXPECT_EQ(outputBuffer, inputBuffer);
    EXPECT_TRUE(opk::mediaio::gst::GstVideoFrame::hasDirectCpuAddress(outputBuffer));
    expectFrameData();
}

TEST_F(GstVideoBuffer, ExplicitDmaCapsRejectBuffersContainingSystemMemory) {
    gst_buffer_append_memory(inputBuffer, gst_allocator_alloc(nullptr, testFrameBytes, nullptr));
    const CapsRef caps{gst_caps_from_string(testFrameDmaCaps), gst_caps_unref};
    auto *const pad = GST_BASE_TRANSFORM_SINK_PAD(testExecutionContext);
    ASSERT_TRUE(gst_pad_set_active(pad, TRUE));
    auto *const event = gst_event_new_caps(caps.get());
    const auto stored = gst_pad_store_sticky_event(pad, event);
    gst_event_unref(event);
    ASSERT_EQ(stored, GST_FLOW_OK);

    EXPECT_EQ(prepare(), GST_FLOW_ERROR);
    expectError(GST_STREAM_ERROR, GST_STREAM_ERROR_FORMAT, "require DMA-BUF-backed");
}

TEST_F(GstVideoBuffer, CapsNegotiationDescribesCopyAndPreservation) {
    const CapsRef dma{gst_caps_from_string(testFrameDmaCaps), gst_caps_unref};
    const CapsRef cpu{gst_caps_from_string(testFrameCpuCaps), gst_caps_unref};
    const CapsRef preserved{gst::Tools::transformVideoCaps(GST_PAD_SINK, dma.get(), nullptr, false),
                            gst_caps_unref};
    EXPECT_TRUE(gst_caps_is_equal(preserved.get(), dma.get()));
    const CapsRef copied{gst::Tools::transformVideoCaps(GST_PAD_SINK, dma.get(), nullptr, true),
                         gst_caps_unref};
    EXPECT_TRUE(gst_caps_is_equal(copied.get(), cpu.get()));
    const CapsRef inputs{gst::Tools::transformVideoCaps(GST_PAD_SRC, cpu.get(), nullptr, true),
                         gst_caps_unref};
    EXPECT_TRUE(gst_caps_is_subset(cpu.get(), inputs.get()));
    EXPECT_TRUE(gst_caps_is_subset(dma.get(), inputs.get()));
    const CapsRef impossible{gst::Tools::transformVideoCaps(GST_PAD_SRC, dma.get(), nullptr, true),
                             gst_caps_unref};
    EXPECT_TRUE(gst_caps_is_empty(impossible.get()));
    const CapsRef filtered{gst::Tools::transformVideoCaps(GST_PAD_SRC, cpu.get(), dma.get(), true),
                           gst_caps_unref};
    EXPECT_TRUE(gst_caps_is_equal(filtered.get(), dma.get()));
}

} // namespace

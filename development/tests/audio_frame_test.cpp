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

#include "mediaio/GstAudioFrame.h"
#include "opk/PcmRingBuffer.h"
#include <array>
#include <gtest/gtest.h>

namespace {
using opk::mediaio::gst::GstAudioFrame;

class HostAudioFrame final : public opk::mediaio::AudioFrame {
  public:
    std::array<int16_t, 8> samples{100, 200, -100, 300, 400, -200, 50, 70};
    opk::mediaio::DataView view =
        opk::mediaio::DataView::host(samples.data(), sizeof(samples), 0, opk::AccessMode::Read);
    opk::AudioSampleType sampleType() const noexcept override {
        return opk::AudioSampleType::S16;
    }
    size_t channels() const noexcept override {
        return 2;
    }
    size_t sampleRate() const noexcept override {
        return 48000;
    }
    size_t sampleCount() const noexcept override {
        return samples.size() / channels();
    }
    opk::mediaio::TimestampNs timestampNs() const noexcept override {
        return 0;
    }
    const opk::mediaio::DataView &data() const noexcept override {
        return view;
    }
};

TEST(AudioFrame, BackendNeutralFeedMatchesRawBytes) {
    HostAudioFrame frame;
    opk::PcmRingBuffer adapted(32), raw(32);
    ASSERT_TRUE(adapted.feed(frame));
    ASSERT_TRUE(raw.feed(
        std::span(static_cast<const uint8_t *>(frame.data().data()), frame.data().byteSize()),
        frame.sampleType(),
        frame.channels(),
        frame.sampleRate()));
    std::vector<float> a(adapted.size()), b(raw.size());
    ASSERT_TRUE(adapted.view().copyTo(a));
    ASSERT_TRUE(raw.view().copyTo(b));
    EXPECT_EQ(a, b);
    EXPECT_EQ(adapted.resamplingDelayNs(), raw.resamplingDelayNs());
    frame.view = opk::mediaio::DataView::host(
        frame.samples.data(), sizeof(frame.samples), 0, opk::AccessMode::Write);
    EXPECT_FALSE(adapted.feed(frame));
}

TEST(GstAudioFrame, PreservesFormatsAndOwnsMapping) {
    gst_init(nullptr, nullptr);
    for (const auto format : {GST_AUDIO_FORMAT_U8,
                              GST_AUDIO_FORMAT_S16,
                              GST_AUDIO_FORMAT_S24LE,
                              GST_AUDIO_FORMAT_S32,
                              GST_AUDIO_FORMAT_F32}) {
        GstAudioInfo info;
        gst_audio_info_set_format(&info, format, 44100, 2, nullptr);
        auto *buffer = gst_buffer_new_allocate(
            nullptr, static_cast<size_t>(GST_AUDIO_INFO_BPF(&info)) * 4, nullptr);
        ASSERT_NE(buffer, nullptr);
        GST_BUFFER_PTS(buffer) = 0;
        bool released = false;
        gst_mini_object_weak_ref(
            GST_MINI_OBJECT(buffer),
            [](gpointer data, GstMiniObject *) { *static_cast<bool *>(data) = true; },
            &released);
        auto frame = GstAudioFrame::mapGstBuffer(buffer, info);
        gst_buffer_unref(buffer);
        ASSERT_NE(frame, nullptr);
        EXPECT_FALSE(released);
        EXPECT_EQ(frame->timestampNs(), 0u);
        EXPECT_EQ(frame->sampleRate(), 44100u);
        EXPECT_EQ(frame->channels(), 2u);
        EXPECT_EQ(frame->sampleCount(), 4u);
        EXPECT_EQ(frame->sampleType(), GstAudioFrame::sampleTypeFromFormat(format));
        EXPECT_TRUE(frame->data().canRead());
        EXPECT_FALSE(frame->data().canWrite());
        EXPECT_NE(frame->data().data(), nullptr);
        frame.reset();
        EXPECT_TRUE(released);
    }
}

TEST(GstAudioFrame, RejectsInvalidFramesAndPreservesMissingTimestamp) {
    gst_init(nullptr, nullptr);
    GstAudioInfo info;
    gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_S16, 16000, 1, nullptr);
    auto *buffer = gst_buffer_new_allocate(nullptr, 3, nullptr);
    EXPECT_EQ(GstAudioFrame::mapGstBuffer(buffer, info), nullptr);
    gst_buffer_unref(buffer);
    buffer = gst_buffer_new_allocate(nullptr, 4, nullptr);
    auto frame = GstAudioFrame::mapGstBuffer(buffer, info);
    EXPECT_NE(frame, nullptr);
    if (frame) {
        EXPECT_EQ(frame->timestampNs(), opk::mediaio::InvalidTimestampNs);
    }
    info.layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;
    EXPECT_EQ(GstAudioFrame::mapGstBuffer(buffer, info), nullptr);
    gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_F64, 16000, 1, nullptr);
    EXPECT_EQ(GstAudioFrame::mapGstBuffer(buffer, info), nullptr);
    EXPECT_EQ(GstAudioFrame::mapGstBuffer(nullptr, info), nullptr);
    gst_buffer_unref(buffer);
}
} // namespace

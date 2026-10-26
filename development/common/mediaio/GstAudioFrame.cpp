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

namespace opk::mediaio::gst {

std::optional<AudioSampleType> GstAudioFrame::sampleTypeFromFormat(GstAudioFormat format) noexcept {
    switch (format) {
    case GST_AUDIO_FORMAT_U8:
        return AudioSampleType::U8;
    case GST_AUDIO_FORMAT_S16:
        return AudioSampleType::S16;
    case GST_AUDIO_FORMAT_S24LE:
        return AudioSampleType::S24;
    case GST_AUDIO_FORMAT_S32:
        return AudioSampleType::S32;
    case GST_AUDIO_FORMAT_F32:
        return AudioSampleType::F32;
    default:
        return std::nullopt;
    }
}

bool GstAudioFrame::supports(const GstAudioInfo &info) noexcept {
    return GST_AUDIO_INFO_IS_VALID(&info) &&
           GST_AUDIO_INFO_LAYOUT(&info) == GST_AUDIO_LAYOUT_INTERLEAVED &&
           GST_AUDIO_INFO_BPF(&info) > 0 &&
           sampleTypeFromFormat(GST_AUDIO_INFO_FORMAT(&info)).has_value();
}

std::unique_ptr<GstAudioFrame> GstAudioFrame::mapGstBuffer(GstBuffer *buffer,
                                                           const GstAudioInfo &info) {
    if (!buffer || !supports(info))
        return nullptr;
    const auto type = sampleTypeFromFormat(GST_AUDIO_INFO_FORMAT(&info));
    if (!type)
        return nullptr;
    auto frame = std::unique_ptr<GstAudioFrame>(new GstAudioFrame());
    frame->buffer_ = gst_buffer_ref(buffer);
    if (!gst_buffer_map(frame->buffer_, &frame->map_, GST_MAP_READ))
        return nullptr;
    frame->mapped_ = true;
    const auto frameBytes = static_cast<size_t>(GST_AUDIO_INFO_BPF(&info));
    if (frame->map_.size % frameBytes)
        return nullptr;
    frame->type_ = *type;
    frame->channels_ = static_cast<size_t>(GST_AUDIO_INFO_CHANNELS(&info));
    frame->rate_ = static_cast<size_t>(GST_AUDIO_INFO_RATE(&info));
    frame->count_ = frame->map_.size / frameBytes;
    frame->timestamp_ =
        GST_BUFFER_PTS_IS_VALID(buffer) ? GST_BUFFER_PTS(buffer) : InvalidTimestampNs;
    frame->data_ = DataView::host(frame->map_.data, frame->map_.size, 0, AccessMode::Read);
    return frame;
}

GstAudioFrame::~GstAudioFrame() {
    if (mapped_)
        gst_buffer_unmap(buffer_, &map_);
    if (buffer_)
        gst_buffer_unref(buffer_);
}

} // namespace opk::mediaio::gst

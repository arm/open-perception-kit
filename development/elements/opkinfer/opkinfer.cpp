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
#include "gst/gstelement.h"
#include "gst/gstpad.h"
#include <gst/audio/audio.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <algorithm>
#include <fmt/core.h>
#include <memory>
#include <string_view>
#include <variant>
#include <vector>

#include "glib-object.h"
#include "glib.h"

#include "Log.h"
#include "opk/PcmRingBuffer.h"
#include "opk/Result.h"
#include "opk/Tools.h"

#include "op/OpChain.h"
#include "op/OpChainContext.h"

#include "gst/ContentRequirementEvent.h"
#include "gst/FrameResultsMeta.h"
#include "gst/PropertyGuard.h"
#include "gst/Tools.h"
#include "mediaio/GstAudioFrame.h"
#include "mediaio/GstVideoFrame.h"

struct GstOpkInferMembers {
    static constexpr size_t audioBufferCapacity = size_t{16000} * 60;
    std::unique_ptr<opk::PcmRingBuffer> pcmAudioBuffer;
    size_t audioWindowSamples = 0;
    size_t audioHopSamples = 0;
    uint64_t nextAudioPosition = 0;
    bool audioDiscontinuity = true;
    uint64_t audioSourceFrames = 0;
    std::optional<uint64_t> audioOriginTimestampNs;

    void resetAudio() {
        if (pcmAudioBuffer)
            pcmAudioBuffer->clear();
        nextAudioPosition = 0;
        audioDiscontinuity = true;
        audioSourceFrames = 0;
        audioOriginTimestampNs.reset();
    }

    opk::op::OpChain opChain;
    std::optional<bool> inputIsLive;

    opk::Result<void> executeOpChain(opk::op::OpChainContext &opChainContext,
                                     GstPad *const inputPad) {
        if (!inputIsLive) {
            // Query on the first frame, after upstream has started. Unknown inputs must wait.
            GstQuery *const query = gst_query_new_latency();
            gboolean live = FALSE;
            if (gst_pad_peer_query(inputPad, query))
                gst_query_parse_latency(query, &live, nullptr, nullptr);
            inputIsLive = live;
            gst_query_unref(query);
        }
        return opChain.execute(opChainContext, !*inputIsLive);
    }

    opk::Result<void> setupOpChainFromJson(const std::string &filePath) {
        auto setupResult = opChain.setupFromFile(filePath);
        if (!setupResult) {
            return setupResult;
        }
        return {};
    }
};

#ifndef PACKAGE
#define PACKAGE "opk-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_OPKINFER (gst_opkinfer_get_type())
G_DECLARE_FINAL_TYPE(GstOpkInfer, gst_opkinfer, GST, OPKINFER, GstBaseTransform)

struct _GstOpkInfer {
    GstBaseTransform parent;

    // cached from caps
    GstVideoInfo videoInfo;
    GstAudioInfo audioInfo;
    gboolean isAudio;
    guint audioHopSamples;

    // Properties
    gchar *opChainPath;
    gboolean active;
    gboolean copyInput;
    gchar *inferId;

    gboolean qosEnabled;
    GstClockTime qosEarliestTime;
    guint64 processingSkipFrames;
    gdouble qosProportion;
    GstClockTime qosTimestamp;
    guint64 qosProcessed;
    guint64 qosDropped;
    guint64 qosGeneration;
    guint64 qosAcceptedEventsDebug;

    // a safe place for c++ stuff
    std::shared_ptr<GstOpkInferMembers> runtimeMembers;
};

G_END_DECLS

G_DEFINE_TYPE(GstOpkInfer, gst_opkinfer, GST_TYPE_BASE_TRANSFORM)

// ---------------- GstBaseTransform virtuals ----------------
//
static const gchar *gst_opkinfer_get_effective_inferId(GstOpkInfer *self) {
    /* If user provided infer-id property, prefer it */
    if (self->inferId && self->inferId[0] != '\0')
        return self->inferId;

    /* Fallback to element name (always exists) */
    return GST_OBJECT_NAME(GST_ELEMENT(self));
}

static gboolean gst_opkinfer_is_active(GstOpkInfer *self) {
    GST_OBJECT_LOCK(self);
    const gboolean active = self->active;
    GST_OBJECT_UNLOCK(self);
    return active;
}

static std::shared_ptr<GstOpkInferMembers> gst_opkinfer_get_members(GstOpkInfer *const self) {
    g_autoptr(GMutexLocker) objectLock = g_mutex_locker_new(GST_OBJECT_GET_LOCK(self));
    return self->runtimeMembers;
}

static void gst_opkinfer_emit_content_requirements(GstOpkInfer *self) {
    const auto members = gst_opkinfer_get_members(self);
    if (members == nullptr)
        return;

    for (const auto contentType : members->opChain.getRequiredContentTypes()) {
        const std::string contentTypeString(contentType);
        GstStructure *structure =
            gst_structure_new(opk::content_requirement_event::k_name.data(),
                              opk::content_requirement_event::k_content_type_field.data(),
                              G_TYPE_STRING,
                              contentTypeString.c_str(),
                              nullptr);
        gst_pad_push_event(GST_BASE_TRANSFORM_SINK_PAD(self),
                           gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, structure));
    }
}

static bool gst_opkinfer_provides_content_type(GstOpkInfer *self, std::string_view contentType) {
    const auto members = gst_opkinfer_get_members(self);
    if (members == nullptr)
        return false;
    const auto providedContentTypes = members->opChain.getProvidedContentTypes();
    return std::ranges::find(providedContentTypes, contentType) != providedContentTypes.end();
}

static void gst_opkinfer_activate_for_content_requirement(GstOpkInfer *self) {
    if (gst_opkinfer_is_active(self))
        gst_opkinfer_emit_content_requirements(self);
    else
        g_object_set(self, "active", TRUE, nullptr);
}

static void gst_opkinfer_set_content_types(GstStructure *structure,
                                           std::string_view field,
                                           const std::vector<std::string_view> &contentTypes) {
    std::vector<std::string> strings(contentTypes.begin(), contentTypes.end());
    std::vector<const gchar *> values;
    values.reserve(strings.size() + 1);
    for (const auto &contentType : strings)
        values.push_back(contentType.c_str());
    values.push_back(nullptr);
    gst_structure_set(structure, field.data(), G_TYPE_STRV, values.data(), nullptr);
}

static void gst_opkinfer_push_model_registration(GstOpkInfer *self) {
    const auto members = gst_opkinfer_get_members(self);
    if (members == nullptr)
        return;

    GstPad *sourcePad = gst_element_get_static_pad(GST_ELEMENT(self), "src");
    if (sourcePad == nullptr)
        return;

    GstStructure *structure = gst_structure_new("opk-model-register",
                                                "model-name",
                                                G_TYPE_STRING,
                                                members->opChain.getName().c_str(),
                                                "element-name",
                                                G_TYPE_STRING,
                                                GST_OBJECT_NAME(self),
                                                "active",
                                                G_TYPE_BOOLEAN,
                                                gst_opkinfer_is_active(self),
                                                nullptr);
    if (!members->opChain.getDisplayName().empty()) {
        gst_structure_set(structure,
                          "display-name",
                          G_TYPE_STRING,
                          members->opChain.getDisplayName().c_str(),
                          nullptr);
    }
    if (!members->opChain.getTask().empty()) {
        gst_structure_set(
            structure, "task", G_TYPE_STRING, members->opChain.getTask().c_str(), nullptr);
    }
    if (!members->opChain.getRuntime().empty()) {
        gst_structure_set(
            structure, "runtime", G_TYPE_STRING, members->opChain.getRuntime().c_str(), nullptr);
    }
    gst_opkinfer_set_content_types(
        structure, "provided-content-types", members->opChain.getProvidedContentTypes());
    gst_opkinfer_set_content_types(
        structure, "required-content-types", members->opChain.getRequiredContentTypes());
    gst_pad_push_event(sourcePad, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, structure));
    gst_object_unref(sourcePad);
}

static void gst_opkinfer_reset_qos_unlocked(GstOpkInfer *self) {
    self->qosEarliestTime = GST_CLOCK_TIME_NONE;
    self->processingSkipFrames = 0;
    self->qosProportion = 1.0;
    self->qosTimestamp = GST_CLOCK_TIME_NONE;
    self->qosProcessed = 0;
    self->qosDropped = 0;
    ++self->qosGeneration;
}

static void gst_opkinfer_reset_qos(GstOpkInfer *self) {
    GST_OBJECT_LOCK(self);
    gst_opkinfer_reset_qos_unlocked(self);
    GST_OBJECT_UNLOCK(self);
}

static bool gst_opkinfer_is_yuv_format(GstVideoFormat format) {
    return format == GST_VIDEO_FORMAT_I420 || format == GST_VIDEO_FORMAT_NV12 ||
           format == GST_VIDEO_FORMAT_YUY2;
}

static bool gst_opkinfer_has_supported_or_defaultable_yuv_colorimetry(const GstVideoInfo &info) {
    const GstVideoColorimetry colorimetry = GST_VIDEO_INFO_COLORIMETRY(&info);

    switch (colorimetry.matrix) {
    case GST_VIDEO_COLOR_MATRIX_UNKNOWN:
    case GST_VIDEO_COLOR_MATRIX_BT601:
    case GST_VIDEO_COLOR_MATRIX_BT709:
    case GST_VIDEO_COLOR_MATRIX_BT2020:
        break;
    default:
        return false;
    }

    switch (colorimetry.range) {
    case GST_VIDEO_COLOR_RANGE_UNKNOWN:
    case GST_VIDEO_COLOR_RANGE_0_255:
    case GST_VIDEO_COLOR_RANGE_16_235:
        return true;
    default:
        return false;
    }
}

static gboolean gst_opkinfer_start(GstBaseTransform *selfInParentType) {
    auto *self = reinterpret_cast<GstOpkInfer *>(selfInParentType);

    gst_opkinfer_reset_qos(self);

    auto members = std::make_unique<GstOpkInferMembers>();

    if (!self->opChainPath || !self->opChainPath[0]) {
        GST_ELEMENT_ERROR(
            self, RESOURCE, SETTINGS, ("opchain-path is mandatory but not set"), (nullptr));
        return FALSE;
    }

    if (auto setupResult = members->setupOpChainFromJson(self->opChainPath); !setupResult) {
        opk::log::error("Error while setting up op-chain [{}]: {}\n",
                        self->opChainPath,
                        setupResult.error().toString());

        GST_ELEMENT_ERROR(self,
                          RESOURCE,
                          FAILED,
                          ("Failed to setup op-chain: %s", setupResult.error().info.c_str()),
                          ("%s", self->opChainPath));

        return FALSE;
    }

    GST_OBJECT_LOCK(self);
    self->runtimeMembers = std::move(members);
    GST_OBJECT_UNLOCK(self);

    gst_opkinfer_push_model_registration(self);

    return TRUE;
}

static gboolean gst_opkinfer_stop(GstBaseTransform *selfInParentType) {
    auto *self = reinterpret_cast<GstOpkInfer *>(selfInParentType);
    gst_opkinfer_reset_qos(self);
    std::shared_ptr<GstOpkInferMembers> retiredMembers;
    GST_OBJECT_LOCK(self);
    retiredMembers.swap(self->runtimeMembers);
    GST_OBJECT_UNLOCK(self);
    return TRUE;
}

static GstStateChangeReturn gst_opkinfer_change_state(GstElement *selfInParentType,
                                                      GstStateChange transition) {
    const auto result =
        GST_ELEMENT_CLASS(gst_opkinfer_parent_class)->change_state(selfInParentType, transition);
    if (result != GST_STATE_CHANGE_FAILURE && transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING &&
        gst_opkinfer_is_active(GST_OPKINFER(selfInParentType))) {
        gst_opkinfer_emit_content_requirements(GST_OPKINFER(selfInParentType));
    }
    return result;
}

static GstCaps *gst_opkinfer_transform_caps(GstBaseTransform *selfInParentType,
                                            GstPadDirection direction,
                                            GstCaps *caps,
                                            GstCaps *filter) {
    const bool copyVideo = g_atomic_int_get(&GST_OPKINFER(selfInParentType)->copyInput);
    GstCaps *result = gst_caps_is_any(caps) ? gst_caps_copy(caps) : gst_caps_new_empty();
    for (guint capsIndex = 0; capsIndex < gst_caps_get_size(caps); ++capsIndex) {
        GstCaps *const entry = gst_caps_copy_nth(caps, capsIndex);
        if (gst_structure_has_name(gst_caps_get_structure(entry, 0), "video/x-raw")) {
            gst_caps_append(result,
                            gst::Tools::transformVideoCaps(direction, entry, nullptr, copyVideo));
            gst_caps_unref(entry);
        } else {
            // copy-input changes video memory negotiation only. Audio retains its caps.
            gst_caps_append(result, entry);
        }
    }
    if (filter != nullptr) {
        GstCaps *const intersection =
            gst_caps_intersect_full(filter, result, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(result);
        result = intersection;
    }
    return result;
}

static gboolean
gst_opkinfer_set_caps(GstBaseTransform *selfInParentType, GstCaps *inCaps, GstCaps *outCaps) {
    auto *self = reinterpret_cast<GstOpkInfer *>(selfInParentType);
    (void)outCaps;

    self->isAudio = FALSE;
    if (self->runtimeMembers)
        self->runtimeMembers->resetAudio();
    if (gst_structure_has_name(gst_caps_get_structure(inCaps, 0), "audio/x-raw")) {
        if (!gst_audio_info_from_caps(&self->audioInfo, inCaps) ||
            !opk::mediaio::gst::GstAudioFrame::supports(self->audioInfo)) {
            GST_ERROR_OBJECT(self, "Unsupported raw audio caps");
            return FALSE;
        }
        if (!self->runtimeMembers)
            return FALSE;
        const auto required = self->runtimeMembers->opChain.getRequiredAudioSamples();
        if (!required) {
            GST_ERROR_OBJECT(self, "%s", required.error().toString().c_str());
            return FALSE;
        }
        const size_t window = *required;
        const size_t hopSamples = self->audioHopSamples ? self->audioHopSamples : window;
        if (!window || window > GstOpkInferMembers::audioBufferCapacity || hopSamples > window) {
            GST_ERROR_OBJECT(self,
                             "Audio requires a PCM window within ring capacity and hop <= window");
            return FALSE;
        }
        self->runtimeMembers->audioWindowSamples = window;
        self->runtimeMembers->audioHopSamples = hopSamples;
        self->isAudio = TRUE;
        return TRUE;
    }

    if (!gst_video_info_from_caps(&self->videoInfo, inCaps)) {
        GST_ERROR_OBJECT(self, "Failed to parse input caps");
        return FALSE;
    }

    const auto format = GST_VIDEO_INFO_FORMAT(&self->videoInfo);
    if (format != GST_VIDEO_FORMAT_BGRA && format != GST_VIDEO_FORMAT_RGB &&
        format != GST_VIDEO_FORMAT_I420 && format != GST_VIDEO_FORMAT_NV12 &&
        format != GST_VIDEO_FORMAT_YUY2) {
        GST_ERROR_OBJECT(self, "Unsupported format (expected BGRA, RGB, I420, NV12, or YUY2)");
        return FALSE;
    }

    if (gst_opkinfer_is_yuv_format(format) &&
        !gst_opkinfer_has_supported_or_defaultable_yuv_colorimetry(self->videoInfo)) {
        const GstVideoColorimetry colorimetry = GST_VIDEO_INFO_COLORIMETRY(&self->videoInfo);
        GST_ERROR_OBJECT(self,
                         "Unsupported YUV colorimetry/range (matrix=%d, range=%d)",
                         static_cast<int>(colorimetry.matrix),
                         static_cast<int>(colorimetry.range));
        return FALSE;
    }

    return TRUE;
}

static GstClockTime gst_opkinfer_saturating_add(GstClockTime timestamp, GstClockTime duration) {
    return duration >= GST_CLOCK_TIME_NONE - timestamp ? GST_CLOCK_TIME_NONE - 1
                                                       : timestamp + duration;
}

static gboolean gst_opkinfer_src_event(GstBaseTransform *selfInParentType, GstEvent *event) {
    auto *self = GST_OPKINFER(selfInParentType);
    const auto eventType = GST_EVENT_TYPE(event);

    if (eventType == GST_EVENT_CUSTOM_UPSTREAM) {
        const GstStructure *structure = gst_event_get_structure(event);
        if (structure != nullptr &&
            gst_structure_has_name(structure, opk::content_requirement_event::k_name.data())) {
            const gchar *contentType = gst_structure_get_string(
                structure, opk::content_requirement_event::k_content_type_field.data());
            if (contentType != nullptr && gst_opkinfer_provides_content_type(self, contentType))
                gst_opkinfer_activate_for_content_requirement(self);
        }
    }

    if (eventType == GST_EVENT_QOS) {
        GST_OBJECT_LOCK(self);
        const gboolean handleQos = self->qosEnabled && self->active;
        const guint64 qosGeneration = self->qosGeneration;
        GST_OBJECT_UNLOCK(self);

        if (!handleQos)
            return GST_BASE_TRANSFORM_CLASS(gst_opkinfer_parent_class)
                ->src_event(selfInParentType, event);

        GstQOSType qosType = GST_QOS_TYPE_UNDERFLOW;
        gdouble proportion = 1.0;
        GstClockTimeDiff qosJitter = 0;
        GstClockTime timestamp = GST_CLOCK_TIME_NONE;
        gst_event_parse_qos(event, &qosType, &proportion, &qosJitter, &timestamp);

        GstClockTime qosEarliestTime = GST_CLOCK_TIME_NONE;
        if (qosType == GST_QOS_TYPE_UNDERFLOW && qosJitter > 0 &&
            GST_CLOCK_TIME_IS_VALID(timestamp)) {
            const auto lateness = static_cast<GstClockTime>(qosJitter);
            qosEarliestTime = gst_opkinfer_saturating_add(timestamp, lateness);
        }

        GST_OBJECT_LOCK(self);
        const gboolean publishQos =
            self->qosEnabled && self->active && qosGeneration == self->qosGeneration;
        if (publishQos) {
            self->qosEarliestTime = qosEarliestTime;
            self->qosProportion = proportion;
            self->qosTimestamp = timestamp;
            ++self->qosAcceptedEventsDebug;
        }
        GST_OBJECT_UNLOCK(self);

        if (!publishQos)
            return GST_BASE_TRANSFORM_CLASS(gst_opkinfer_parent_class)
                ->src_event(selfInParentType, event);

        g_object_notify(G_OBJECT(self), "qos-accepted-events-debug");

        GST_DEBUG_OBJECT(self,
                         "Received QoS event: type=%d proportion=%f diff=%" G_GINT64_FORMAT
                         " timestamp=%" G_GUINT64_FORMAT,
                         qosType,
                         proportion,
                         qosJitter,
                         timestamp);

        // This element owns the QoS policy for inference. Consuming the event
        // prevents upstream decoders from dropping the video buffer itself.
        gst_event_unref(event);
        return TRUE;
    }

    // Unrelated events keep the native path.
    return GST_BASE_TRANSFORM_CLASS(gst_opkinfer_parent_class)->src_event(selfInParentType, event);
}

static gboolean gst_opkinfer_sink_event(GstBaseTransform *selfInParentType, GstEvent *event) {
    const auto eventType = GST_EVENT_TYPE(event);
    switch (eventType) {
    case GST_EVENT_STREAM_START:
        if (const auto members = gst_opkinfer_get_members(GST_OPKINFER(selfInParentType))) {
            members->resetAudio();
            members->inputIsLive.reset();
        }
        if (gst_opkinfer_is_active(GST_OPKINFER(selfInParentType)))
            gst_opkinfer_emit_content_requirements(GST_OPKINFER(selfInParentType));
        break;
    case GST_EVENT_SEGMENT:
    case GST_EVENT_GAP:
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
        // FLUSH_START can arrive concurrently with processing. Clear only on
        // serialized stream events or FLUSH_STOP, once processing has stopped.
        if (eventType != GST_EVENT_FLUSH_START) {
            if (const auto members = gst_opkinfer_get_members(GST_OPKINFER(selfInParentType)))
                members->resetAudio();
        }
        gst_opkinfer_reset_qos(GST_OPKINFER(selfInParentType));
        break;
    default:
        break;
    }
    return GST_BASE_TRANSFORM_CLASS(gst_opkinfer_parent_class)->sink_event(selfInParentType, event);
}

struct GstOpkInferFramePolicy {
    gboolean qosEnabled;
    gboolean skipInference;
    gdouble qosProportion;
    GstClockTimeDiff qosJitter;
    GstClockTime qosTimestamp;
    guint64 qosGeneration;
};

static GstOpkInferFramePolicy gst_opkinfer_get_frame_policy(GstOpkInfer *self,
                                                            GstClockTime runningTime) {
    GST_OBJECT_LOCK(self);
    const gboolean qosEnabled = self->qosEnabled;
    const gboolean qosSkip = GST_CLOCK_TIME_IS_VALID(runningTime) && qosEnabled &&
                             GST_CLOCK_TIME_IS_VALID(self->qosEarliestTime) &&
                             runningTime <= self->qosEarliestTime;
    const gboolean processingSkip = qosEnabled && self->processingSkipFrames > 0;
    if (processingSkip)
        --self->processingSkipFrames;
    if (GST_CLOCK_TIME_IS_VALID(runningTime) && GST_CLOCK_TIME_IS_VALID(self->qosEarliestTime) &&
        runningTime > self->qosEarliestTime) {
        self->qosEarliestTime = GST_CLOCK_TIME_NONE;
    }
    const GstOpkInferFramePolicy policy{qosEnabled,
                                        qosSkip || processingSkip,
                                        qosSkip ? self->qosProportion : 1.0,
                                        qosSkip ? GST_CLOCK_DIFF(runningTime, self->qosEarliestTime)
                                                : 0,
                                        self->qosTimestamp,
                                        self->qosGeneration};
    GST_OBJECT_UNLOCK(self);
    return policy;
}

static std::shared_ptr<opk::mediaio::VideoFrame>
gst_opkinfer_retain_video_frame(GstOpkInfer *self, GstBuffer *buffer) {
    const bool dmaBuf = opk::mediaio::gst::GstVideoFrame::hasDmaBufContent(buffer);
    if (dmaBuf || opk::mediaio::gst::GstVideoFrame::hasDirectCpuAddress(buffer)) {
        auto frame{dmaBuf ? opk::mediaio::gst::GstVideoFrame::retainDmaBuf(buffer, self->videoInfo)
                          : opk::mediaio::gst::GstVideoFrame::mapGstBuffer(
                                buffer, self->videoInfo, opk::AccessMode::Read)};
        if (!frame) {
            GST_ELEMENT_ERROR(self,
                              RESOURCE,
                              FAILED,
                              ("Failed to retain video buffer for preprocessing."),
                              ("%s: %s",
                               self->opChainPath,
                               dmaBuf ? "DMA-BUF input has invalid video information."
                                      : "System-memory video mapping failed."));
        }
        return frame;
    }

    GST_ELEMENT_ERROR(
        self, RESOURCE, FAILED, ("Unsupported GstBuffer memory type."), ("%s", self->opChainPath));
    return nullptr;
}

static GstFlowReturn gst_opkinfer_prepare_output_buffer(GstBaseTransform *const selfInParentType,
                                                        GstBuffer *const inputBuffer,
                                                        GstBuffer **const outputBuffer) {
    auto *const self = GST_OPKINFER(selfInParentType);
    if (self->isAudio)
        return GST_BASE_TRANSFORM_CLASS(gst_opkinfer_parent_class)
            ->prepare_output_buffer(selfInParentType, inputBuffer, outputBuffer);
    return gst::Tools::prepareVideoBuffer(
        selfInParentType, inputBuffer, outputBuffer, g_atomic_int_get(&self->copyInput));
}

/// Execute one scheduled PCM window against writable result metadata.
static GstFlowReturn
gst_opkinfer_process_audio_window(GstOpkInfer *self,
                                  const opk::PcmRingBuffer::View &window,
                                  open_perception_kit::FrameResults &frameResults,
                                  std::string &opChainError) {
    auto &members = *self->runtimeMembers;
    opk::op::OpChainContext context;
    context.frameResults = &frameResults;
    context.inferenceInfo.inferElementId = gst_opkinfer_get_effective_inferId(self);
    context.pcmViews.emplace("pipelineAudioData", window);
    auto &audio = context.inferenceInfo.audio;
    audio.discontinuity = members.audioDiscontinuity;
    audio.hopSampleCount = members.audioHopSamples;
    audio.windowStartSample = window.beginPosition();
    audio.windowEndSample = window.endPosition();
    if (members.audioOriginTimestampNs) {
        const auto delayNs = members.pcmAudioBuffer->resamplingDelayNs();
        auto timestamp = [&](uint64_t samplePosition) -> std::optional<uint64_t> {
            if (samplePosition > G_MAXUINT64 / 62500)
                return std::nullopt;
            const uint64_t offset = samplePosition * 62500;
            const uint64_t origin = *members.audioOriginTimestampNs;
            if (offset > G_MAXUINT64 - origin || origin + offset < delayNs)
                return std::nullopt;
            return origin + offset - delayNs;
        };
        const auto start = timestamp(audio.windowStartSample);
        const auto end = timestamp(audio.windowEndSample);
        if (start && end) {
            audio.windowStartTimestampNs = start;
            audio.windowEndTimestampNs = end;
        }
    }
    const auto execution = members.executeOpChain(context, GST_BASE_TRANSFORM_SINK_PAD(self));
    if (!execution) {
        opChainError = execution.error().info;
        GST_ERROR_OBJECT(self, "%s", execution.error().toString().c_str());
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

/// Execute one mapped video frame against writable result metadata.
static GstFlowReturn
gst_opkinfer_process_video_frame(GstOpkInfer *self,
                                 const std::shared_ptr<opk::mediaio::VideoFrame> &sharedMediaFrame,
                                 open_perception_kit::FrameResults &frameResults,
                                 std::string &opChainError) {
    // Execute the op-chain with the provided context. The chain can read and mutate the
    // frame results and read the video frame, but not mutate the frame.
    opk::op::OpChainContext opChainContext;
    opChainContext.inferenceInfo.inferElementId =
        std::string(gst_opkinfer_get_effective_inferId(self));

    opChainContext.frameResults = &frameResults;
    opChainContext.videoFrames["pipelineVideoFrame"] = sharedMediaFrame;

    auto executeResult =
        self->runtimeMembers->executeOpChain(opChainContext, GST_BASE_TRANSFORM_SINK_PAD(self));
    if (!executeResult) {
        opChainError = executeResult.error().info;
        opk::log::error("{}\n", executeResult.error().toString());
        return GST_FLOW_CUSTOM_ERROR;
    }

    return GST_FLOW_OK;
}

/// Convert incoming audio and execute all complete windows in chronological order.
static GstFlowReturn gst_opkinfer_process_audio_buffer(GstOpkInfer *self, GstBuffer *buffer) {
    if (!self->runtimeMembers)
        return GST_FLOW_OK;
    if (!gst_opkinfer_is_active(self)) {
        self->runtimeMembers->resetAudio();
        return GST_FLOW_OK;
    }
    if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT) ||
        GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_GAP)) {
        self->runtimeMembers->resetAudio();
        if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_GAP))
            return GST_FLOW_OK;
    }
    // Anchor sample zero to source PTS, not arrival/processing time. Missing
    // initial PTS can be recovered from a later timestamp on the same stream.
    auto frame = opk::mediaio::gst::GstAudioFrame::mapGstBuffer(buffer, self->audioInfo);
    if (!frame) {
        GST_ELEMENT_ERROR(self, RESOURCE, READ, ("Failed to map PCM audio frame"), (nullptr));
        return GST_FLOW_ERROR;
    }
    const auto sampleRate = static_cast<uint64_t>(frame->sampleRate());
    auto elapsedNs =
        gst_util_uint64_scale(self->runtimeMembers->audioSourceFrames, GST_SECOND, sampleRate);
    if (frame->timestampNs() != opk::mediaio::InvalidTimestampNs) {
        const uint64_t presentationTimestampNs = frame->timestampNs();
        if (self->runtimeMembers->audioOriginTimestampNs &&
            elapsedNs <= G_MAXUINT64 - *self->runtimeMembers->audioOriginTimestampNs) {
            const auto expected = *self->runtimeMembers->audioOriginTimestampNs + elapsedNs;
            const auto difference = presentationTimestampNs > expected
                                        ? presentationTimestampNs - expected
                                        : expected - presentationTimestampNs;
            // Permit timestamp rounding of one source sample, but never stitch a gap.
            if (difference > (GST_SECOND + sampleRate - 1) / sampleRate) {
                self->runtimeMembers->resetAudio();
                elapsedNs = 0;
            }
        }
        if (!self->runtimeMembers->audioOriginTimestampNs && presentationTimestampNs >= elapsedNs)
            self->runtimeMembers->audioOriginTimestampNs = presentationTimestampNs - elapsedNs;
    }
    const auto sourceFrames = frame->sampleCount();
    bool inputBuffered = false;
    try {
        if (!self->runtimeMembers->pcmAudioBuffer)
            self->runtimeMembers->pcmAudioBuffer =
                std::make_unique<opk::PcmRingBuffer>(GstOpkInferMembers::audioBufferCapacity);
        inputBuffered = sourceFrames <= G_MAXUINT64 - self->runtimeMembers->audioSourceFrames &&
                        self->runtimeMembers->pcmAudioBuffer->feed(*frame);
    } catch (const std::exception &error) {
        GST_ERROR_OBJECT(self, "Audio buffering failed: %s", error.what());
    }
    frame.reset(); // Release the input mapping before mutating downstream metadata.
    if (!inputBuffered) {
        GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("Failed to buffer input PCM audio"), (nullptr));
        return GST_FLOW_ERROR;
    }
    auto &pcmBuffer = *self->runtimeMembers->pcmAudioBuffer;
    self->runtimeMembers->audioSourceFrames += sourceFrames;
    GST_LOG_OBJECT(self,
                   "Buffered audio: retained=%zu end=%" G_GUINT64_FORMAT,
                   pcmBuffer.size(),
                   pcmBuffer.endPosition());
    auto &members = *self->runtimeMembers;
    if (members.nextAudioPosition < pcmBuffer.beginPosition()) {
        GST_ELEMENT_ERROR(self,
                          STREAM,
                          FAILED,
                          ("Audio input overwrote unprocessed samples; reduce input buffer size"),
                          (nullptr));
        return GST_FLOW_ERROR;
    }
    if (!opk::FrameResultsMeta::get(buffer)) {
        if (!opk::FrameResultsMeta::add(buffer,
                                        std::make_shared<open_perception_kit::FrameResults>()))
            return GST_FLOW_ERROR;
    }
    // Consume complete windows in order, including multiple windows from one input buffer.
    // Audio scheduling deliberately bypasses the video QoS frame-dropping policy.
    while (pcmBuffer.endPosition() - members.nextAudioPosition >= members.audioWindowSamples) {
        const auto window = pcmBuffer.window(members.nextAudioPosition, members.audioWindowSamples);
        if (!window)
            return GST_FLOW_ERROR;
        std::string opChainError;
        const auto result = opk::FrameResultsMeta::mutate<GstFlowReturn>(
            buffer, [self, window = *window, &opChainError](auto &frameResults) {
                return gst_opkinfer_process_audio_window(self, window, frameResults, opChainError);
            });
        if (!std::holds_alternative<GstFlowReturn>(result) ||
            std::get<GstFlowReturn>(result) != GST_FLOW_OK) {
            GST_ELEMENT_ERROR(self,
                              STREAM,
                              FAILED,
                              ("Audio OpChain execution failed: %s",
                               std::holds_alternative<opk::MetaError>(result)
                                   ? "Failed to update FrameResults metadata."
                                   : opChainError.c_str()),
                              ("%s", self->opChainPath));
            return GST_FLOW_ERROR;
        }
        members.nextAudioPosition += members.audioHopSamples;
        members.audioDiscontinuity = false;
    }
    return GST_FLOW_OK;
}

/// Apply video QoS policy, map the frame, and run inference when scheduled.
static GstFlowReturn gst_opkinfer_process_video_buffer(GstOpkInfer *self, GstBuffer *buffer) {
    if (!gst_opkinfer_is_active(self))
        return GST_FLOW_OK;

    // Keep the downstream metadata contract even when QoS skips inference.
    if (auto frameResultsMeta = opk::FrameResultsMeta::get(buffer); !frameResultsMeta) {
        auto frameResults = std::make_shared<open_perception_kit::FrameResults>();
        if (!opk::FrameResultsMeta::add(buffer, frameResults)) {
            GST_WARNING_OBJECT(self, "Failed to attach FrameResultsMeta");
            return GST_FLOW_OK;
        }
    }

    const GstClockTime runningTime = GST_BUFFER_PTS_IS_VALID(buffer)
                                         ? gst_segment_to_running_time(&self->parent.segment,
                                                                       GST_FORMAT_TIME,
                                                                       GST_BUFFER_PTS(buffer))
                                         : GST_CLOCK_TIME_NONE;

    const auto framePolicy = gst_opkinfer_get_frame_policy(self, runningTime);

    if (framePolicy.skipInference) {
        GST_OBJECT_LOCK(self);
        if (framePolicy.qosGeneration != self->qosGeneration) {
            GST_OBJECT_UNLOCK(self);
            return GST_FLOW_OK;
        }
        const guint64 processed = self->qosProcessed;
        const guint64 dropped = ++self->qosDropped;
        GST_OBJECT_UNLOCK(self);

        // Report the QoS action while returning OK so the video buffer still flows.
        GstMessage *message = gst_message_new_qos(
            GST_OBJECT(self),
            FALSE,
            runningTime,
            gst_segment_to_stream_time(
                &self->parent.segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer)),
            GST_BUFFER_PTS_IS_VALID(buffer) ? GST_BUFFER_PTS(buffer) : framePolicy.qosTimestamp,
            GST_BUFFER_DURATION(buffer));
        gst_message_set_qos_values(
            message, framePolicy.qosJitter, framePolicy.qosProportion, GST_FORMAT_PERCENT_MAX);
        gst_message_set_qos_stats(message, GST_FORMAT_BUFFERS, processed, dropped);
        gst_element_post_message(GST_ELEMENT(self), message);
        return GST_FLOW_OK;
    }

    if (!self->runtimeMembers)
        return GST_FLOW_OK;

    const GstClockTime processingStartedAt =
        framePolicy.qosEnabled ? gst_util_get_timestamp() : GST_CLOCK_TIME_NONE;

    // Retain after attaching FrameResultsMeta: the extra reference prevents adding metadata.
    auto sharedMediaFrame{gst_opkinfer_retain_video_frame(self, buffer)};
    if (!sharedMediaFrame) {
        return GST_FLOW_ERROR;
    }

    // Keep the frame and its metadata alive throughout the op-chain execution.
    std::string opChainError;
    auto mutationResult = opk::FrameResultsMeta::mutate<GstFlowReturn>(
        buffer, [self, sharedMediaFrame, &opChainError](auto &frameResults) {
            return gst_opkinfer_process_video_frame(
                self, sharedMediaFrame, frameResults, opChainError);
        });

    // Collapse op-chain failures and metadata mutation failures into a GStreamer
    // element error so the pipeline fails consistently.
    if ((std::holds_alternative<GstFlowReturn>(mutationResult) &&
         std::get<GstFlowReturn>(mutationResult) != GST_FLOW_OK) ||
        std::holds_alternative<opk::MetaError>(mutationResult)) {
        GST_ELEMENT_ERROR(self,
                          RESOURCE,
                          FAILED,
                          ("Error while executing op-chain: %s",
                           std::holds_alternative<opk::MetaError>(mutationResult)
                               ? "Failed to update FrameResults metadata."
                               : opChainError.c_str()),
                          ("%s", self->opChainPath));

        return GST_FLOW_ERROR;
    }

    const GstClockTime processingLatency =
        framePolicy.qosEnabled ? gst_util_get_timestamp() - processingStartedAt : 0;
    GstClockTime frameDuration = GST_BUFFER_DURATION(buffer);
    if (GST_VIDEO_INFO_FPS_N(&self->videoInfo) > 0 && GST_VIDEO_INFO_FPS_D(&self->videoInfo) > 0) {
        frameDuration = gst_util_uint64_scale(GST_SECOND,
                                              GST_VIDEO_INFO_FPS_D(&self->videoInfo),
                                              GST_VIDEO_INFO_FPS_N(&self->videoInfo));
    }
    GST_OBJECT_LOCK(self);
    const gboolean publishProcessing = framePolicy.qosEnabled && self->qosEnabled && self->active &&
                                       framePolicy.qosGeneration == self->qosGeneration;
    if (publishProcessing) {
        ++self->qosProcessed;
        if (GST_CLOCK_TIME_IS_VALID(frameDuration) && frameDuration > 0 &&
            processingLatency > frameDuration) {
            // Integer division already rounds down for nonmultiples. Subtracting one nanosecond
            // only changes exact multiples, treating a frame due exactly at completion as on time.
            self->processingSkipFrames = (processingLatency - 1) / frameDuration;
        }
    }
    GST_OBJECT_UNLOCK(self);

    return GST_FLOW_OK;
}

static GstFlowReturn gst_opkinfer_transform_ip(GstBaseTransform *selfInParentType,
                                               GstBuffer *buffer) {
    auto *self = GST_OPKINFER(selfInParentType);
    if (self->isAudio)
        return gst_opkinfer_process_audio_buffer(self, buffer);
    return gst_opkinfer_process_video_buffer(self, buffer);
}

// ---------------- properties & class init ----------------

constexpr guint PROP_OPCHAIN_PATH = 1;
constexpr guint PROP_MODEL_ACTIVE = 2;
constexpr guint PROP_INFER_ID = 4;
constexpr guint PROP_QOS_ENABLED = 5;
constexpr guint PROP_QOS_ACCEPTED_EVENTS_DEBUG = 6;
constexpr guint PROP_COPY_INPUT = 7;
constexpr guint PROP_AUDIO_HOP_SAMPLES = 8;

static void gst_opkinfer_set_property(GObject *selfInParentType,
                                      guint propertyId,
                                      const GValue *value,
                                      GParamSpec *propertySpec) {
    auto *self = reinterpret_cast<GstOpkInfer *>(selfInParentType);
    const gst::PropertyGuard propertyGuard(GST_ELEMENT(self), propertySpec);
    if (!propertyGuard.isChangeAllowed())
        return;
    switch (propertyId) {
    case PROP_COPY_INPUT:
        g_atomic_int_set(&self->copyInput, g_value_get_boolean(value));
        gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(self));
        break;
    case PROP_OPCHAIN_PATH:
        GST_OBJECT_LOCK(self);
        g_free(self->opChainPath);
        self->opChainPath = g_value_dup_string(value);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_MODEL_ACTIVE: {
        gboolean activeChanged = FALSE;
        gboolean emitContentRequirements = FALSE;
        const gboolean active = g_value_get_boolean(value);
        GST_OBJECT_LOCK(self);
        activeChanged = self->active != active;
        emitContentRequirements = !self->active && active;
        self->active = active;
        if (!self->active)
            gst_opkinfer_reset_qos_unlocked(self);
        GST_OBJECT_UNLOCK(self);
        if (activeChanged)
            gst_opkinfer_push_model_registration(self);
        if (emitContentRequirements)
            gst_opkinfer_emit_content_requirements(self);
        break;
    }
    case PROP_INFER_ID:
        GST_OBJECT_LOCK(self);
        g_free(self->inferId);
        self->inferId = g_value_dup_string(value);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_AUDIO_HOP_SAMPLES:
        GST_OBJECT_LOCK(self);
        self->audioHopSamples = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_QOS_ENABLED:
        GST_OBJECT_LOCK(self);
        self->qosEnabled = g_value_get_boolean(value);
        if (!self->qosEnabled)
            gst_opkinfer_reset_qos_unlocked(self);
        GST_OBJECT_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(selfInParentType, propertyId, propertySpec);
    }
}

static void gst_opkinfer_get_property(GObject *selfInParentType,
                                      guint propertyId,
                                      GValue *value,
                                      GParamSpec *propertySpec) {
    auto *self = reinterpret_cast<GstOpkInfer *>(selfInParentType);
    switch (propertyId) {
    case PROP_COPY_INPUT:
        g_value_set_boolean(value, g_atomic_int_get(&self->copyInput));
        break;
    case PROP_OPCHAIN_PATH:
        GST_OBJECT_LOCK(self);
        g_value_set_string(value, self->opChainPath);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_MODEL_ACTIVE:
        g_value_set_boolean(value, gst_opkinfer_is_active(self));
        break;
    case PROP_INFER_ID:
        GST_OBJECT_LOCK(self);
        g_value_set_string(value, gst_opkinfer_get_effective_inferId(self));
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_AUDIO_HOP_SAMPLES:
        GST_OBJECT_LOCK(self);
        g_value_set_uint(value, self->audioHopSamples);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_QOS_ENABLED:
        GST_OBJECT_LOCK(self);
        g_value_set_boolean(value, self->qosEnabled);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_QOS_ACCEPTED_EVENTS_DEBUG:
        GST_OBJECT_LOCK(self);
        g_value_set_uint64(value, self->qosAcceptedEventsDebug);
        GST_OBJECT_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(selfInParentType, propertyId, propertySpec);
    }
}

static void gst_opkinfer_finalize(GObject *selfInParentType) {
    auto *self = reinterpret_cast<GstOpkInfer *>(selfInParentType);

    std::destroy_at(&self->runtimeMembers);
    g_clear_pointer(&self->opChainPath, g_free);
    g_clear_pointer(&self->inferId, g_free);

    G_OBJECT_CLASS(gst_opkinfer_parent_class)->finalize(selfInParentType);
}

static void gst_opkinfer_class_init(GstOpkInferClass *inferClass) {
    GObjectClass *objectClass = G_OBJECT_CLASS(inferClass);
    GstElementClass *elementClass = GST_ELEMENT_CLASS(inferClass);
    GstBaseTransformClass *baseTransformClass = GST_BASE_TRANSFORM_CLASS(inferClass);

    objectClass->set_property = gst_opkinfer_set_property;
    objectClass->get_property = gst_opkinfer_get_property;
    objectClass->finalize = gst_opkinfer_finalize;

    g_object_class_install_property(
        objectClass,
        PROP_OPCHAIN_PATH,
        g_param_spec_string(
            "opchain-path",
            "OpChain path",
            "Path to OpChain setup JSON"
            "; Set in NULL or READY; restart via READY required because the OpChain is loaded at "
            "start",
            nullptr,
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_READY)));

    g_object_class_install_property(
        objectClass,
        PROP_MODEL_ACTIVE,
        g_param_spec_boolean("active",
                             "Active",
                             "Do or not do"
                             "; Changeable while playing; model loading still happens at startup",
                             true,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        PROP_INFER_ID,
        g_param_spec_string(
            "infer-id",
            "ID of the inference element",
            "ID of the inference element (used in Plumber to identify the layers. "
            "Defaults to the name property of the element)"
            "; Set in NULL or READY; restart via READY required because result producer identity "
            "must remain stable while processing",
            "",
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_READY)));

    g_object_class_install_property(
        objectClass,
        PROP_QOS_ENABLED,
        g_param_spec_boolean("qos-enabled",
                             "QoS enabled",
                             "Enable experimental inference skipping from QoS feedback"
                             "; Changeable while playing; disabling resets inference skipping",
                             false,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        objectClass,
        PROP_QOS_ACCEPTED_EVENTS_DEBUG,
        g_param_spec_uint64("qos-accepted-events-debug",
                            "QoS accepted events debug",
                            "Debug-only count of QoS events committed by opkinfer",
                            0,
                            G_MAXUINT64,
                            0,
                            (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define OPKINFER_AUDIO_FORMATS "{U8,S16LE,S24LE,S32LE,F32LE}"
#else
#define OPKINFER_AUDIO_FORMATS "{U8,S16BE,S24LE,S32BE,F32BE}"
#endif
#define OPKINFER_AUDIO_CAPS                                                                        \
    "audio/x-raw, format=" OPKINFER_AUDIO_FORMATS                                                  \
    ", layout=interleaved, rate=[1,384000], channels=[1,64]"

    g_object_class_install_property(
        objectClass,
        PROP_AUDIO_HOP_SAMPLES,
        g_param_spec_uint(
            "audio-hop-samples",
            "Audio hop samples",
            "New mono 16 kHz samples between audio windows; 0 uses the PCM input window size"
            "; Set in NULL or READY; restart via READY required because audio window scheduling is "
            "configured during negotiation",
            0,
            G_MAXUINT,
            0,
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_READY)));

    g_object_class_install_property(
        objectClass,
        PROP_COPY_INPUT,
        g_param_spec_boolean("copy-input",
                             "Copy input",
                             "Copy video into CPU memory instead of retaining the input DMA-BUF"
                             "; Set in NULL or READY; restart via READY required because video "
                             "memory and caps are negotiated at startup",
                             FALSE,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_READY)));

    GstCaps *const caps = gst_caps_from_string(
        (std::string(gst::Tools::supportedVideoCaps) + "; " OPKINFER_AUDIO_CAPS).c_str());
    gst_element_class_add_pad_template(
        elementClass, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(
        elementClass, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
#undef OPKINFER_AUDIO_CAPS
#undef OPKINFER_AUDIO_FORMATS

    gst_element_class_set_static_metadata(elementClass,
                                          "OPK Inference",
                                          "Filter/Effect/Video",
                                          "ONNX Runtime inference",
                                          "Arm Limited <perception-fdbck@arm.com>");

    baseTransformClass->start = gst_opkinfer_start;
    baseTransformClass->stop = gst_opkinfer_stop;
    baseTransformClass->set_caps = gst_opkinfer_set_caps;
    baseTransformClass->transform_caps = gst_opkinfer_transform_caps;
    baseTransformClass->sink_event = gst_opkinfer_sink_event;
    baseTransformClass->src_event = gst_opkinfer_src_event;
    baseTransformClass->transform_ip = gst_opkinfer_transform_ip;
    baseTransformClass->prepare_output_buffer = gst_opkinfer_prepare_output_buffer;
    elementClass->change_state = gst_opkinfer_change_state;
}

static void gst_opkinfer_init(GstOpkInfer *self) {
    self->opChainPath = nullptr;
    self->active = true;
    self->copyInput = false;
    std::construct_at(&self->runtimeMembers);
    self->inferId = nullptr;
    self->qosEnabled = false;
    self->qosAcceptedEventsDebug = 0;
    self->qosEarliestTime = GST_CLOCK_TIME_NONE;
    self->processingSkipFrames = 0;
    self->qosProportion = 1.0;
    self->qosTimestamp = GST_CLOCK_TIME_NONE;
    self->qosProcessed = 0;
    self->qosDropped = 0;
    self->qosGeneration = 0;

    gst_video_info_init(&self->videoInfo);
    gst_audio_info_init(&self->audioInfo);
    self->isAudio = FALSE;
    self->audioHopSamples = 0;

    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(self), FALSE);
}

static gboolean opkinfer_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "opkinfer", GST_RANK_NONE, GST_TYPE_OPKINFER);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  opkinfer,
                  "OPK inference (YOLO + ONNX Runtime)",
                  opkinfer_plugin_init,
                  "1.0",
                  "Apache 2.0",
                  "opk-elements",
                  "https://www.arm.com/")

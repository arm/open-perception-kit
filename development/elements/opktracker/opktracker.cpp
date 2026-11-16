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

#include "gst/PropertyGuard.h"

#include "Tracker.h"

#include <gst/FrameResultsMeta.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "Log.h"
#include "gst/Tools.h"

#ifndef PACKAGE
#define PACKAGE "opk-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_OPKTRACKER (gst_opktracker_get_type())
G_DECLARE_FINAL_TYPE(GstOpkTracker, gst_opktracker, GST, OPKTRACKER, GstBaseTransform)

struct _GstOpkTracker {
    GstBaseTransform parent;

    GstVideoInfo videoInfo;

    gchar *contentType;
    gboolean useEmbeddings;
    gchar *embeddingContentType;
    gfloat embeddingWeight;
    gfloat minCosineSimilarity;
    gfloat reidReassociateThreshold;
    gfloat dormantTrackHistorySeconds;
    gfloat iouThreshold;
    gint maxMissedFrames;
    gint minHitsToConfirm;
    gboolean appendIdentityIdToText;
    gfloat traceHistorySeconds;
    gfloat kalmanDtFallback;
    gboolean kalmanDtForceFallback;
    gboolean kalmanDtFallbackActive;
    gfloat kalmanInitialCovariancePos;
    gfloat kalmanInitialCovarianceVel;
    gfloat kalmanProcessNoisePos;
    gfloat kalmanProcessNoiseVel;
    gfloat kalmanMeasurementNoisePos;
    gboolean useKalman;
    gboolean emitPredictedDetections;
    gboolean emitTrace;
    gchar *associationMode;

    gchar *inferId;

    struct Members;
    Members *runtimeMembers;
};

G_END_DECLS

struct _GstOpkTracker::Members {
    opk::tracker::Tracker tracker;
};

G_DEFINE_TYPE(GstOpkTracker, gst_opktracker, GST_TYPE_BASE_TRANSFORM)

namespace {

enum class PropertyId : guint {
    Reserved = 0,
    ContentType,
    UseEmbeddings,
    EmbeddingContentType,
    EmbeddingWeight,
    MinCosineSimilarity,
    ReidReassociateThreshold,
    DormantTrackHistorySeconds,
    IouThreshold,
    MaxMissedFrames,
    MinHitsToConfirm,
    AppendTrackIdToText,
    TraceHistorySeconds,
    KalmanDtFallback,
    KalmanDtForceFallback,
    KalmanInitialCovariancePos,
    KalmanInitialCovarianceVel,
    KalmanProcessNoisePos,
    KalmanProcessNoiseVel,
    KalmanMeasurementNoisePos,
    UseKalman,
    EmitPredictedDetections,
    EmitTrace,
    AssociationMode,
    InferId,
};

} // namespace

static opk::tracker::AssociationMode associationModeFromString(const gchar *modeText) {
    if (modeText == nullptr) {
        return opk::tracker::Defaults::associationMode;
    }

    const std::string mode = modeText;
    if (mode == "iou") {
        return opk::tracker::AssociationMode::Iou;
    }
    if (mode == "embedding") {
        return opk::tracker::AssociationMode::Embedding;
    }
    return opk::tracker::AssociationMode::Hybrid;
}

static const gchar *associationModeToString(opk::tracker::AssociationMode mode) {
    switch (mode) {
    case opk::tracker::AssociationMode::Iou:
        return "iou";
    case opk::tracker::AssociationMode::Embedding:
        return "embedding";
    case opk::tracker::AssociationMode::Hybrid:
        return "hybrid";
    }

    return "hybrid";
}

static const gchar *gst_opktracker_get_effective_inferId(const GstOpkTracker *self) {
    /* If user provided infer-id property, prefer it */
    if (self->inferId && self->inferId[0] != '\0')
        return self->inferId;

    /* Fallback to element name (always exists) */
    return GST_OBJECT_NAME(GST_ELEMENT(self));
}

static opk::tracker::Config trackerConfigFromElement(GstOpkTracker *const self) {
    g_autoptr(GMutexLocker) objectLock = g_mutex_locker_new(GST_OBJECT_GET_LOCK(self));
    opk::tracker::Config config;
    config.contentType =
        self->contentType ? self->contentType : opk::tracker::Defaults::contentType;
    config.useEmbeddings = self->useEmbeddings;
    config.embeddingContentType = self->embeddingContentType
                                      ? self->embeddingContentType
                                      : opk::tracker::Defaults::embeddingContentType;
    config.embeddingWeight = self->embeddingWeight;
    config.minCosineSimilarity = self->minCosineSimilarity;
    config.reidReassociateThreshold = self->reidReassociateThreshold;
    config.dormantTrackHistorySeconds = self->dormantTrackHistorySeconds;
    config.iouThreshold = self->iouThreshold;
    config.maxMissedFrames = self->maxMissedFrames;
    config.minHitsToConfirm = self->minHitsToConfirm;
    config.appendIdentityIdToText = self->appendIdentityIdToText;
    config.traceHistorySeconds = self->traceHistorySeconds;
    config.kalmanDtFallback = self->kalmanDtFallback;
    config.kalmanDtForceFallback = self->kalmanDtForceFallback;
    config.kalmanInitialCovariancePos = self->kalmanInitialCovariancePos;
    config.kalmanInitialCovarianceVel = self->kalmanInitialCovarianceVel;
    config.kalmanProcessNoisePos = self->kalmanProcessNoisePos;
    config.kalmanProcessNoiseVel = self->kalmanProcessNoiseVel;
    config.kalmanMeasurementNoisePos = self->kalmanMeasurementNoisePos;
    config.useKalman = self->useKalman;
    config.emitPredictedDetections = self->emitPredictedDetections;
    config.emitTrace = self->emitTrace;
    config.associationMode = associationModeFromString(self->associationMode);
    config.inferId = gst_opktracker_get_effective_inferId(self);
    config.producerInstanceId = GST_OBJECT_NAME(GST_ELEMENT(self));
    return config;
}

static gboolean gst_opktracker_start(GstBaseTransform *selfInParentType) {
    auto *self = reinterpret_cast<GstOpkTracker *>(selfInParentType);

    if (!self->runtimeMembers) {
        self->runtimeMembers = new GstOpkTracker::Members();
    }
    self->runtimeMembers->tracker.reset();
    self->kalmanDtFallbackActive = FALSE;

    return TRUE;
}

static gboolean gst_opktracker_stop(GstBaseTransform *selfInParentType) {
    auto *self = reinterpret_cast<GstOpkTracker *>(selfInParentType);
    if (self->runtimeMembers) {
        self->runtimeMembers->tracker.reset();
    }
    return TRUE;
}

static bool gst_opktracker_is_supported_format(GstVideoFormat format) noexcept {
    switch (format) {
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_YUY2:
        return true;
    default:
        return false;
    }
}

static gboolean
gst_opktracker_set_caps(GstBaseTransform *selfInParentType, GstCaps *inCaps, GstCaps *outCaps) {
    auto *self = reinterpret_cast<GstOpkTracker *>(selfInParentType);
    (void)outCaps;

    if (!gst_video_info_from_caps(&self->videoInfo, inCaps)) {
        GST_ERROR_OBJECT(self, "Failed to parse input caps");
        return FALSE;
    }

    if (!gst_opktracker_is_supported_format(GST_VIDEO_INFO_FORMAT(&self->videoInfo))) {
        GST_ERROR_OBJECT(self, "Unsupported format (expected BGRA, RGB, I420, NV12, or YUY2)");
        return FALSE;
    }

    return TRUE;
}

static GstFlowReturn gst_opktracker_transform_ip(GstBaseTransform *selfInParentType,
                                                 GstBuffer *buffer) {
    auto *self = reinterpret_cast<GstOpkTracker *>(selfInParentType);

    if (!self->runtimeMembers) {
        return GST_FLOW_OK;
    }

    if (const auto frameResultsMeta = opk::FrameResultsMeta::get(buffer); !frameResultsMeta) {
        return GST_FLOW_OK;
    }

    const GstClockTime runningTime = GST_BUFFER_PTS_IS_VALID(buffer)
                                         ? gst_segment_to_running_time(&self->parent.segment,
                                                                       GST_FORMAT_TIME,
                                                                       GST_BUFFER_PTS(buffer))
                                         : GST_CLOCK_TIME_NONE;
    const auto runningTimeMs = GST_CLOCK_TIME_IS_VALID(runningTime)
                                   ? std::optional<uint64_t>{GST_TIME_AS_MSECONDS(runningTime)}
                                   : std::nullopt;
    opk::FrameResultsMeta::mutate<GstFlowReturn>(buffer, [self, runningTimeMs](auto &frameResults) {
        self->runtimeMembers->tracker.process(
            frameResults, trackerConfigFromElement(self), runningTimeMs);
        return GST_FLOW_OK;
    });
    const auto &kalmanDeltaTime = self->runtimeMembers->tracker.kalmanDeltaTimeTracking();
    if (kalmanDeltaTime.usesFallback() && !self->kalmanDtFallbackActive) {
        GST_DEBUG_OBJECT(self,
                         "Using fallback Kalman dt %.6f seconds (%s)",
                         kalmanDeltaTime.effectiveKalmanDt(),
                         kalmanDeltaTime.fallbackForced() ? "forced"
                                                          : "missing or invalid running time");
        opk::log::warning("[opktracker] Using fallback Kalman dt={} seconds ({})\n",
                          kalmanDeltaTime.effectiveKalmanDt(),
                          kalmanDeltaTime.fallbackForced() ? "forced"
                                                           : "missing or invalid running time");
    }
    self->kalmanDtFallbackActive = kalmanDeltaTime.usesFallback();
    return GST_FLOW_OK;
}

static void gst_opktracker_set_property(GObject *selfInParentType,
                                        guint propertyId,
                                        const GValue *value,
                                        GParamSpec *propertySpec) {
    auto *self = reinterpret_cast<GstOpkTracker *>(selfInParentType);
    const gst::PropertyGuard propertyGuard(GST_ELEMENT(self), propertySpec);
    if (!propertyGuard.isChangeAllowed())
        return;
    if (propertyId == static_cast<guint>(PropertyId::AssociationMode)) {
        if (const gchar *const mode = g_value_get_string(value);
            mode && g_strcmp0(mode, "iou") != 0 && g_strcmp0(mode, "embedding") != 0 &&
            g_strcmp0(mode, "hybrid") != 0) {
            GST_ELEMENT_WARNING(self,
                                RESOURCE,
                                SETTINGS,
                                ("association-mode must be iou, embedding, or hybrid"),
                                ("The previous value is unchanged"));
            return;
        }
    }
    g_autoptr(GMutexLocker) objectLock = g_mutex_locker_new(GST_OBJECT_GET_LOCK(self));
    switch (static_cast<PropertyId>(propertyId)) {
    case PropertyId::ContentType: {
        const auto *const requestedContentType = g_value_get_string(value);
        g_free(self->contentType);
        self->contentType = g_strdup(requestedContentType ? requestedContentType
                                                          : opk::tracker::Defaults::contentType);
        break;
    }
    case PropertyId::UseEmbeddings:
        self->useEmbeddings = g_value_get_boolean(value);
        break;
    case PropertyId::EmbeddingContentType: {
        const auto *const requestedEmbeddingContentType = g_value_get_string(value);
        g_free(self->embeddingContentType);
        self->embeddingContentType =
            g_strdup(requestedEmbeddingContentType ? requestedEmbeddingContentType
                                                   : opk::tracker::Defaults::embeddingContentType);
        break;
    }
    case PropertyId::EmbeddingWeight:
        self->embeddingWeight = g_value_get_float(value);
        break;
    case PropertyId::MinCosineSimilarity:
        self->minCosineSimilarity = g_value_get_float(value);
        break;
    case PropertyId::ReidReassociateThreshold:
        self->reidReassociateThreshold = g_value_get_float(value);
        break;
    case PropertyId::DormantTrackHistorySeconds:
        self->dormantTrackHistorySeconds = g_value_get_float(value);
        break;
    case PropertyId::IouThreshold:
        self->iouThreshold = g_value_get_float(value);
        break;
    case PropertyId::MaxMissedFrames:
        self->maxMissedFrames = g_value_get_int(value);
        break;
    case PropertyId::MinHitsToConfirm:
        self->minHitsToConfirm = g_value_get_int(value);
        break;
    case PropertyId::AppendTrackIdToText:
        self->appendIdentityIdToText = g_value_get_boolean(value);
        break;
    case PropertyId::TraceHistorySeconds:
        self->traceHistorySeconds = g_value_get_float(value);
        break;
    case PropertyId::KalmanDtFallback:
        self->kalmanDtFallback = g_value_get_float(value);
        break;
    case PropertyId::KalmanDtForceFallback:
        self->kalmanDtForceFallback = g_value_get_boolean(value);
        break;
    case PropertyId::KalmanInitialCovariancePos:
        self->kalmanInitialCovariancePos = g_value_get_float(value);
        break;
    case PropertyId::KalmanInitialCovarianceVel:
        self->kalmanInitialCovarianceVel = g_value_get_float(value);
        break;
    case PropertyId::KalmanProcessNoisePos:
        self->kalmanProcessNoisePos = g_value_get_float(value);
        break;
    case PropertyId::KalmanProcessNoiseVel:
        self->kalmanProcessNoiseVel = g_value_get_float(value);
        break;
    case PropertyId::KalmanMeasurementNoisePos:
        self->kalmanMeasurementNoisePos = g_value_get_float(value);
        break;
    case PropertyId::UseKalman:
        self->useKalman = g_value_get_boolean(value);
        break;
    case PropertyId::EmitPredictedDetections:
        self->emitPredictedDetections = g_value_get_boolean(value);
        break;
    case PropertyId::EmitTrace:
        self->emitTrace = g_value_get_boolean(value);
        break;
    case PropertyId::AssociationMode: {
        const auto *const requestedAssociationMode = g_value_get_string(value);
        g_free(self->associationMode);
        self->associationMode =
            g_strdup(requestedAssociationMode
                         ? requestedAssociationMode
                         : associationModeToString(opk::tracker::Defaults::associationMode));
        break;
    }
    case PropertyId::InferId:
        g_free(self->inferId);
        self->inferId = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(selfInParentType, propertyId, propertySpec);
    }
}

static void gst_opktracker_get_property(GObject *selfInParentType,
                                        guint propertyId,
                                        GValue *value,
                                        GParamSpec *propertySpec) {
    auto *self = reinterpret_cast<GstOpkTracker *>(selfInParentType);
    g_autoptr(GMutexLocker) objectLock = g_mutex_locker_new(GST_OBJECT_GET_LOCK(self));
    switch (static_cast<PropertyId>(propertyId)) {
    case PropertyId::ContentType:
        g_value_set_string(value, self->contentType);
        break;
    case PropertyId::UseEmbeddings:
        g_value_set_boolean(value, self->useEmbeddings);
        break;
    case PropertyId::EmbeddingContentType:
        g_value_set_string(value, self->embeddingContentType);
        break;
    case PropertyId::EmbeddingWeight:
        g_value_set_float(value, self->embeddingWeight);
        break;
    case PropertyId::MinCosineSimilarity:
        g_value_set_float(value, self->minCosineSimilarity);
        break;
    case PropertyId::ReidReassociateThreshold:
        g_value_set_float(value, self->reidReassociateThreshold);
        break;
    case PropertyId::DormantTrackHistorySeconds:
        g_value_set_float(value, self->dormantTrackHistorySeconds);
        break;
    case PropertyId::IouThreshold:
        g_value_set_float(value, self->iouThreshold);
        break;
    case PropertyId::MaxMissedFrames:
        g_value_set_int(value, self->maxMissedFrames);
        break;
    case PropertyId::MinHitsToConfirm:
        g_value_set_int(value, self->minHitsToConfirm);
        break;
    case PropertyId::AppendTrackIdToText:
        g_value_set_boolean(value, self->appendIdentityIdToText);
        break;
    case PropertyId::TraceHistorySeconds:
        g_value_set_float(value, self->traceHistorySeconds);
        break;
    case PropertyId::KalmanDtFallback:
        g_value_set_float(value, self->kalmanDtFallback);
        break;
    case PropertyId::KalmanDtForceFallback:
        g_value_set_boolean(value, self->kalmanDtForceFallback);
        break;
    case PropertyId::KalmanInitialCovariancePos:
        g_value_set_float(value, self->kalmanInitialCovariancePos);
        break;
    case PropertyId::KalmanInitialCovarianceVel:
        g_value_set_float(value, self->kalmanInitialCovarianceVel);
        break;
    case PropertyId::KalmanProcessNoisePos:
        g_value_set_float(value, self->kalmanProcessNoisePos);
        break;
    case PropertyId::KalmanProcessNoiseVel:
        g_value_set_float(value, self->kalmanProcessNoiseVel);
        break;
    case PropertyId::KalmanMeasurementNoisePos:
        g_value_set_float(value, self->kalmanMeasurementNoisePos);
        break;
    case PropertyId::UseKalman:
        g_value_set_boolean(value, self->useKalman);
        break;
    case PropertyId::EmitPredictedDetections:
        g_value_set_boolean(value, self->emitPredictedDetections);
        break;
    case PropertyId::EmitTrace:
        g_value_set_boolean(value, self->emitTrace);
        break;
    case PropertyId::AssociationMode:
        g_value_set_string(value, self->associationMode);
        break;
    case PropertyId::InferId:
        g_value_set_string(value, gst_opktracker_get_effective_inferId(self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(selfInParentType, propertyId, propertySpec);
    }
}

static void gst_opktracker_finalize(GObject *selfInParentType) {
    auto *self = reinterpret_cast<GstOpkTracker *>(selfInParentType);

    g_free(self->contentType);
    self->contentType = nullptr;

    g_free(self->embeddingContentType);
    self->embeddingContentType = nullptr;

    g_free(self->associationMode);
    self->associationMode = nullptr;

    g_free(self->inferId);
    self->inferId = nullptr;

    delete self->runtimeMembers;
    self->runtimeMembers = nullptr;

    G_OBJECT_CLASS(gst_opktracker_parent_class)->finalize(selfInParentType);
}

static void gst_opktracker_class_init(GstOpkTrackerClass *trackerClass) {
    GObjectClass *objectClass = G_OBJECT_CLASS(trackerClass);
    GstElementClass *elementClass = GST_ELEMENT_CLASS(trackerClass);
    GstBaseTransformClass *baseTransformClass = GST_BASE_TRANSFORM_CLASS(trackerClass);

    objectClass->set_property = gst_opktracker_set_property;
    objectClass->get_property = gst_opktracker_get_property;
    objectClass->finalize = gst_opktracker_finalize;

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::ContentType),
        g_param_spec_string(
            "content-type",
            "Content type",
            "FrameResults layer content type to track"
            "; Set in NULL or READY; restart via READY resets tracking history for changed "
            "identity/content",
            opk::tracker::Defaults::contentType,
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_READY)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::UseEmbeddings),
        g_param_spec_boolean("use-embeddings",
                             "Use embeddings",
                             "Enable ReID embedding-based association"
                             "; Changeable while playing; applied to subsequent frames",
                             opk::tracker::Defaults::useEmbeddings,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::EmbeddingContentType),
        g_param_spec_string(
            "embedding-content-type",
            "Embedding content type",
            "FrameResults layer content type containing object embeddings"
            "; Set in NULL or READY; restart via READY resets tracking history for changed "
            "identity/content",
            opk::tracker::Defaults::embeddingContentType,
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_READY)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::EmbeddingWeight),
        g_param_spec_float("embedding-weight",
                           "Embedding weight",
                           "Blend factor between IoU and embedding cost (0..1)"
                           "; Changeable while playing; applied to subsequent frames",
                           0.0f,
                           1.0f,
                           opk::tracker::Defaults::embeddingWeight,
                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                    GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::MinCosineSimilarity),
        g_param_spec_float("min-cosine-similarity",
                           "Min cosine similarity",
                           "Minimum cosine similarity to accept embedding contribution"
                           "; Changeable while playing; applied to subsequent frames",
                           -1.0f,
                           1.0f,
                           opk::tracker::Defaults::minCosineSimilarity,
                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                    GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::ReidReassociateThreshold),
        g_param_spec_float("reid-reassociate-threshold",
                           "ReID reassociate threshold",
                           "Similarity threshold to restore track from dormant gallery"
                           "; Changeable while playing; applied to subsequent frames",
                           -1.0f,
                           1.0f,
                           opk::tracker::Defaults::reidReassociateThreshold,
                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                    GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::DormantTrackHistorySeconds),
        g_param_spec_float("dormant-track-history-seconds",
                           "Dormant track history seconds",
                           "How long expired track embeddings remain available for reassociation"
                           "; Changeable while playing; applied to subsequent frames",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::dormantTrackHistorySeconds,
                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                    GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::IouThreshold),
        g_param_spec_float("iou-threshold",
                           "IoU threshold",
                           "Minimum IoU for matching detection to track"
                           "; Changeable while playing; applied to subsequent frames",
                           0.0f,
                           1.0f,
                           opk::tracker::Defaults::iouThreshold,
                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                    GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::MaxMissedFrames),
        g_param_spec_int("max-missed-frames",
                         "Max missed frames",
                         "Frames to keep unmatched tracks alive"
                         "; Changeable while playing; applied to subsequent frames",
                         0,
                         G_MAXINT,
                         opk::tracker::Defaults::maxMissedFrames,
                         static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                  GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::MinHitsToConfirm),
        g_param_spec_int("min-hits-to-confirm",
                         "Min hits to confirm",
                         "Hits before track is shown"
                         "; Changeable while playing; applied to subsequent frames",
                         1,
                         G_MAXINT,
                         opk::tracker::Defaults::minHitsToConfirm,
                         static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                  GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::AppendTrackIdToText),
        g_param_spec_boolean("append-track-id-to-text",
                             "Append track ID",
                             "Append [ID:n] to detection text"
                             "; Changeable while playing; applied to subsequent frames",
                             opk::tracker::Defaults::appendIdentityIdToText,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::TraceHistorySeconds),
        g_param_spec_float(
            "trace-history-seconds",
            "Trace history seconds",
            "Time-window for trace history (stored points use the current Kalman time step)"
            "; Changeable while playing; applied to subsequent frames",
            0.0f,
            G_MAXFLOAT,
            opk::tracker::Defaults::traceHistorySeconds,
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::KalmanDtFallback),
        g_param_spec_float("kalman-dt-fallback",
                           "Kalman dt fallback",
                           "Kalman time step used when timestamps are unavailable or forced"
                           "; Changeable while playing; applied to subsequent frames",
                           0.0001f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanDtFallback,
                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                    GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::KalmanDtForceFallback),
        g_param_spec_boolean("kalman-dt-force-fallback",
                             "Force Kalman dt fallback",
                             "Use kalman-dt-fallback instead of buffer timestamps"
                             "; Changeable while playing; applied to subsequent frames",
                             opk::tracker::Defaults::kalmanDtForceFallback,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::KalmanInitialCovariancePos),
        g_param_spec_float(
            "kalman-initial-covariance-pos",
            "Kalman initial covariance position",
            "Initial position covariance"
            "; Changeable while playing; applied to subsequent frames when creating new tracks; "
            "existing tracks retain their covariance",
            0.0f,
            G_MAXFLOAT,
            opk::tracker::Defaults::kalmanInitialCovariancePos,
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::KalmanInitialCovarianceVel),
        g_param_spec_float(
            "kalman-initial-covariance-vel",
            "Kalman initial covariance velocity",
            "Initial velocity covariance"
            "; Changeable while playing; applied to subsequent frames when creating new tracks; "
            "existing tracks retain their covariance",
            0.0f,
            G_MAXFLOAT,
            opk::tracker::Defaults::kalmanInitialCovarianceVel,
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::KalmanProcessNoisePos),
        g_param_spec_float("kalman-process-noise-pos",
                           "Kalman process noise position",
                           "Process noise on position"
                           "; Changeable while playing; applied to subsequent frames",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanProcessNoisePos,
                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                    GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::KalmanProcessNoiseVel),
        g_param_spec_float("kalman-process-noise-vel",
                           "Kalman process noise velocity",
                           "Process noise on velocity"
                           "; Changeable while playing; applied to subsequent frames",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanProcessNoiseVel,
                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                    GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::KalmanMeasurementNoisePos),
        g_param_spec_float("kalman-measurement-noise-pos",
                           "Kalman measurement noise position",
                           "Measurement noise on x/y"
                           "; Changeable while playing; applied to subsequent frames",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanMeasurementNoisePos,
                           static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                    GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::UseKalman),
        g_param_spec_boolean("use-kalman",
                             "Use Kalman",
                             "Enable Kalman prediction and measurement smoothing"
                             "; Changeable while playing; applied to subsequent frames",
                             opk::tracker::Defaults::useKalman,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::EmitPredictedDetections),
        g_param_spec_boolean("emit-predicted-detections",
                             "Emit predicted detections",
                             "Append predicted-only tracks when detections are temporarily missing"
                             "; Changeable while playing; applied to subsequent frames",
                             opk::tracker::Defaults::emitPredictedDetections,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::EmitTrace),
        g_param_spec_boolean("emit-trace",
                             "Emit trace",
                             "Append trackTrace output for active tracks"
                             "; Changeable while playing; applied to subsequent frames",
                             opk::tracker::Defaults::emitTrace,
                             static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                      GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::AssociationMode),
        g_param_spec_string("association-mode",
                            "Association mode",
                            "Association strategy: hybrid, iou, or embedding"
                            "; Changeable while playing; applied to subsequent frames",
                            associationModeToString(opk::tracker::Defaults::associationMode),
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                                     GST_PARAM_MUTABLE_PLAYING)));

    g_object_class_install_property(
        objectClass,
        static_cast<guint>(PropertyId::InferId),
        g_param_spec_string(
            "infer-id",
            "ID of the inference element",
            "ID of the inference element (used in Plumber to identify the layers. "
            "Defaults to the name property of the element)"
            "; Set in NULL or READY; restart via READY resets tracking history for changed "
            "identity/content",
            "",
            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_READY)));

    static GstStaticPadTemplate sinkPadTemplate = GST_STATIC_PAD_TEMPLATE(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(gst::Tools::supportedVideoCaps));
    static GstStaticPadTemplate sourcePadTemplate = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(gst::Tools::supportedVideoCaps));
    gst_element_class_add_static_pad_template(elementClass, &sinkPadTemplate);
    gst_element_class_add_static_pad_template(elementClass, &sourcePadTemplate);

    gst_element_class_set_static_metadata(
        elementClass,
        "OPK Tracker",
        "Filter/Effect/Video",
        "Tracks detections across frames using FrameResults metadata",
        "Arm Limited <perception-fdbck@arm.com>");

    baseTransformClass->start = gst_opktracker_start;
    baseTransformClass->stop = gst_opktracker_stop;
    baseTransformClass->set_caps = gst_opktracker_set_caps;
    baseTransformClass->transform_ip = gst_opktracker_transform_ip;
    baseTransformClass->prepare_output_buffer =
        [](GstBaseTransform *selfInParentType, GstBuffer *inputBuffer, GstBuffer **outputBuffer) {
            return gst::Tools::prepareVideoBuffer(selfInParentType, inputBuffer, outputBuffer);
        };
}

static void gst_opktracker_init(GstOpkTracker *self) {
    self->runtimeMembers = nullptr;

    self->contentType = g_strdup(opk::tracker::Defaults::contentType);
    self->useEmbeddings = opk::tracker::Defaults::useEmbeddings;
    self->embeddingContentType = g_strdup(opk::tracker::Defaults::embeddingContentType);
    self->embeddingWeight = opk::tracker::Defaults::embeddingWeight;
    self->minCosineSimilarity = opk::tracker::Defaults::minCosineSimilarity;
    self->reidReassociateThreshold = opk::tracker::Defaults::reidReassociateThreshold;
    self->dormantTrackHistorySeconds = opk::tracker::Defaults::dormantTrackHistorySeconds;
    self->iouThreshold = opk::tracker::Defaults::iouThreshold;
    self->maxMissedFrames = opk::tracker::Defaults::maxMissedFrames;
    self->minHitsToConfirm = opk::tracker::Defaults::minHitsToConfirm;
    self->appendIdentityIdToText = opk::tracker::Defaults::appendIdentityIdToText;
    self->traceHistorySeconds = opk::tracker::Defaults::traceHistorySeconds;
    self->kalmanDtFallback = opk::tracker::Defaults::kalmanDtFallback;
    self->kalmanDtForceFallback = opk::tracker::Defaults::kalmanDtForceFallback;
    self->kalmanDtFallbackActive = FALSE;
    self->kalmanInitialCovariancePos = opk::tracker::Defaults::kalmanInitialCovariancePos;
    self->kalmanInitialCovarianceVel = opk::tracker::Defaults::kalmanInitialCovarianceVel;
    self->kalmanProcessNoisePos = opk::tracker::Defaults::kalmanProcessNoisePos;
    self->kalmanProcessNoiseVel = opk::tracker::Defaults::kalmanProcessNoiseVel;
    self->kalmanMeasurementNoisePos = opk::tracker::Defaults::kalmanMeasurementNoisePos;
    self->useKalman = opk::tracker::Defaults::useKalman;
    self->emitPredictedDetections = opk::tracker::Defaults::emitPredictedDetections;
    self->emitTrace = opk::tracker::Defaults::emitTrace;
    self->associationMode =
        g_strdup(associationModeToString(opk::tracker::Defaults::associationMode));
    self->inferId = nullptr;

    gst_video_info_init(&self->videoInfo);

    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(self), FALSE);
}

static gboolean opktracker_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "opktracker", GST_RANK_NONE, GST_TYPE_OPKTRACKER);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  opktracker,
                  "OPK tracker based on FrameResults metadata",
                  opktracker_plugin_init,
                  "1.0",
                  "Apache 2.0",
                  PACKAGE,
                  "https://www.arm.com/")

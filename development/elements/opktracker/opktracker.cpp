/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Tracker.h"

#include <gst/FrameResultsMeta.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "Log.h"

#ifndef PACKAGE
#define PACKAGE "opk-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_OPKTRACKER (gst_opktracker_get_type())
G_DECLARE_FINAL_TYPE(GstOpkTracker, gst_opktracker, GST, OPKTRACKER, GstBaseTransform)

struct _GstOpkTracker {
    GstBaseTransform parent;

    GstVideoInfo vinfo;

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
    Members *m;
};

G_END_DECLS

static constexpr const char *OPK_SUPPORTED_RAW_VIDEO_CAPS =
    "video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}";

struct _GstOpkTracker::Members {
    opk::tracker::Tracker tracker;
};

G_DEFINE_TYPE(GstOpkTracker, gst_opktracker, GST_TYPE_BASE_TRANSFORM)

enum {
    PROP_0,
    PROP_CONTENT_TYPE,
    PROP_USE_EMBEDDINGS,
    PROP_EMBEDDING_CONTENT_TYPE,
    PROP_EMBEDDING_WEIGHT,
    PROP_MIN_COSINE_SIMILARITY,
    PROP_REID_REASSOCIATE_THRESHOLD,
    PROP_DORMANT_TRACK_HISTORY_SECONDS,
    PROP_IOU_THRESHOLD,
    PROP_MAX_MISSED_FRAMES,
    PROP_MIN_HITS_TO_CONFIRM,
    PROP_APPEND_TRACK_ID_TO_TEXT,
    PROP_TRACE_HISTORY_SECONDS,
    PROP_KALMAN_DT_FALLBACK,
    PROP_KALMAN_DT_FORCE_FALLBACK,
    PROP_KALMAN_INITIAL_COVARIANCE_POS,
    PROP_KALMAN_INITIAL_COVARIANCE_VEL,
    PROP_KALMAN_PROCESS_NOISE_POS,
    PROP_KALMAN_PROCESS_NOISE_VEL,
    PROP_KALMAN_MEASUREMENT_NOISE_POS,
    PROP_USE_KALMAN,
    PROP_EMIT_PREDICTED_DETECTIONS,
    PROP_EMIT_TRACE,
    PROP_ASSOCIATION_MODE,
    PROP_INFER_ID,
};

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

static opk::tracker::Config trackerConfigFromElement(const GstOpkTracker *self) {
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

static gboolean gst_opktracker_start(GstBaseTransform *b) {
    auto *self = (GstOpkTracker *)b;

    if (!self->m) {
        self->m = new GstOpkTracker::Members();
    }
    self->m->tracker.reset();
    self->kalmanDtFallbackActive = FALSE;

    return TRUE;
}

static gboolean gst_opktracker_stop(GstBaseTransform *b) {
    auto *self = (GstOpkTracker *)b;
    if (self->m) {
        self->m->tracker.reset();
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

static gboolean gst_opktracker_set_caps(GstBaseTransform *b, GstCaps *incaps, GstCaps *outcaps) {
    auto *self = (GstOpkTracker *)b;
    (void)outcaps;

    if (!gst_video_info_from_caps(&self->vinfo, incaps)) {
        GST_ERROR_OBJECT(self, "Failed to parse input caps");
        return FALSE;
    }

    if (!gst_opktracker_is_supported_format(GST_VIDEO_INFO_FORMAT(&self->vinfo))) {
        GST_ERROR_OBJECT(self, "Unsupported format (expected BGRA, RGB, I420, NV12, or YUY2)");
        return FALSE;
    }

    return TRUE;
}

static GstFlowReturn gst_opktracker_transform_ip(GstBaseTransform *b, GstBuffer *buf) {
    auto *self = (GstOpkTracker *)b;

    if (!self->m) {
        return GST_FLOW_OK;
    }

    if (const auto frameResultsMeta = opk::FrameResultsMeta::get(buf); !frameResultsMeta) {
        return GST_FLOW_OK;
    }

    const GstClockTime runningTime =
        GST_BUFFER_PTS_IS_VALID(buf)
            ? gst_segment_to_running_time(&b->segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buf))
            : GST_CLOCK_TIME_NONE;
    const auto runningTimeMs = GST_CLOCK_TIME_IS_VALID(runningTime)
                                   ? std::optional<uint64_t>{GST_TIME_AS_MSECONDS(runningTime)}
                                   : std::nullopt;
    opk::FrameResultsMeta::mutate<GstFlowReturn>(buf, [self, runningTimeMs](auto &frameResults) {
        self->m->tracker.process(frameResults, trackerConfigFromElement(self), runningTimeMs);
        return GST_FLOW_OK;
    });
    const auto &kalmanDeltaTime = self->m->tracker.kalmanDeltaTimeTracking();
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

static void gst_opktracker_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps) {
    auto *self = (GstOpkTracker *)o;
    switch (id) {
    case PROP_CONTENT_TYPE:
        g_free(self->contentType);
        self->contentType = g_value_dup_string(v);
        break;
    case PROP_USE_EMBEDDINGS:
        self->useEmbeddings = g_value_get_boolean(v);
        break;
    case PROP_EMBEDDING_CONTENT_TYPE:
        g_free(self->embeddingContentType);
        self->embeddingContentType = g_value_dup_string(v);
        break;
    case PROP_EMBEDDING_WEIGHT:
        self->embeddingWeight = g_value_get_float(v);
        break;
    case PROP_MIN_COSINE_SIMILARITY:
        self->minCosineSimilarity = g_value_get_float(v);
        break;
    case PROP_REID_REASSOCIATE_THRESHOLD:
        self->reidReassociateThreshold = g_value_get_float(v);
        break;
    case PROP_DORMANT_TRACK_HISTORY_SECONDS:
        self->dormantTrackHistorySeconds = g_value_get_float(v);
        break;
    case PROP_IOU_THRESHOLD:
        self->iouThreshold = g_value_get_float(v);
        break;
    case PROP_MAX_MISSED_FRAMES:
        self->maxMissedFrames = g_value_get_int(v);
        break;
    case PROP_MIN_HITS_TO_CONFIRM:
        self->minHitsToConfirm = g_value_get_int(v);
        break;
    case PROP_APPEND_TRACK_ID_TO_TEXT:
        self->appendIdentityIdToText = g_value_get_boolean(v);
        break;
    case PROP_TRACE_HISTORY_SECONDS:
        self->traceHistorySeconds = g_value_get_float(v);
        break;
    case PROP_KALMAN_DT_FALLBACK:
        self->kalmanDtFallback = g_value_get_float(v);
        break;
    case PROP_KALMAN_DT_FORCE_FALLBACK:
        self->kalmanDtForceFallback = g_value_get_boolean(v);
        break;
    case PROP_KALMAN_INITIAL_COVARIANCE_POS:
        self->kalmanInitialCovariancePos = g_value_get_float(v);
        break;
    case PROP_KALMAN_INITIAL_COVARIANCE_VEL:
        self->kalmanInitialCovarianceVel = g_value_get_float(v);
        break;
    case PROP_KALMAN_PROCESS_NOISE_POS:
        self->kalmanProcessNoisePos = g_value_get_float(v);
        break;
    case PROP_KALMAN_PROCESS_NOISE_VEL:
        self->kalmanProcessNoiseVel = g_value_get_float(v);
        break;
    case PROP_KALMAN_MEASUREMENT_NOISE_POS:
        self->kalmanMeasurementNoisePos = g_value_get_float(v);
        break;
    case PROP_USE_KALMAN:
        self->useKalman = g_value_get_boolean(v);
        break;
    case PROP_EMIT_PREDICTED_DETECTIONS:
        self->emitPredictedDetections = g_value_get_boolean(v);
        break;
    case PROP_EMIT_TRACE:
        self->emitTrace = g_value_get_boolean(v);
        break;
    case PROP_ASSOCIATION_MODE:
        g_free(self->associationMode);
        self->associationMode = g_value_dup_string(v);
        break;
    case PROP_INFER_ID:
        g_free(self->inferId);
        self->inferId = g_value_dup_string(v);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_opktracker_get_property(GObject *o, guint id, GValue *v, GParamSpec *ps) {
    auto *self = (GstOpkTracker *)o;
    switch (id) {
    case PROP_CONTENT_TYPE:
        g_value_set_string(v, self->contentType);
        break;
    case PROP_USE_EMBEDDINGS:
        g_value_set_boolean(v, self->useEmbeddings);
        break;
    case PROP_EMBEDDING_CONTENT_TYPE:
        g_value_set_string(v, self->embeddingContentType);
        break;
    case PROP_EMBEDDING_WEIGHT:
        g_value_set_float(v, self->embeddingWeight);
        break;
    case PROP_MIN_COSINE_SIMILARITY:
        g_value_set_float(v, self->minCosineSimilarity);
        break;
    case PROP_REID_REASSOCIATE_THRESHOLD:
        g_value_set_float(v, self->reidReassociateThreshold);
        break;
    case PROP_DORMANT_TRACK_HISTORY_SECONDS:
        g_value_set_float(v, self->dormantTrackHistorySeconds);
        break;
    case PROP_IOU_THRESHOLD:
        g_value_set_float(v, self->iouThreshold);
        break;
    case PROP_MAX_MISSED_FRAMES:
        g_value_set_int(v, self->maxMissedFrames);
        break;
    case PROP_MIN_HITS_TO_CONFIRM:
        g_value_set_int(v, self->minHitsToConfirm);
        break;
    case PROP_APPEND_TRACK_ID_TO_TEXT:
        g_value_set_boolean(v, self->appendIdentityIdToText);
        break;
    case PROP_TRACE_HISTORY_SECONDS:
        g_value_set_float(v, self->traceHistorySeconds);
        break;
    case PROP_KALMAN_DT_FALLBACK:
        g_value_set_float(v, self->kalmanDtFallback);
        break;
    case PROP_KALMAN_DT_FORCE_FALLBACK:
        g_value_set_boolean(v, self->kalmanDtForceFallback);
        break;
    case PROP_KALMAN_INITIAL_COVARIANCE_POS:
        g_value_set_float(v, self->kalmanInitialCovariancePos);
        break;
    case PROP_KALMAN_INITIAL_COVARIANCE_VEL:
        g_value_set_float(v, self->kalmanInitialCovarianceVel);
        break;
    case PROP_KALMAN_PROCESS_NOISE_POS:
        g_value_set_float(v, self->kalmanProcessNoisePos);
        break;
    case PROP_KALMAN_PROCESS_NOISE_VEL:
        g_value_set_float(v, self->kalmanProcessNoiseVel);
        break;
    case PROP_KALMAN_MEASUREMENT_NOISE_POS:
        g_value_set_float(v, self->kalmanMeasurementNoisePos);
        break;
    case PROP_USE_KALMAN:
        g_value_set_boolean(v, self->useKalman);
        break;
    case PROP_EMIT_PREDICTED_DETECTIONS:
        g_value_set_boolean(v, self->emitPredictedDetections);
        break;
    case PROP_EMIT_TRACE:
        g_value_set_boolean(v, self->emitTrace);
        break;
    case PROP_ASSOCIATION_MODE:
        g_value_set_string(v, self->associationMode);
        break;
    case PROP_INFER_ID:
        g_value_set_string(v, gst_opktracker_get_effective_inferId(self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_opktracker_finalize(GObject *object) {
    auto *self = (GstOpkTracker *)object;

    g_free(self->contentType);
    self->contentType = nullptr;

    g_free(self->embeddingContentType);
    self->embeddingContentType = nullptr;

    g_free(self->associationMode);
    self->associationMode = nullptr;

    g_free(self->inferId);
    self->inferId = nullptr;

    delete self->m;
    self->m = nullptr;

    G_OBJECT_CLASS(gst_opktracker_parent_class)->finalize(object);
}

static void gst_opktracker_class_init(GstOpkTrackerClass *klass) {
    GObjectClass *gobj = G_OBJECT_CLASS(klass);
    GstElementClass *ecls = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *bcls = GST_BASE_TRANSFORM_CLASS(klass);

    gobj->set_property = gst_opktracker_set_property;
    gobj->get_property = gst_opktracker_get_property;
    gobj->finalize = gst_opktracker_finalize;

    g_object_class_install_property(
        gobj,
        PROP_CONTENT_TYPE,
        g_param_spec_string("content-type",
                            "Content type",
                            "FrameResults layer content type to track",
                            opk::tracker::Defaults::contentType,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_USE_EMBEDDINGS,
        g_param_spec_boolean("use-embeddings",
                             "Use embeddings",
                             "Enable ReID embedding-based association",
                             opk::tracker::Defaults::useEmbeddings,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_EMBEDDING_CONTENT_TYPE,
        g_param_spec_string("embedding-content-type",
                            "Embedding content type",
                            "FrameResults layer content type containing object embeddings",
                            opk::tracker::Defaults::embeddingContentType,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_EMBEDDING_WEIGHT,
        g_param_spec_float("embedding-weight",
                           "Embedding weight",
                           "Blend factor between IoU and embedding cost (0..1)",
                           0.0f,
                           1.0f,
                           opk::tracker::Defaults::embeddingWeight,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_MIN_COSINE_SIMILARITY,
        g_param_spec_float("min-cosine-similarity",
                           "Min cosine similarity",
                           "Minimum cosine similarity to accept embedding contribution",
                           -1.0f,
                           1.0f,
                           opk::tracker::Defaults::minCosineSimilarity,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_REID_REASSOCIATE_THRESHOLD,
        g_param_spec_float("reid-reassociate-threshold",
                           "ReID reassociate threshold",
                           "Similarity threshold to restore track from dormant gallery",
                           -1.0f,
                           1.0f,
                           opk::tracker::Defaults::reidReassociateThreshold,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_DORMANT_TRACK_HISTORY_SECONDS,
        g_param_spec_float("dormant-track-history-seconds",
                           "Dormant track history seconds",
                           "How long expired track embeddings remain available for reassociation",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::dormantTrackHistorySeconds,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_IOU_THRESHOLD,
        g_param_spec_float("iou-threshold",
                           "IoU threshold",
                           "Minimum IoU for matching detection to track",
                           0.0f,
                           1.0f,
                           opk::tracker::Defaults::iouThreshold,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_MAX_MISSED_FRAMES,
        g_param_spec_int("max-missed-frames",
                         "Max missed frames",
                         "Frames to keep unmatched tracks alive",
                         0,
                         G_MAXINT,
                         opk::tracker::Defaults::maxMissedFrames,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_MIN_HITS_TO_CONFIRM,
        g_param_spec_int("min-hits-to-confirm",
                         "Min hits to confirm",
                         "Hits before track is shown",
                         1,
                         G_MAXINT,
                         opk::tracker::Defaults::minHitsToConfirm,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_APPEND_TRACK_ID_TO_TEXT,
        g_param_spec_boolean("append-track-id-to-text",
                             "Append track ID",
                             "Append [ID:n] to detection text",
                             opk::tracker::Defaults::appendIdentityIdToText,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_TRACE_HISTORY_SECONDS,
        g_param_spec_float(
            "trace-history-seconds",
            "Trace history seconds",
            "Time-window for trace history (stored points use the current Kalman time step)",
            0.0f,
            G_MAXFLOAT,
            opk::tracker::Defaults::traceHistorySeconds,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_DT_FALLBACK,
        g_param_spec_float("kalman-dt-fallback",
                           "Kalman dt fallback",
                           "Kalman time step used when timestamps are unavailable or forced",
                           0.0001f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanDtFallback,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_DT_FORCE_FALLBACK,
        g_param_spec_boolean("kalman-dt-force-fallback",
                             "Force Kalman dt fallback",
                             "Use kalman-dt-fallback instead of buffer timestamps",
                             opk::tracker::Defaults::kalmanDtForceFallback,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_INITIAL_COVARIANCE_POS,
        g_param_spec_float("kalman-initial-covariance-pos",
                           "Kalman initial covariance position",
                           "Initial position covariance",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanInitialCovariancePos,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_INITIAL_COVARIANCE_VEL,
        g_param_spec_float("kalman-initial-covariance-vel",
                           "Kalman initial covariance velocity",
                           "Initial velocity covariance",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanInitialCovarianceVel,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_PROCESS_NOISE_POS,
        g_param_spec_float("kalman-process-noise-pos",
                           "Kalman process noise position",
                           "Process noise on position",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanProcessNoisePos,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_PROCESS_NOISE_VEL,
        g_param_spec_float("kalman-process-noise-vel",
                           "Kalman process noise velocity",
                           "Process noise on velocity",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanProcessNoiseVel,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_MEASUREMENT_NOISE_POS,
        g_param_spec_float("kalman-measurement-noise-pos",
                           "Kalman measurement noise position",
                           "Measurement noise on x/y",
                           0.0f,
                           G_MAXFLOAT,
                           opk::tracker::Defaults::kalmanMeasurementNoisePos,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_USE_KALMAN,
        g_param_spec_boolean("use-kalman",
                             "Use Kalman",
                             "Enable Kalman prediction and measurement smoothing",
                             opk::tracker::Defaults::useKalman,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_EMIT_PREDICTED_DETECTIONS,
        g_param_spec_boolean("emit-predicted-detections",
                             "Emit predicted detections",
                             "Append predicted-only tracks when detections are temporarily missing",
                             opk::tracker::Defaults::emitPredictedDetections,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_EMIT_TRACE,
        g_param_spec_boolean("emit-trace",
                             "Emit trace",
                             "Append trackTrace output for active tracks",
                             opk::tracker::Defaults::emitTrace,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_ASSOCIATION_MODE,
        g_param_spec_string("association-mode",
                            "Association mode",
                            "Association strategy: hybrid, iou, or embedding",
                            associationModeToString(opk::tracker::Defaults::associationMode),
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_INFER_ID,
        g_param_spec_string("infer-id",
                            "ID of the inference element",
                            "ID of the inference element (used in Plumber to identify the layers. "
                            "Defaults to the name property of the element)",
                            "",
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    static GstStaticPadTemplate sink_t = GST_STATIC_PAD_TEMPLATE(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(OPK_SUPPORTED_RAW_VIDEO_CAPS));
    static GstStaticPadTemplate src_t = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(OPK_SUPPORTED_RAW_VIDEO_CAPS));
    gst_element_class_add_static_pad_template(ecls, &sink_t);
    gst_element_class_add_static_pad_template(ecls, &src_t);

    gst_element_class_set_static_metadata(
        ecls,
        "OPK Tracker",
        "Filter/Effect/Video",
        "Tracks detections across frames using FrameResults metadata",
        "Arm Limited");

    bcls->start = gst_opktracker_start;
    bcls->stop = gst_opktracker_stop;
    bcls->set_caps = gst_opktracker_set_caps;
    bcls->transform_ip = gst_opktracker_transform_ip;
}

static void gst_opktracker_init(GstOpkTracker *self) {
    self->m = nullptr;

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

    gst_video_info_init(&self->vinfo);

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
                  "LGPL",
                  "Open Perception Kit",
                  "https://www.arm.com/")

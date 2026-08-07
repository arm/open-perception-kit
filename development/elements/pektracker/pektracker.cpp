/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Tracker.h"

#include <gst/PerceptionMeta.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#ifndef PACKAGE
#define PACKAGE "pek-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_PEKTRACKER (gst_pektracker_get_type())
G_DECLARE_FINAL_TYPE(GstPekTracker, gst_pektracker, GST, PEKTRACKER, GstBaseTransform)

struct _GstPekTracker {
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
    gfloat kalmanDt;
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

struct _GstPekTracker::Members {
    pek::tracker::Tracker tracker;
};

G_DEFINE_TYPE(GstPekTracker, gst_pektracker, GST_TYPE_BASE_TRANSFORM)

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
    PROP_KALMAN_DT,
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

static pek::tracker::AssociationMode associationModeFromString(const gchar *modeText) {
    if (modeText == nullptr) {
        return pek::tracker::Defaults::associationMode;
    }

    const std::string mode = modeText;
    if (mode == "iou") {
        return pek::tracker::AssociationMode::Iou;
    }
    if (mode == "embedding") {
        return pek::tracker::AssociationMode::Embedding;
    }
    return pek::tracker::AssociationMode::Hybrid;
}

static const gchar *associationModeToString(pek::tracker::AssociationMode mode) {
    switch (mode) {
    case pek::tracker::AssociationMode::Iou:
        return "iou";
    case pek::tracker::AssociationMode::Embedding:
        return "embedding";
    case pek::tracker::AssociationMode::Hybrid:
        return "hybrid";
    }

    return "hybrid";
}

static const gchar *gst_pektracker_get_effective_inferId(const GstPekTracker *self) {
    /* If user provided infer-id property, prefer it */
    if (self->inferId && self->inferId[0] != '\0')
        return self->inferId;

    /* Fallback to element name (always exists) */
    return GST_OBJECT_NAME(GST_ELEMENT(self));
}

static pek::tracker::Config trackerConfigFromElement(const GstPekTracker *self) {
    pek::tracker::Config config;
    config.contentType =
        self->contentType ? self->contentType : pek::tracker::Defaults::contentType;
    config.useEmbeddings = self->useEmbeddings;
    config.embeddingContentType = self->embeddingContentType
                                      ? self->embeddingContentType
                                      : pek::tracker::Defaults::embeddingContentType;
    config.embeddingWeight = self->embeddingWeight;
    config.minCosineSimilarity = self->minCosineSimilarity;
    config.reidReassociateThreshold = self->reidReassociateThreshold;
    config.dormantTrackHistorySeconds = self->dormantTrackHistorySeconds;
    config.iouThreshold = self->iouThreshold;
    config.maxMissedFrames = self->maxMissedFrames;
    config.minHitsToConfirm = self->minHitsToConfirm;
    config.appendIdentityIdToText = self->appendIdentityIdToText;
    config.traceHistorySeconds = self->traceHistorySeconds;
    config.kalmanDt = self->kalmanDt;
    config.kalmanInitialCovariancePos = self->kalmanInitialCovariancePos;
    config.kalmanInitialCovarianceVel = self->kalmanInitialCovarianceVel;
    config.kalmanProcessNoisePos = self->kalmanProcessNoisePos;
    config.kalmanProcessNoiseVel = self->kalmanProcessNoiseVel;
    config.kalmanMeasurementNoisePos = self->kalmanMeasurementNoisePos;
    config.useKalman = self->useKalman;
    config.emitPredictedDetections = self->emitPredictedDetections;
    config.emitTrace = self->emitTrace;
    config.associationMode = associationModeFromString(self->associationMode);
    config.inferId = gst_pektracker_get_effective_inferId(self);
    return config;
}

static gboolean gst_pektracker_start(GstBaseTransform *b) {
    auto *self = (GstPekTracker *)b;

    if (!self->m) {
        self->m = new GstPekTracker::Members();
    }
    self->m->tracker.reset();

    return TRUE;
}

static gboolean gst_pektracker_stop(GstBaseTransform *b) {
    auto *self = (GstPekTracker *)b;
    if (self->m) {
        self->m->tracker.reset();
    }
    return TRUE;
}

static gboolean gst_pektracker_set_caps(GstBaseTransform *b, GstCaps *incaps, GstCaps *outcaps) {
    auto *self = (GstPekTracker *)b;
    (void)outcaps;

    if (!gst_video_info_from_caps(&self->vinfo, incaps)) {
        GST_ERROR_OBJECT(self, "Failed to parse input caps");
        return FALSE;
    }

    if (GST_VIDEO_INFO_FORMAT(&self->vinfo) != GST_VIDEO_FORMAT_BGRA) {
        GST_ERROR_OBJECT(self, "Unsupported format (expected BGRA)");
        return FALSE;
    }

    return TRUE;
}

static GstFlowReturn gst_pektracker_transform_ip(GstBaseTransform *b, GstBuffer *buf) {
    auto *self = (GstPekTracker *)b;

    if (!self->m) {
        return GST_FLOW_OK;
    }

    if (const auto perceptionMeta = pek::PerceptionMeta::get(buf); !perceptionMeta) {
        return GST_FLOW_OK;
    }

    pek::PerceptionMeta::mutate<GstFlowReturn>(buf, [self](auto &perception) {
        self->m->tracker.process(perception, trackerConfigFromElement(self));
        return GST_FLOW_OK;
    });

    return GST_FLOW_OK;
}

static void gst_pektracker_set_property(GObject *o, guint id, const GValue *v, GParamSpec *ps) {
    auto *self = (GstPekTracker *)o;
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
    case PROP_KALMAN_DT:
        self->kalmanDt = g_value_get_float(v);
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

static void gst_pektracker_get_property(GObject *o, guint id, GValue *v, GParamSpec *ps) {
    auto *self = (GstPekTracker *)o;
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
    case PROP_KALMAN_DT:
        g_value_set_float(v, self->kalmanDt);
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
        g_value_set_string(v, gst_pektracker_get_effective_inferId(self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void gst_pektracker_finalize(GObject *object) {
    auto *self = (GstPekTracker *)object;

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

    G_OBJECT_CLASS(gst_pektracker_parent_class)->finalize(object);
}

static void gst_pektracker_class_init(GstPekTrackerClass *klass) {
    GObjectClass *gobj = G_OBJECT_CLASS(klass);
    GstElementClass *ecls = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *bcls = GST_BASE_TRANSFORM_CLASS(klass);

    gobj->set_property = gst_pektracker_set_property;
    gobj->get_property = gst_pektracker_get_property;
    gobj->finalize = gst_pektracker_finalize;

    g_object_class_install_property(
        gobj,
        PROP_CONTENT_TYPE,
        g_param_spec_string("content-type",
                            "Content type",
                            "Perception layer contentType to track",
                            pek::tracker::Defaults::contentType,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_USE_EMBEDDINGS,
        g_param_spec_boolean("use-embeddings",
                             "Use embeddings",
                             "Enable ReID embedding-based association",
                             pek::tracker::Defaults::useEmbeddings,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_EMBEDDING_CONTENT_TYPE,
        g_param_spec_string("embedding-content-type",
                            "Embedding content type",
                            "Perception layer contentType containing object embeddings",
                            pek::tracker::Defaults::embeddingContentType,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_EMBEDDING_WEIGHT,
        g_param_spec_float("embedding-weight",
                           "Embedding weight",
                           "Blend factor between IoU and embedding cost (0..1)",
                           0.0f,
                           1.0f,
                           pek::tracker::Defaults::embeddingWeight,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_MIN_COSINE_SIMILARITY,
        g_param_spec_float("min-cosine-similarity",
                           "Min cosine similarity",
                           "Minimum cosine similarity to accept embedding contribution",
                           -1.0f,
                           1.0f,
                           pek::tracker::Defaults::minCosineSimilarity,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_REID_REASSOCIATE_THRESHOLD,
        g_param_spec_float("reid-reassociate-threshold",
                           "ReID reassociate threshold",
                           "Similarity threshold to restore track from dormant gallery",
                           -1.0f,
                           1.0f,
                           pek::tracker::Defaults::reidReassociateThreshold,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_DORMANT_TRACK_HISTORY_SECONDS,
        g_param_spec_float("dormant-track-history-seconds",
                           "Dormant track history seconds",
                           "How long expired track embeddings remain available for reassociation",
                           0.0f,
                           G_MAXFLOAT,
                           pek::tracker::Defaults::dormantTrackHistorySeconds,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_IOU_THRESHOLD,
        g_param_spec_float("iou-threshold",
                           "IoU threshold",
                           "Minimum IoU for matching detection to track",
                           0.0f,
                           1.0f,
                           pek::tracker::Defaults::iouThreshold,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_MAX_MISSED_FRAMES,
        g_param_spec_int("max-missed-frames",
                         "Max missed frames",
                         "Frames to keep unmatched tracks alive",
                         0,
                         G_MAXINT,
                         pek::tracker::Defaults::maxMissedFrames,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_MIN_HITS_TO_CONFIRM,
        g_param_spec_int("min-hits-to-confirm",
                         "Min hits to confirm",
                         "Hits before track is shown",
                         1,
                         G_MAXINT,
                         pek::tracker::Defaults::minHitsToConfirm,
                         (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_APPEND_TRACK_ID_TO_TEXT,
        g_param_spec_boolean("append-track-id-to-text",
                             "Append track ID",
                             "Append [ID:n] to detection text",
                             pek::tracker::Defaults::appendIdentityIdToText,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_TRACE_HISTORY_SECONDS,
        g_param_spec_float(
            "trace-history-seconds",
            "Trace history seconds",
            "Time-window for trace history (stored points: ceil(seconds / kalman-dt))",
            0.0f,
            G_MAXFLOAT,
            pek::tracker::Defaults::traceHistorySeconds,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_DT,
        g_param_spec_float("kalman-dt",
                           "Kalman dt",
                           "Kalman time step",
                           0.0001f,
                           G_MAXFLOAT,
                           pek::tracker::Defaults::kalmanDt,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_INITIAL_COVARIANCE_POS,
        g_param_spec_float("kalman-initial-covariance-pos",
                           "Kalman initial covariance position",
                           "Initial position covariance",
                           0.0f,
                           G_MAXFLOAT,
                           pek::tracker::Defaults::kalmanInitialCovariancePos,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_INITIAL_COVARIANCE_VEL,
        g_param_spec_float("kalman-initial-covariance-vel",
                           "Kalman initial covariance velocity",
                           "Initial velocity covariance",
                           0.0f,
                           G_MAXFLOAT,
                           pek::tracker::Defaults::kalmanInitialCovarianceVel,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_PROCESS_NOISE_POS,
        g_param_spec_float("kalman-process-noise-pos",
                           "Kalman process noise position",
                           "Process noise on position",
                           0.0f,
                           G_MAXFLOAT,
                           pek::tracker::Defaults::kalmanProcessNoisePos,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_PROCESS_NOISE_VEL,
        g_param_spec_float("kalman-process-noise-vel",
                           "Kalman process noise velocity",
                           "Process noise on velocity",
                           0.0f,
                           G_MAXFLOAT,
                           pek::tracker::Defaults::kalmanProcessNoiseVel,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_KALMAN_MEASUREMENT_NOISE_POS,
        g_param_spec_float("kalman-measurement-noise-pos",
                           "Kalman measurement noise position",
                           "Measurement noise on x/y",
                           0.0f,
                           G_MAXFLOAT,
                           pek::tracker::Defaults::kalmanMeasurementNoisePos,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_USE_KALMAN,
        g_param_spec_boolean("use-kalman",
                             "Use Kalman",
                             "Enable Kalman prediction and measurement smoothing",
                             pek::tracker::Defaults::useKalman,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_EMIT_PREDICTED_DETECTIONS,
        g_param_spec_boolean("emit-predicted-detections",
                             "Emit predicted detections",
                             "Append predicted-only tracks when detections are temporarily missing",
                             pek::tracker::Defaults::emitPredictedDetections,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_EMIT_TRACE,
        g_param_spec_boolean("emit-trace",
                             "Emit trace",
                             "Append trackTrace output for active tracks",
                             pek::tracker::Defaults::emitTrace,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobj,
        PROP_ASSOCIATION_MODE,
        g_param_spec_string("association-mode",
                            "Association mode",
                            "Association strategy: hybrid, iou, or embedding",
                            associationModeToString(pek::tracker::Defaults::associationMode),
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
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw, format={BGRA}"));
    static GstStaticPadTemplate src_t = GST_STATIC_PAD_TEMPLATE(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw, format={BGRA}"));
    gst_element_class_add_static_pad_template(ecls, &sink_t);
    gst_element_class_add_static_pad_template(ecls, &src_t);

    gst_element_class_set_static_metadata(ecls,
                                          "PEK Tracker",
                                          "Filter/Effect/Video",
                                          "Tracks detections across frames using Perception meta",
                                          "PEK Development Team");

    bcls->start = gst_pektracker_start;
    bcls->stop = gst_pektracker_stop;
    bcls->set_caps = gst_pektracker_set_caps;
    bcls->transform_ip = gst_pektracker_transform_ip;
}

static void gst_pektracker_init(GstPekTracker *self) {
    self->m = nullptr;

    self->contentType = g_strdup(pek::tracker::Defaults::contentType);
    self->useEmbeddings = pek::tracker::Defaults::useEmbeddings;
    self->embeddingContentType = g_strdup(pek::tracker::Defaults::embeddingContentType);
    self->embeddingWeight = pek::tracker::Defaults::embeddingWeight;
    self->minCosineSimilarity = pek::tracker::Defaults::minCosineSimilarity;
    self->reidReassociateThreshold = pek::tracker::Defaults::reidReassociateThreshold;
    self->dormantTrackHistorySeconds = pek::tracker::Defaults::dormantTrackHistorySeconds;
    self->iouThreshold = pek::tracker::Defaults::iouThreshold;
    self->maxMissedFrames = pek::tracker::Defaults::maxMissedFrames;
    self->minHitsToConfirm = pek::tracker::Defaults::minHitsToConfirm;
    self->appendIdentityIdToText = pek::tracker::Defaults::appendIdentityIdToText;
    self->traceHistorySeconds = pek::tracker::Defaults::traceHistorySeconds;
    self->kalmanDt = pek::tracker::Defaults::kalmanDt;
    self->kalmanInitialCovariancePos = pek::tracker::Defaults::kalmanInitialCovariancePos;
    self->kalmanInitialCovarianceVel = pek::tracker::Defaults::kalmanInitialCovarianceVel;
    self->kalmanProcessNoisePos = pek::tracker::Defaults::kalmanProcessNoisePos;
    self->kalmanProcessNoiseVel = pek::tracker::Defaults::kalmanProcessNoiseVel;
    self->kalmanMeasurementNoisePos = pek::tracker::Defaults::kalmanMeasurementNoisePos;
    self->useKalman = pek::tracker::Defaults::useKalman;
    self->emitPredictedDetections = pek::tracker::Defaults::emitPredictedDetections;
    self->emitTrace = pek::tracker::Defaults::emitTrace;
    self->associationMode =
        g_strdup(associationModeToString(pek::tracker::Defaults::associationMode));
    self->inferId = nullptr;

    gst_video_info_init(&self->vinfo);

    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(self), FALSE);
}

static gboolean pektracker_plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "pektracker", GST_RANK_NONE, GST_TYPE_PEKTRACKER);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  pektracker,
                  "PEK tracker based on Perception metadata",
                  pektracker_plugin_init,
                  "1.0",
                  "LGPL",
                  PACKAGE,
                  "https://arm.com")

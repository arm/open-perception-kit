/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
#include "gst/GstMetaWrapper.h"
#include "gst/gstpad.h"
#include "opk/Tools.h"

#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "gst/FrameResultsMeta.h"
#include "opk/FrameResults.h"
#include "perf/PerformanceMetrics.h"

#ifndef PACKAGE
#define PACKAGE "opk-elements"
#endif

G_BEGIN_DECLS

#define GST_TYPE_OPK_PERFORMANCE (gst_opk_performance_get_type())
#define GST_OPK_PERFORMANCE(obj)                                                                   \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OPK_PERFORMANCE, GstOpkPerformance))
#define GST_OPK_PERFORMANCE_CLASS(klass)                                                           \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_OPK_PERFORMANCE, GstOpkPerformanceClass))
#define GST_IS_OPK_PERFORMANCE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OPK_PERFORMANCE))
#define GST_IS_OPK_PERFORMANCE_CLASS(klass)                                                        \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_OPK_PERFORMANCE))

typedef struct _GstOpkPerformance GstOpkPerformance;
typedef struct _GstOpkPerformanceClass GstOpkPerformanceClass;

struct _GstOpkPerformance {
    GstVideoFilter videofilter;

    // Properties
    gboolean show_all_metrics;
    gboolean enabled;

    // Internal state
    guint frame_count;
    guint update_interval;

    // FPS tracking
    std::chrono::steady_clock::time_point last_frame_time;
    gdouble fps_average;

    // Cached overlay surface
    std::vector<std::string> cached_lines;
    opk::perf::PerformanceMetrics::Snapshot internal_baseline;
    gboolean cache_dirty;
    gboolean started;
    gboolean measurement_inprogress;
};

struct _GstOpkPerformanceClass {
    GstVideoFilterClass parent_class;
};

GType gst_opk_performance_get_type(void);

G_END_DECLS

GST_DEBUG_CATEGORY_STATIC(gst_opk_performance_debug);
#define GST_CAT_DEFAULT gst_opk_performance_debug

// Default values
#define DEFAULT_UPDATE_INTERVAL 5
#define DEFAULT_SHOW_ALL_METRICS FALSE
#define DEFAULT_ENABLED TRUE

static constexpr const char *OPK_SUPPORTED_RAW_VIDEO_CAPS =
    "video/x-raw, format=(string){BGRA,RGB,I420,NV12,YUY2}";

// Property IDs
enum class PropertyId : guint {
    Reserved = 0,
    UpdateInterval,
    ShowAllMetrics,
    Enabled,
};

// Function prototypes
static void gst_opk_performance_set_property(GObject *object,
                                             guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec);
static void
gst_opk_performance_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_opk_performance_finalize(GObject *object);
static GstFlowReturn gst_opk_performance_transform_frame_ip(GstVideoFilter *filter,
                                                            GstVideoFrame *frame);
static gboolean gst_opk_performance_start(GstBaseTransform *trans);
static gboolean gst_opk_performance_stop(GstBaseTransform *trans);
static gboolean gst_opk_performance_sink_event(GstBaseTransform *trans, GstEvent *event);
static gboolean gst_opk_performance_src_event(GstBaseTransform *trans, GstEvent *event);

static void update_measurement_state(GstOpkPerformance *self) {
    const gboolean measurement_is_enabled = self->started && self->enabled;
    if (self->measurement_inprogress == measurement_is_enabled) {
        return;
    }

    if (measurement_is_enabled) {
        self->internal_baseline = opk::perf::defaultPerformanceMetrics().aggregateSnapshot();
    } else {
        self->internal_baseline = {};
    }
    self->measurement_inprogress = measurement_is_enabled;
    self->cache_dirty = true;
}

#define gst_opk_performance_parent_class parent_class
G_DEFINE_TYPE(GstOpkPerformance, gst_opk_performance, GST_TYPE_VIDEO_FILTER);

static void gst_opk_performance_class_init(GstOpkPerformanceClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoFilterClass *vfilter_class = GST_VIDEO_FILTER_CLASS(klass);
    GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gst_opk_performance_set_property;
    gobject_class->get_property = gst_opk_performance_get_property;
    gobject_class->finalize = gst_opk_performance_finalize;

    vfilter_class->transform_frame_ip = gst_opk_performance_transform_frame_ip;
    trans_class->start = gst_opk_performance_start;
    trans_class->stop = gst_opk_performance_stop;
    trans_class->sink_event = gst_opk_performance_sink_event;
    trans_class->src_event = gst_opk_performance_src_event;

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(PropertyId::UpdateInterval),
        g_param_spec_uint("update-interval",
                          "Update Interval",
                          "Update overlay every N frames",
                          1,
                          120,
                          DEFAULT_UPDATE_INTERVAL,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(PropertyId::ShowAllMetrics),
        g_param_spec_boolean("show-all-metrics",
                             "Show All Metrics",
                             "Display all available metrics instead of predefined list",
                             DEFAULT_SHOW_ALL_METRICS,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class,
        static_cast<guint>(PropertyId::Enabled),
        g_param_spec_boolean("enabled",
                             "Enabled",
                             "Enable or disable performance metadata generation",
                             DEFAULT_ENABLED,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    // Set metadata
    gst_element_class_set_static_metadata(
        element_class,
        "OPK Performance Overlay",
        "Filter/Effect/Video",
        "Overlays real-time performance metrics from Performance Metrics",
        "Arm Limited <perception-fdbck@arm.com>");

    // Set pad templates
    GstCaps *caps = gst_caps_from_string(OPK_SUPPORTED_RAW_VIDEO_CAPS);
    GstPadTemplate *src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    GstPadTemplate *sink_template =
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    gst_element_class_add_pad_template(element_class, src_template);
    gst_element_class_add_pad_template(element_class, sink_template);
    gst_caps_unref(caps);
}

static void gst_opk_performance_init(GstOpkPerformance *self) {
    self->frame_count = 0;
    self->update_interval = DEFAULT_UPDATE_INTERVAL;
    self->show_all_metrics = DEFAULT_SHOW_ALL_METRICS;
    std::construct_at(&self->last_frame_time, std::chrono::steady_clock::now());
    self->fps_average = 0.0;
    self->enabled = DEFAULT_ENABLED;
    std::construct_at(&self->cached_lines);
    std::construct_at(&self->internal_baseline);
    self->cache_dirty = true;
    self->started = false;
    self->measurement_inprogress = false;
}

static void gst_opk_performance_finalize(GObject *object) {
    GstOpkPerformance *self = GST_OPK_PERFORMANCE(object);

    GST_OBJECT_LOCK(self);
    self->started = false;
    update_measurement_state(self);
    GST_OBJECT_UNLOCK(self);

    std::destroy_at(&self->internal_baseline);
    std::destroy_at(&self->cached_lines);
    std::destroy_at(&self->last_frame_time);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_opk_performance_set_property(GObject *object,
                                             guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec) {
    GstOpkPerformance *self = GST_OPK_PERFORMANCE(object);

    switch (static_cast<PropertyId>(prop_id)) {
    case PropertyId::UpdateInterval:
        self->update_interval = g_value_get_uint(value);
        break;
    case PropertyId::ShowAllMetrics:
        GST_OBJECT_LOCK(self);
        self->show_all_metrics = g_value_get_boolean(value);
        self->cache_dirty = true;
        GST_OBJECT_UNLOCK(self);
        break;
    case PropertyId::Enabled:
        GST_OBJECT_LOCK(self);
        self->enabled = g_value_get_boolean(value);
        update_measurement_state(self);
        GST_OBJECT_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_opk_performance_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstOpkPerformance *self = GST_OPK_PERFORMANCE(object);

    switch (static_cast<PropertyId>(prop_id)) { // NOSONAR: keep property IDs explicit.
    case PropertyId::UpdateInterval:
        g_value_set_uint(value, self->update_interval);
        break;
    case PropertyId::ShowAllMetrics:
        GST_OBJECT_LOCK(self);
        g_value_set_boolean(value, self->show_all_metrics);
        GST_OBJECT_UNLOCK(self);
        break;
    case PropertyId::Enabled:
        GST_OBJECT_LOCK(self);
        g_value_set_boolean(value, self->enabled);
        GST_OBJECT_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean gst_opk_performance_start(GstBaseTransform *trans) {
    GstOpkPerformance *self = GST_OPK_PERFORMANCE(trans);
    GST_OBJECT_LOCK(self);
    self->started = true;
    update_measurement_state(self);
    GST_OBJECT_UNLOCK(self);
    return TRUE;
}

static gboolean gst_opk_performance_stop(GstBaseTransform *trans) {
    GstOpkPerformance *self = GST_OPK_PERFORMANCE(trans);
    GST_OBJECT_LOCK(self);
    self->started = false;
    update_measurement_state(self);
    GST_OBJECT_UNLOCK(self);
    return TRUE;
}

// Helper function to render overlay to cached surface
static std::vector<std::string> get_performance_data(GstOpkPerformance *self,
                                                     const char *pixel_format) {
    std::vector<opk::perf::ScopeIntervalMetrics> scope_interval_metrics;
    gboolean show_all_metrics;
    gdouble fps_average;

    GST_OBJECT_LOCK(self);
    if (self->measurement_inprogress) {
        auto interval_end_snapshot = opk::perf::defaultPerformanceMetrics().aggregateSnapshot();
        scope_interval_metrics = opk::perf::calculateScopeIntervalMetrics(self->internal_baseline,
                                                                          interval_end_snapshot);
        self->internal_baseline = std::move(interval_end_snapshot);
    }
    show_all_metrics = self->show_all_metrics;
    fps_average = self->fps_average;
    GST_OBJECT_UNLOCK(self);

    std::vector<std::string> lines;
    lines.emplace_back(std::format("{:<24}: {}", "Pixel format", pixel_format));

    if (show_all_metrics) {
        for (const auto &metric : scope_interval_metrics) {
            const double average_duration_ms =
                static_cast<double>(metric.averageDurationNs) / 1000000.0;
            lines.emplace_back(std::format("{:<24}: {:7.2f}ms", metric.name, average_duration_ms));
        }
    } else {
        struct StageIntervalTotals {
            const char *display_name;
            std::string_view metric_name_marker;
            std::uint64_t completed_scope_count = 0;
            std::uint64_t total_duration_ns = 0;
        };

        std::array stage_interval_totals = {StageIntervalTotals{"PreProc", "/GenImgPre/"},
                                            StageIntervalTotals{"Inference", "/Infer/"},
                                            StageIntervalTotals{"PostProc", "/Post/"},
                                            StageIntervalTotals{"OSD", "osd/render"}};
        for (const auto &metric : scope_interval_metrics) {
            const auto stage =
                std::ranges::find_if(stage_interval_totals, [&metric](const auto &item) {
                    return metric.name.find(item.metric_name_marker) != std::string::npos;
                });
            if (stage != stage_interval_totals.end()) {
                stage->completed_scope_count += metric.completedScopeCount;
                stage->total_duration_ns += metric.totalDurationNs;
            }
        }

        for (const auto &stage : stage_interval_totals) {
            if (stage.completed_scope_count == 0) {
                continue;
            }
            const double average_duration_ms = static_cast<double>(stage.total_duration_ns) /
                                               static_cast<double>(stage.completed_scope_count) /
                                               1000000.0;
            lines.emplace_back(
                std::format("{:<24}: {:7.2f}ms", stage.display_name, average_duration_ms));
        }
    }

    // Always include an FPS line so downstream OSD can show at least FPS when
    // no model metrics are available. If FPS not yet measured, show placeholder.
    if (fps_average > 0) {
        lines.emplace_back(std::format("Pipeline                : {:6.1f} FPS", fps_average));
    } else {
        lines.emplace_back("Pipeline                :    --.- FPS");
    }

    return lines;
}

static GstFlowReturn gst_opk_performance_transform_frame_ip(GstVideoFilter *filter,
                                                            GstVideoFrame *frame) {
    GstOpkPerformance *self = GST_OPK_PERFORMANCE(filter);

    GST_OBJECT_LOCK(self);
    if (const gboolean enabled = self->enabled; !enabled) {
        GST_OBJECT_UNLOCK(self);
        return GST_FLOW_OK;
    }

    // Track frame timing for FPS calculation
    auto current_time = std::chrono::steady_clock::now();
    if (self->frame_count > 1) { // Skip first frame
        auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            current_time - self->last_frame_time);
        double frame_ms = frame_duration.count() / 1000.0;
        double instant_fps = frame_ms > 0 ? 1000.0 / frame_ms : 0;
        // Exponential moving average for smoother FPS display
        self->fps_average = (self->fps_average == 0.0)
                                ? instant_fps
                                : (self->fps_average * 0.95 + instant_fps * 0.05);
    }
    self->last_frame_time = current_time;

    self->frame_count++;

    const gboolean cache_should_be_updated =
        self->cache_dirty || (self->frame_count % self->update_interval) == 0;
    self->cache_dirty = false;
    GST_OBJECT_UNLOCK(self);

    if (cache_should_be_updated) {
        // Update cache every N frames
        const auto format = GST_VIDEO_INFO_FORMAT(&frame->info);
        const char *format_name = gst_video_format_to_string(format);
        self->cached_lines = get_performance_data(self, format_name ? format_name : "unknown");
    }

    // Ensure generated FrameResults metadata exists so performance is a standalone payload.
    if (auto frameResultsMeta = opk::FrameResultsMeta::get(frame->buffer); !frameResultsMeta) {
        auto frameResults = std::make_shared<open_perception_kit::FrameResults>();
        opk::FrameResultsMeta::add(frame->buffer, frameResults);
    }

    auto ret =
        opk::FrameResultsMeta::mutate<GstFlowReturn>(frame->buffer, [self](auto &frameResults) {
            open_perception_kit::appendPerformanceOverlay(frameResults, self->cached_lines);
            return GST_FLOW_OK;
        });

    using ME = opk::MetaError;
    if (std::holds_alternative<ME>(ret)) {
        switch (std::get<ME>(ret)) {
        case ME::OK:
        case ME::NO_METADATA:
            // NO_METADATA means no AI model has attached FrameResults yet, which is normal.
            return GST_FLOW_OK;
        }
    } else {
        if (std::get<GstFlowReturn>(ret) != GST_FLOW_OK) {
            return std::get<GstFlowReturn>(ret);
        }
    }

    return GST_FLOW_OK;
}

static gboolean gst_opk_performance_sink_event(GstBaseTransform *trans, GstEvent *event) {
    // Handle downstream events (from upstream elements)
    // Custom control events come via src_event instead
    return GST_BASE_TRANSFORM_CLASS(parent_class)->sink_event(trans, event);
}

static gboolean gst_opk_performance_src_event(GstBaseTransform *trans, GstEvent *event) {
    GstOpkPerformance *self = GST_OPK_PERFORMANCE(trans);

    if (GST_EVENT_TYPE(event) == GST_EVENT_CUSTOM_UPSTREAM) {
        const GstStructure *structure = gst_event_get_structure(event);

        if (structure && gst_structure_has_name(structure, "opkperformance")) {
            gboolean enabled;
            if (gst_structure_get_boolean(structure, "enabled", &enabled)) {
                GST_INFO_OBJECT(self, "Received upstream event: enabled=%d", enabled);
                GST_OBJECT_LOCK(self);
                self->enabled = enabled;
                update_measurement_state(self);
                GST_OBJECT_UNLOCK(self);
            } else {
                GST_WARNING_OBJECT(self, "Received opkperformance event without 'enabled' field");
            }
            gst_event_unref(event);
            return TRUE;
        }
    }

    // Chain up to parent class for other events
    return GST_BASE_TRANSFORM_CLASS(parent_class)->src_event(trans, event);
}

// Plugin initialization
static gboolean opkperformance_plugin_init(GstPlugin *plugin) {
    GST_DEBUG_CATEGORY_INIT(
        gst_opk_performance_debug, "opkperformance", 0, "OPK Performance Overlay");

    return gst_element_register(plugin, "opkperformance", GST_RANK_NONE, GST_TYPE_OPK_PERFORMANCE);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  opkperformance,
                  "OPK Performance Overlay - displays real-time performance metrics",
                  opkperformance_plugin_init,
                  "1.0",
                  "Apache 2.0",
                  PACKAGE,
                  "https://example.com")

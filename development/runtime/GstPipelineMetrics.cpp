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
#include "runtime/GstPipelineMetrics.h"

#include <gst/base/gstbasesink.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace opk::runtime::gst {
namespace {
constexpr std::array deviceIdentityProperties{"device-name",
                                              "device-path",
                                              "device-id",
                                              "camera-name",
                                              "camera-id",
                                              "serial"}; // tcambin camera hardware serial number.

std::string objectPath(GstObject *const object) {
    gchar *const path{gst_object_get_path_string(object)};
    const std::string result{path ? path : ""};
    g_free(path);
    return result;
}

template <typename Callback> void iterate(GstIterator *const iterator, Callback callback) {
    if (!iterator) {
        return;
    }
    GValue value = G_VALUE_INIT;
    bool done{false};
    while (!done) {
        switch (gst_iterator_next(iterator, &value)) {
        case GST_ITERATOR_OK:
            callback(g_value_get_object(&value));
            g_value_reset(&value);
            break;
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(iterator);
            break;
        default:
            done = true;
            break;
        }
    }
    if (G_VALUE_TYPE(&value)) {
        g_value_unset(&value);
    }
    gst_iterator_free(iterator);
}

// Follow ghost/proxy pads as well as ordinary links, so opksink's internal video
// encoder and drain are observed without selecting unrelated pipeline branches.
void findTerminals(GstPad *const pad,
                   std::set<GstPad *> &visited,
                   std::map<std::string, GstPad *> &terminals) {
    if (!visited.insert(pad).second) {
        return;
    }
    GstPad *next{nullptr};
    if (GST_PAD_DIRECTION(pad) == GST_PAD_SRC) {
        next = gst_pad_get_peer(pad);
    } else if (GST_IS_GHOST_PAD(pad)) {
        next = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
    } else if (GST_IS_PROXY_PAD(pad)) {
        next = GST_PAD(gst_proxy_pad_get_internal(GST_PROXY_PAD(pad)));
    } else {
        GstElement *const element{gst_pad_get_parent_element(pad)};
        if (element) {
            bool hasSource{false};
            iterate(gst_element_iterate_src_pads(element), [&](gpointer item) {
                hasSource = true;
                findTerminals(GST_PAD(item), visited, terminals);
            });
            if (!hasSource) {
                const auto path{objectPath(GST_OBJECT(pad))};
                if (!terminals.contains(path)) {
                    terminals.emplace(path, GST_PAD(gst_object_ref(pad)));
                }
            }
            gst_object_unref(element);
        }
    }
    if (next) {
        findTerminals(next, visited, terminals);
        gst_object_unref(next);
    }
}

std::string describeElement(GstElement *const element) {
    std::map<std::string, std::string> properties;
    GstElementFactory *const factory{gst_element_get_factory(element)};
    properties["factory"] = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory))
                                    : G_OBJECT_TYPE_NAME(element);
    guint count{0};
    GParamSpec **const specifications{
        g_object_class_list_properties(G_OBJECT_GET_CLASS(element), &count)};
    for (guint index{0}; index < count; ++index) {
        const auto *const property{specifications[index]};
        const std::string_view name{property->name};
        const bool deviceIdentity{std::ranges::find(deviceIdentityProperties, name) !=
                                  deviceIdentityProperties.end()};
        // These identify a running RTP session, not its processing configuration.
        const bool sessionIdentity{name == "ssrc" || name == "timestamp-offset" ||
                                   name == "seqnum-offset" || name == "stream-id"};
        if (!(property->flags & G_PARAM_READABLE) ||
            (!(property->flags & G_PARAM_WRITABLE) && !deviceIdentity) || name == "name" ||
            sessionIdentity) {
            continue;
        }
        const GType type{G_PARAM_SPEC_VALUE_TYPE(property)};
        const GType fundamental{G_TYPE_FUNDAMENTAL(type)};
        if (fundamental != G_TYPE_STRING && fundamental != G_TYPE_BOOLEAN &&
            fundamental != G_TYPE_INT && fundamental != G_TYPE_UINT &&
            fundamental != G_TYPE_INT64 && fundamental != G_TYPE_UINT64 &&
            fundamental != G_TYPE_LONG && fundamental != G_TYPE_ULONG &&
            fundamental != G_TYPE_FLOAT && fundamental != G_TYPE_DOUBLE &&
            fundamental != G_TYPE_ENUM && fundamental != G_TYPE_FLAGS && type != GST_TYPE_CAPS) {
            continue;
        }
        GValue value = G_VALUE_INIT;
        g_value_init(&value, type);
        g_object_get_property(G_OBJECT(element), property->name, &value);
        gchar *const serialized{gst_value_serialize(&value)};
        properties[property->name] = serialized ? serialized : "";
        g_free(serialized);
        g_value_unset(&value);
    }
    g_free(specifications);
    std::string description;
    for (const auto &[key, value] : properties)
        description +=
            std::to_string(key.size()) + ":" + key + std::to_string(value.size()) + ":" + value;
    return description;
}

// Describe only the path producing the measured video. Per-client RTP/WebRTC branches
// do not alter that path. Factory/settings descriptions exclude auto-generated object names.
void describeUpstream(GstPad *const pad,
                      std::map<GstPad *, size_t> &visited,
                      std::map<GstElement *, size_t> &elements,
                      std::vector<std::string> &description) {
    const auto [padEntry, newPad]{visited.emplace(pad, visited.size())};
    description.push_back("pad:" + std::to_string(padEntry->second));
    if (!newPad)
        return;
    description.push_back(GST_PAD_DIRECTION(pad) == GST_PAD_SINK ? "sink" : "src");
    GstElement *const element{gst_pad_get_parent_element(pad)};
    if (element) {
        const auto [entry, inserted]{elements.emplace(element, elements.size())};
        description.push_back("element:" + std::to_string(entry->second));
        if (inserted)
            description.push_back(describeElement(element));
    }
    GstCaps *const caps{gst_pad_get_current_caps(pad)};
    if (caps) {
        const char *const media{gst_caps_get_size(caps) > 0
                                    ? gst_structure_get_name(gst_caps_get_structure(caps, 0))
                                    : ""};
        if (g_str_has_prefix(media, "video/") || g_str_has_prefix(media, "image/")) {
            gchar *const text{gst_caps_to_string(caps)};
            description.push_back(text ? text : "");
            g_free(text);
        }
        gst_caps_unref(caps);
    }
    GstPad *previous{nullptr};
    if (GST_PAD_DIRECTION(pad) == GST_PAD_SINK)
        previous = gst_pad_get_peer(pad);
    else if (GST_IS_GHOST_PAD(pad))
        previous = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
    else if (GST_IS_PROXY_PAD(pad))
        previous = GST_PAD(gst_proxy_pad_get_internal(GST_PROXY_PAD(pad)));
    else if (element)
        iterate(gst_element_iterate_sink_pads(element), [&](gpointer item) {
            describeUpstream(GST_PAD(item), visited, elements, description);
        });
    if (previous) {
        describeUpstream(previous, visited, elements, description);
        gst_object_unref(previous);
    }
    if (element)
        gst_object_unref(element);
}

} // namespace

struct GstPipelineMetrics::Impl {
    struct Probe {
        GstPad *pad;
        gulong id;
    };
    struct Callback {
        std::shared_ptr<perf::PerformanceMetrics::FrameStream> measurements;
        std::string endpoint;
    };

    GWeakRef anchor{};
    std::shared_ptr<perf::PerformanceMetrics::FrameStream> measurements;
    std::map<std::string, Probe> probes;

    Impl(GstElement *const element,
         std::shared_ptr<perf::PerformanceMetrics::FrameStream> recording)
        : measurements{std::move(recording)} {
        g_weak_ref_init(&anchor, G_OBJECT(element));
    }
    ~Impl() {
        for (const auto &[name, probe] : probes) {
            gst_pad_remove_probe(probe.pad, probe.id);
            gst_object_unref(probe.pad);
        }
        g_weak_ref_clear(&anchor);
    }

    static void destroyCallback(gpointer data) {
        delete static_cast<Callback *>(data);
    }

    static GstPadProbeReturn
    observe(GstPad *const pad, GstPadProbeInfo *const info, gpointer data) {
        auto &callback{*static_cast<Callback *>(data)};
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
            const GstEventType type{GST_EVENT_TYPE(gst_pad_probe_info_get_event(info))};
            if (type == GST_EVENT_CAPS || type == GST_EVENT_SEGMENT ||
                type == GST_EVENT_STREAM_START || type == GST_EVENT_FLUSH_STOP) {
                callback.measurements->discontinuity(callback.endpoint);
            }
            return GST_PAD_PROBE_OK;
        }
        GstCaps *const caps{gst_pad_get_current_caps(pad)};
        const char *const media{caps && gst_caps_get_size(caps) > 0
                                    ? gst_structure_get_name(gst_caps_get_structure(caps, 0))
                                    : ""};
        const bool video{g_str_has_prefix(media, "video/") || g_str_has_prefix(media, "image/")};
        if (caps) {
            gst_caps_unref(caps);
        }
        if (!video) {
            return GST_PAD_PROBE_OK;
        }
        const auto record{[&](GstBuffer *const buffer) {
            if (!GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(buffer)) ||
                GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER)) {
                return;
            }
            callback.measurements->output(GST_BUFFER_PTS(buffer), callback.endpoint);
        }};
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            record(gst_pad_probe_info_get_buffer(info));
        } else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
            GstBufferList *const buffers{gst_pad_probe_info_get_buffer_list(info)};
            for (guint index{0}; index < gst_buffer_list_length(buffers); ++index) {
                record(gst_buffer_list_get(buffers, index));
            }
        }
        return GST_PAD_PROBE_OK;
    }

    void refresh() {
        auto *const element{GST_ELEMENT(g_weak_ref_get(&anchor))};
        if (!element) {
            return;
        }
        std::map<std::string, GstPad *> terminals;
        std::set<GstPad *> visited;
        iterate(gst_element_iterate_src_pads(element),
                [&](gpointer item) { findTerminals(GST_PAD(item), visited, terminals); });
        const auto endpoint{measurements->snapshot().endpoint};
        std::vector<std::string> description;
        std::map<GstElement *, size_t> describedElements;
        std::map<GstPad *, size_t> describedPads;
        const auto terminal{terminals.find(endpoint)};
        if (terminal != terminals.end()) {
            describeUpstream(terminal->second, describedPads, describedElements, description);
        } else {
            // Before the first matched output, retain the source/anchor configuration.
            iterate(gst_element_iterate_sink_pads(element), [&](gpointer item) {
                describeUpstream(GST_PAD(item), describedPads, describedElements, description);
            });
        }
        gst_object_unref(element);
        std::string nextConfiguration;
        for (const auto &value : description)
            nextConfiguration += std::to_string(value.size()) + ":" + value;
        // Source URLs and element properties may contain credentials; retain only their hash.
        gchar *const digest{
            g_compute_checksum_for_string(G_CHECKSUM_SHA256, nextConfiguration.c_str(), -1)};
        nextConfiguration = digest;
        g_free(digest);
        bool changed{false};
        for (auto iterator{probes.begin()}; iterator != probes.end();) {
            const auto found{terminals.find(iterator->first)};
            if (found == terminals.end() || found->second != iterator->second.pad) {
                gst_pad_remove_probe(iterator->second.pad, iterator->second.id);
                gst_object_unref(iterator->second.pad);
                iterator = probes.erase(iterator);
                changed = true;
            } else {
                ++iterator;
            }
        }
        for (const auto &[name, pad] : terminals) {
            if (!probes.contains(name)) {
                auto *const callback{new Callback{measurements, name}};
                const auto types{static_cast<GstPadProbeType>(
                    GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST |
                    GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH)};
                const gulong id{gst_pad_add_probe(pad, types, observe, callback, destroyCallback)};
                if (id != 0) {
                    probes.emplace(name, Probe{GST_PAD(gst_object_ref(pad)), id});
                }
                changed = true;
            }
            gst_object_unref(pad);
        }
        std::vector<std::string> endpoints;
        for (const auto &[name, probe] : probes)
            endpoints.push_back(name);
        measurements->configure(nextConfiguration, endpoints, changed);
    }
};

GstPipelineMetrics::GstPipelineMetrics(
    GstElement *const anchor, std::shared_ptr<perf::PerformanceMetrics::FrameStream> measurements)
    : impl{std::make_unique<Impl>(anchor, std::move(measurements))} {
    impl->refresh();
}

GstPipelineMetrics::~GstPipelineMetrics() = default;

void GstPipelineMetrics::markInput(const GstClockTime timestamp) {
    impl->measurements->input(
        GST_CLOCK_TIME_IS_VALID(timestamp) ? std::optional<uint64_t>{timestamp} : std::nullopt);
}

GstPipelineMetrics::Snapshot GstPipelineMetrics::snapshot() {
    impl->refresh();
    auto result{impl->measurements->snapshot()};
    result.sinkStatsAvailable = false;
    const auto found{impl->probes.find(result.endpoint)};
    if (found != impl->probes.end()) {
        GstElement *const sink{gst_pad_get_parent_element(found->second.pad)};
        if (sink && GST_IS_BASE_SINK(sink)) {
            GstStructure *const stats{gst_base_sink_get_stats(GST_BASE_SINK(sink))};
            result.sinkStatsAvailable =
                gst_structure_get_uint64(stats, "rendered", &result.renderedFrames) &&
                gst_structure_get_uint64(stats, "dropped", &result.droppedFrames);
            gst_structure_free(stats);
        }
        if (sink) {
            gst_object_unref(sink);
        }
    }
    impl->measurements->sinkStatistics(result);
    return impl->measurements->snapshot();
}

} // namespace opk::runtime::gst

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

#include "gst/PromptInputsMeta.h"
#include "gst/PropertyGuard.h"
#include "opk/PromptCodecs.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#ifndef PACKAGE
#define PACKAGE "opk-elements"
#endif

#define GST_TYPE_OPKPROMPT (gst_opkprompt_get_type())
G_DECLARE_FINAL_TYPE(GstOpkPrompt, gst_opkprompt, GST, OPKPROMPT, GstBaseTransform)

struct PromptMembers {
    std::mutex mutex;
    opk::PromptController controller;
    std::string formats = "BPRM";
    std::string inferenceInstance;
    std::string destinationElement;
    std::string sourceName = "pipelineVideoFrame";
    guint width = 0;
    guint height = 0;
};

struct _GstOpkPrompt {
    GstBaseTransform parent;
    PromptMembers *members;
};

G_DEFINE_TYPE(GstOpkPrompt, gst_opkprompt, GST_TYPE_BASE_TRANSFORM)

namespace {

enum Property : guint {
    Reserved,
    InferenceInstance,
    SourceName,
    DestinationElement,
    Formats,
    Capabilities
};
constexpr const char *CommandName = "opk-prompt-command-v1";
constexpr const char *AcknowledgementName = "opk-prompt-ack-v1";

bool includesFormat(const PromptMembers &members, const std::string_view identifier) {
    size_t offset = 0;
    while (offset <= members.formats.size()) {
        const size_t end = members.formats.find(',', offset);
        const std::string_view item(members.formats.data() + offset,
                                    (end == std::string::npos ? members.formats.size() : end) -
                                        offset);
        if (item == identifier) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1;
    }
    return false;
}

bool validFormats(const PromptMembers &members) {
    size_t expectedLength = 0;
    size_t count = 0;
    for (const auto &entry : opk::promptCodecCapabilities()) {
        if (includesFormat(members, entry.format.id)) {
            expectedLength += entry.format.id.size();
            ++count;
        }
    }
    // Identifiers are fixed four-character ASCII strings; reject duplicates/unknown entries.
    return count != 0 && expectedLength + count - 1 == members.formats.size();
}

std::string capabilities(const PromptMembers &members) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto &entry : opk::promptCodecCapabilities()) {
        if (!includesFormat(members, entry.format.id)) {
            continue;
        }
        nlohmann::json item;
        item["format"] = std::string(entry.format.id);
        item["major"] = entry.format.major;
        item["minor"] = entry.format.minor;
        item["maxSerializedBytes"] = entry.maxSerializedBytes;
        item["maxGroups"] = entry.limits.maxGroups;
        item["maxReferences"] = entry.limits.maxReferences;
        item["maxReferenceDimension"] = entry.limits.maxReferenceDimension;
        item["maxReferenceBytes"] = entry.limits.maxReferenceBytes;
        item["maxDescriptionBytes"] = entry.limits.maxDescriptionBytes;
        item["maxGroupNameBytes"] = entry.maxGroupNameBytes;
        entries.push_back(std::move(item));
    }
    return entries.dump();
}

void acknowledge(GstOpkPrompt *const self,
                 const char *const command,
                 const char *const session,
                 const uint64_t requestId,
                 const uint64_t revision,
                 const opk::Result<void> &result) {
    const std::string boundedCommand = command ? std::string(command, strnlen(command, 32)) : "";
    const std::string boundedSession =
        session ? std::string(session, strnlen(session, opk::PromptController::MaxNameBytes)) : "";
    GstStructure *const acknowledgement =
        gst_structure_new(AcknowledgementName,
                          "boundary",
                          G_TYPE_STRING,
                          "opkprompt",
                          "command",
                          G_TYPE_STRING,
                          boundedCommand.c_str(),
                          "session-id",
                          G_TYPE_STRING,
                          boundedSession.c_str(),
                          "request-id",
                          G_TYPE_UINT64,
                          static_cast<guint64>(requestId),
                          "revision",
                          G_TYPE_UINT64,
                          static_cast<guint64>(revision),
                          "accepted",
                          G_TYPE_BOOLEAN,
                          result.has_value(),
                          "reason",
                          G_TYPE_STRING,
                          result ? "" : result.error().info.c_str(),
                          nullptr);
    gst_element_post_message(GST_ELEMENT(self),
                             gst_message_new_element(GST_OBJECT(self), acknowledgement));
}

opk::Result<void> handleCommand(GstOpkPrompt *const self,
                                const GstStructure *const structure,
                                const char *const command,
                                const char *const session,
                                uint64_t &requestId,
                                uint64_t &revision) {
    if (!command || !session || !*session ||
        std::char_traits<char>::length(session) > opk::PromptController::MaxNameBytes) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "Missing or oversized command/session"));
    }
    auto &members = *self->members;
    const std::lock_guard lock(members.mutex);
    if (members.inferenceInstance.empty() || members.destinationElement.empty() ||
        members.sourceName.empty()) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "Prompt destination is not configured"));
    }
    const std::string_view operation(command);
    if (operation == "submit") {
        const GValue *const value = gst_structure_get_value(structure, "payload");
        if (!value || !G_VALUE_HOLDS(value, G_TYPE_BYTES)) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData, "Submit requires GBytes payload"));
        }
        GBytes *const bytes = static_cast<GBytes *>(g_value_get_boxed(value));
        if (!bytes) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, "Null prompt payload"));
        }
        gsize size = 0;
        const auto *const data = static_cast<const uint8_t *>(g_bytes_get_data(bytes, &size));
        const auto *const registration = opk::findPromptCodec(std::span<const uint8_t>{data, size});
        if (!validFormats(members) || !registration) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "Prompt format is not enabled for this ingress"));
        }
        auto snapshot = registration->codec->decode({data, size},
                                                    session,
                                                    members.inferenceInstance,
                                                    members.sourceName,
                                                    requestId,
                                                    revision);
        if (!snapshot) {
            return tl::unexpected(snapshot.error());
        }
        if (!includesFormat(members, registration->format.id)) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "Prompt format is not enabled for this ingress"));
        }
        return members.controller.submit(std::move(*snapshot));
    }
    if (operation == "cancel") {
        guint64 id = 0;
        guint64 rev = 0;
        if (!gst_structure_get_uint64(structure, "request-id", &id) ||
            !gst_structure_get_uint64(structure, "revision", &rev)) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "Cancel requires uint64 request-id/revision"));
        }
        requestId = id;
        revision = rev;
        return members.controller.cancel(session, id, rev);
    }
    if (operation == "end-session") {
        return members.controller.endSession(session);
    }
    return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, "Unknown prompt command"));
}

gboolean sendEvent(GstElement *const element, GstEvent *const event) {
    const GstStructure *const structure = gst_event_get_structure(event);
    if (structure && gst_structure_has_name(structure, CommandName)) {
        auto *const self = GST_OPKPROMPT(element);
        const char *const command = gst_structure_get_string(structure, "command");
        const char *const session = gst_structure_get_string(structure, "session-id");
        uint64_t requestId = 0;
        uint64_t revision = 0;
        const opk::Result<void> result =
            GST_EVENT_TYPE(event) == GST_EVENT_CUSTOM_DOWNSTREAM_OOB
                ? handleCommand(self, structure, command, session, requestId, revision)
                : tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                           "Prompt commands require CUSTOM_DOWNSTREAM_OOB"));
        acknowledge(self, command, session, requestId, revision, result);
        gst_event_unref(event);
        return result.has_value();
    }
    return GST_ELEMENT_CLASS(gst_opkprompt_parent_class)->send_event(element, event);
}

gboolean start(GstBaseTransform *const transform) {
    auto *const self = GST_OPKPROMPT(transform);
    bool valid = false;
    {
        auto &members = *self->members;
        const std::lock_guard lock(members.mutex);
        const auto validName = [](const std::string &name) {
            return !name.empty() && name.size() <= opk::PromptController::MaxNameBytes &&
                   std::ranges::none_of(name, [](const unsigned char value) {
                       return value <= ' ' || value == 127;
                   });
        };
        valid = validName(members.inferenceInstance) && validName(members.sourceName) &&
                validName(members.destinationElement) && validFormats(members);
        const auto active = members.controller.snapshot();
        if (valid && active) {
            const auto *const registration = opk::findPromptCodec(active->payload->format().id);
            valid = registration && includesFormat(members, registration->format.id) &&
                    registration->codec
                        ->validate(*active, members.inferenceInstance, members.sourceName)
                        .has_value();
        }
    }
    if (!valid) {
        GST_ELEMENT_ERROR(
            self,
            RESOURCE,
            SETTINGS,
            ("Prompt binding names must be nonempty, at most 128 bytes, with no "
             "whitespace/control characters; formats must name registered prompt formats"),
            (nullptr));
    }
    return valid;
}

gboolean setCaps(GstBaseTransform *const transform, GstCaps *const input, GstCaps *) {
    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, input)) {
        return FALSE;
    }
    auto &members = *GST_OPKPROMPT(transform)->members;
    const std::lock_guard lock(members.mutex);
    members.width = GST_VIDEO_INFO_WIDTH(&info);
    members.height = GST_VIDEO_INFO_HEIGHT(&info);
    return TRUE;
}

bool supportedRoute(GstOpkPrompt *const self, const std::string &target) {
    // Until geometry transforms are qualified, only known whole-buffer forwarding is supported.
    GstPad *pad = gst_element_get_static_pad(GST_ELEMENT(self), "src");
    constexpr size_t MaxForwarders = 16;
    for (size_t hop = 0; pad && hop <= MaxForwarders; ++hop) {
        GstPad *const peer = gst_pad_get_peer(pad);
        gst_object_unref(pad);
        if (!peer) {
            return false;
        }
        GstElement *const next = gst_pad_get_parent_element(peer);
        gst_object_unref(peer);
        if (!next) {
            return false;
        }
        gchar *const name = gst_object_get_name(GST_OBJECT(next));
        const bool reached = name && target == name;
        g_free(name);
        GstElementFactory *const factory = gst_element_get_factory(next);
        const char *const kind =
            factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : "";
        if (reached) {
            const bool inference = g_str_equal(kind, "opkinfer");
            gst_object_unref(next);
            return inference;
        }
        const bool forwarder = g_str_equal(kind, "identity") || g_str_equal(kind, "queue");
        pad = forwarder ? gst_element_get_static_pad(next, "src") : nullptr;
        gst_object_unref(next);
        if (!forwarder) {
            return false;
        }
    }
    if (pad) {
        gst_object_unref(pad);
    }
    return false;
}

GstFlowReturn attach(GstBaseTransform *const transform, GstBuffer *const buffer) {
    auto *const self = GST_OPKPROMPT(transform);
    auto &members = *self->members;
    std::string failure;
    std::string target;
    std::shared_ptr<const opk::PromptSnapshot> snapshot;
    guint width = 0;
    guint height = 0;
    {
        const std::lock_guard lock(members.mutex);
        if (members.inferenceInstance.empty() || members.destinationElement.empty() ||
            members.width == 0 || members.height == 0 || opk::PromptInputsMeta::get(buffer)) {
            failure = "Invalid prompt binding or duplicate PromptInputsMeta";
        } else {
            snapshot = members.controller.snapshot();
            target = members.destinationElement;
            width = members.width;
            height = members.height;
        }
    }
    if (failure.empty() && snapshot) {
        if (!supportedRoute(self, target)) {
            failure = "Prompt route requires named destination with only identity/queue in between";
        } else if (!opk::PromptInputsMeta::add(buffer, snapshot, target, width, height)) {
            failure = "Unable to attach immutable prompt metadata";
        }
    }
    // Posting can invoke an application's synchronous bus handler and reenter command ingress.
    if (!failure.empty()) {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("%s", failure.c_str()), (nullptr));
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

void setProperty(GObject *const object,
                 const guint id,
                 const GValue *const value,
                 GParamSpec *const spec) {
    auto *const self = GST_OPKPROMPT(object);
    const gst::PropertyGuard guard(GST_ELEMENT(self), spec);
    if (!guard.isChangeAllowed()) {
        return;
    }
    auto &members = *self->members;
    const std::lock_guard lock(members.mutex);
    const char *const text = g_value_get_string(value);
    switch (id) {
    case InferenceInstance:
        members.inferenceInstance = text ? text : "";
        break;
    case DestinationElement:
        members.destinationElement = text ? text : "";
        break;
    case SourceName:
        members.sourceName = text ? text : "";
        break;
    case Formats:
        members.formats = text ? text : "";
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
    }
}

void getProperty(GObject *const object,
                 const guint id,
                 GValue *const value,
                 GParamSpec *const spec) {
    auto &members = *GST_OPKPROMPT(object)->members;
    const std::lock_guard lock(members.mutex);
    switch (id) {
    case InferenceInstance:
        g_value_set_string(value, members.inferenceInstance.c_str());
        break;
    case DestinationElement:
        g_value_set_string(value, members.destinationElement.c_str());
        break;
    case SourceName:
        g_value_set_string(value, members.sourceName.c_str());
        break;
    case Formats:
        g_value_set_string(value, members.formats.c_str());
        break;
    case Capabilities:
        g_value_set_string(value, capabilities(members).c_str());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
    }
}

void finalize(GObject *const object) {
    delete GST_OPKPROMPT(object)->members;
    G_OBJECT_CLASS(gst_opkprompt_parent_class)->finalize(object);
}

} // namespace

static void gst_opkprompt_class_init(GstOpkPromptClass *const klass) {
    auto *const objectClass = G_OBJECT_CLASS(klass);
    auto *const elementClass = GST_ELEMENT_CLASS(klass);
    auto *const transformClass = GST_BASE_TRANSFORM_CLASS(klass);
    objectClass->set_property = setProperty;
    objectClass->get_property = getProperty;
    objectClass->finalize = finalize;
    const auto flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(
        objectClass,
        InferenceInstance,
        g_param_spec_string("inference-instance",
                            "Inference instance",
                            "Destination opkinfer infer-id (or element name)",
                            "",
                            flags));
    g_object_class_install_property(objectClass,
                                    SourceName,
                                    g_param_spec_string("source-name",
                                                        "Source name",
                                                        "Destination OpChain video source",
                                                        "pipelineVideoFrame",
                                                        flags));
    g_object_class_install_property(
        objectClass,
        DestinationElement,
        g_param_spec_string("destination-element",
                            "Destination element",
                            "Name of the opkinfer element that may consume this branch's prompts",
                            "",
                            flags));
    g_object_class_install_property(
        objectClass,
        Formats,
        g_param_spec_string("formats",
                            "Prompt formats",
                            "Comma-separated enabled ingress formats; destination consumers are "
                            "checked independently",
                            "BPRM",
                            flags));
    g_object_class_install_property(
        objectClass,
        Capabilities,
        g_param_spec_string("capabilities",
                            "Prompt capabilities",
                            "JSON decoder limits for the configured ingress formats",
                            "[]",
                            static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    elementClass->send_event = sendEvent;
    transformClass->start = start;
    transformClass->set_caps = setCaps;
    transformClass->transform_ip = attach;
    gst_element_class_set_static_metadata(elementClass,
                                          "OPK prompt ingress",
                                          "Filter/Metadata",
                                          "Binds continuing semantic prompts to video buffers",
                                          "Arm");
    GstCaps *const caps = gst_caps_from_string("video/x-raw");
    gst_element_class_add_pad_template(
        elementClass, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(
        elementClass, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
}

static void gst_opkprompt_init(GstOpkPrompt *const self) {
    self->members = new PromptMembers;
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
}

static gboolean pluginInit(GstPlugin *const plugin) {
    return gst_element_register(plugin, "opkprompt", GST_RANK_NONE, GST_TYPE_OPKPROMPT);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  opkprompt,
                  "OPK prompt ingress",
                  pluginInit,
                  "1.0",
                  "Apache-2.0",
                  "Open Perception Kit",
                  "https://github.com/arm/open-perception-kit")

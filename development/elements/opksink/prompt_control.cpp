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

#include "prompt_control.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "Log.h"
#include "utils.h"
#include <fb/box_prompt_inputs_generated.h>

namespace {

using json = nlohmann::json;
using Element = std::shared_ptr<GstElement>;
constexpr size_t MaxNameBytes = PromptControl::MaxNameBytes;
constexpr size_t MaxPacketBytes = 4096;
constexpr size_t MaxRouteElements = 16;
constexpr size_t MaxReasonBytes = 1024;
constexpr size_t MaxTargets = 64;

Element own(GstElement *const element) {
    return Element(element, [](GstElement *const value) {
        if (value) {
            gst_object_unref(value);
        }
    });
}

std::string elementName(GstElement *const element) {
    g_autofree gchar *name = gst_element_get_name(element);
    return name ? name : "";
}

std::string factoryName(GstElement *const element) {
    GstElementFactory *const factory = gst_element_get_factory(element);
    return factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : "";
}

std::string property(GstElement *const element, const char *const name) {
    gchar *value = nullptr;
    g_object_get(element, name, &value, nullptr);
    const std::string result = value ? value : "";
    g_free(value);
    return result;
}

bool validName(const std::string &value) {
    return !value.empty() && value.size() <= MaxNameBytes &&
           value.find('\0') == std::string::npos &&
           g_utf8_validate(value.data(), value.size(), nullptr);
}

bool validName(const char *const value) {
    return value && *value && strnlen(value, MaxNameBytes + 1) <= MaxNameBytes &&
           g_utf8_validate(value, -1, nullptr);
}

std::string field(const json &message, const char *const name) {
    const auto value = message.at(name).get<std::string>();
    if (!validName(value)) {
        throw std::invalid_argument(std::string("Invalid ") + name);
    }
    return value;
}

uint64_t integer(const json &message, const char *const name) {
    const std::string value = field(message, name);
    uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0) {
        throw std::invalid_argument(std::string("Invalid uint64 ") + name);
    }
    return result;
}

std::vector<Element> elements(GstElement *const pipeline) {
    std::vector<Element> result;
    GstIterator *const iterator = gst_bin_iterate_recurse(GST_BIN(pipeline));
    GValue value = G_VALUE_INIT;
    bool done = false;
    size_t retries = 0;
    while (!done) {
        switch (gst_iterator_next(iterator, &value)) {
        case GST_ITERATOR_OK:
            result.push_back(own(GST_ELEMENT(g_value_dup_object(&value))));
            g_value_reset(&value);
            break;
        case GST_ITERATOR_RESYNC:
            result.clear();
            if (++retries > 3) {
                done = true;
            } else {
                gst_iterator_resync(iterator);
            }
            break;
        case GST_ITERATOR_ERROR:
            result.clear();
            done = true;
            break;
        case GST_ITERATOR_DONE:
            done = true;
            break;
        }
    }
    g_value_unset(&value);
    gst_iterator_free(iterator);
    return result;
}

// Return exactly one src pad. Multiple outputs cannot establish display identity.
GstPad *singleSource(GstElement *const element) {
    GstIterator *const iterator = gst_element_iterate_src_pads(element);
    GValue value = G_VALUE_INIT;
    GstPad *result = nullptr;
    const GstIteratorResult first = gst_iterator_next(iterator, &value);
    if (first == GST_ITERATOR_OK) {
        result = GST_PAD(g_value_dup_object(&value));
        g_value_reset(&value);
        if (gst_iterator_next(iterator, &value) != GST_ITERATOR_DONE) {
            gst_clear_object(&result);
        }
    }
    g_value_unset(&value);
    gst_iterator_free(iterator);
    return result;
}

Element nextElement(GstElement *const element, GstElement *const destination) {
    g_autoptr(GstPad) source = singleSource(element);
    if (!source) {
        return {};
    }
    GstPad *pad = gst_pad_get_peer(source);
    // Ghost/proxy pads may cross a nested bin boundary without changing pixels.
    for (size_t depth = 0; pad && depth < MaxRouteElements; ++depth) {
        if (GST_IS_GHOST_PAD(pad)) {
            GstElement *parent = gst_pad_get_parent_element(pad);
            if (parent == destination) {
                g_autofree gchar *name = gst_pad_get_name(pad);
                const bool videoInput = std::string_view(name) == "sink";
                gst_object_unref(pad);
                if (videoInput) {
                    return own(parent);
                }
                gst_object_unref(parent);
                return {};
            }
            gst_clear_object(&parent);
            GstPad *const target = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
            gst_object_unref(pad);
            pad = target;
            continue;
        }
        GstElement *const parent = gst_pad_get_parent_element(pad);
        if (parent) {
            gst_object_unref(pad);
            return own(parent);
        }
        GstPad *const internal = GST_IS_PROXY_PAD(pad)
                                     ? GST_PAD(gst_proxy_pad_get_internal(GST_PROXY_PAD(pad)))
                                     : nullptr;
        gst_object_unref(pad);
        pad = internal ? gst_pad_get_peer(internal) : nullptr;
        if (internal) {
            gst_object_unref(internal);
        }
    }
    gst_clear_object(&pad);
    return {};
}

bool route(GstElement *const start,
           GstElement *const destination,
           const bool allowResultsPublisher) {
    Element current = own(GST_ELEMENT(gst_object_ref(start)));
    std::set<GstElement *> visited;
    for (size_t depth = 0; depth <= MaxRouteElements; ++depth) {
        if (!visited.insert(current.get()).second) {
            return false;
        }
        const Element next = nextElement(current.get(), destination);
        if (!next) {
            return false;
        }
        if (next.get() == destination) {
            return true;
        }
        const std::string factory = factoryName(next.get());
        if (factory != "identity" && factory != "queue" &&
            !(allowResultsPublisher && factory == "opkcomm")) {
            return false;
        }
        current = next;
    }
    return false;
}

struct RequestIdentity {
    uint64_t id = 0;
    uint64_t revision = 0;
};

// BPRM is selected here at the transport composition boundary; never inspect geometry.
RequestIdentity boxPacketIdentity(const std::span<const uint8_t> bytes) {
    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (open_perception_kit::metadata::VerifyBoxPromptInputsBuffer(verifier)) {
        const auto *const root = open_perception_kit::metadata::GetBoxPromptInputs(bytes.data());
        if (const auto *const envelope = root->request()) {
            return {envelope->request_id(), envelope->revision()};
        }
    }
    return {};
}

std::optional<json> acknowledgement(const GstStructure *const structure) {
    const char *const boundary = gst_structure_get_string(structure, "boundary");
    const char *const session = gst_structure_get_string(structure, "session-id");
    const char *const command = gst_structure_get_string(structure, "command");
    const char *const reason = gst_structure_get_string(structure, "reason");
    guint64 id = 0;
    guint64 revision = 0;
    gboolean accepted = FALSE;
    if (!boundary || std::string_view(boundary) != "opkprompt" || !session || !command || !reason ||
        !validName(session) || !validName(command) ||
        strnlen(reason, MaxReasonBytes + 1) > MaxReasonBytes ||
        !g_utf8_validate(reason, -1, nullptr) ||
        !gst_structure_get_uint64(structure, "request-id", &id) ||
        !gst_structure_get_uint64(structure, "revision", &revision) ||
        !gst_structure_get_boolean(structure, "accepted", &accepted)) {
        return std::nullopt;
    }
    return json{{"session_id", session},
                {"command", command},
                {"reason", reason},
                {"request_id", std::to_string(id)},
                {"revision", std::to_string(revision)},
                {"accepted", accepted != FALSE}};
}

json error(const json &message, const std::string &reason) {
    json result{{"type", "prompt_error"}, {"version", 1}, {"reason", reason}};
    for (const char *const name : {"command_id", "target"}) {
        const auto value = message.find(name);
        if (value != message.end() && value->is_string()) {
            const auto &text = value->get_ref<const std::string &>();
            if (validName(text)) {
                result[name] = text;
            }
        }
    }
    return result;
}

} // namespace

struct PromptControl::Target {
    Element ingress;
    Element destination;
    std::string destinationName;
    std::string inferenceInstance;
    std::string sourceName;
    bool drawingSupported = false;
    std::string reason;
};

struct PromptControl::Session {
    std::string id;
    std::map<std::string, Target> touched;
    std::set<std::string> ended;
    std::set<std::string> cleaning;
    std::set<std::string> ending;
    bool closed = false;
    std::atomic<size_t> pending{0};
    std::atomic<bool> overflow{false};
};

struct PromptControl::Capture {
    PromptControl *const owner;
    GstObject *const source;
    const std::string &session;
    const std::string &operation;
    const uint64_t requestId;
    const uint64_t revision;
    Capture *const previous;
    std::optional<json> reply;

    Capture(PromptControl *const control,
            GstObject *const element,
            const std::string &sessionId,
            const std::string &command,
            const uint64_t id,
            const uint64_t rev)
        : owner(control), source(element), session(sessionId), operation(command), requestId(id),
          revision(rev), previous(capture_) {
        capture_ = this;
    }

    ~Capture() {
        capture_ = previous;
    }
};

thread_local PromptControl::Capture *PromptControl::capture_ = nullptr;

PromptControl::PromptControl(GstElement *const sink, Post post, Notify notify, Post cleanup)
    : post_(std::move(post)), notify_(std::move(notify)), cleanup_(std::move(cleanup)) {
    if (!cleanup_) {
        cleanup_ = [](std::function<void()> callback) { callback(); };
    }
    g_weak_ref_init(&sink_, G_OBJECT(sink));
}

PromptControl::~PromptControl() {
    stop();
    g_weak_ref_clear(&sink_);
}

void PromptControl::start() {
    g_autoptr(GstElement) sink = GST_ELEMENT(g_weak_ref_get(&sink_));
    g_autoptr(GstElement) pipeline = sink ? get_top_pipeline(sink) : nullptr;
    if (!pipeline) {
        active_.store(true);
        return;
    }
    bus_ = gst_element_get_bus(pipeline);
    if (!bus_) {
        throw std::runtime_error("Prompt control requires the pipeline bus");
    }
    active_.store(true);
    auto *const weak = new std::weak_ptr<PromptControl>(weak_from_this());
    observer_ = g_signal_connect_data(
        bus_,
        "sync-message::element",
        G_CALLBACK(PromptControl::busMessage),
        weak,
        [](gpointer data, GClosure *) { delete static_cast<std::weak_ptr<PromptControl> *>(data); },
        G_CONNECT_DEFAULT);
    gst_bus_enable_sync_message_emission(bus_);
    active_.store(true);
}

void PromptControl::stop() {
    if (!active_.exchange(false)) {
        return;
    }
    std::vector<std::string> sessions;
    {
        const std::lock_guard lock(sessionsMutex_);
        for (const auto &[id, session] : sessions_) {
            sessions.push_back(id);
        }
    }
    for (const auto &id : sessions) {
        close(id);
    }
    if (observer_) {
        g_signal_handler_disconnect(bus_, observer_);
        observer_ = 0;
        gst_bus_disable_sync_message_emission(bus_);
    }
    gst_clear_object(&bus_);
}

std::map<std::string, PromptControl::Target> PromptControl::targets() const {
    std::map<std::string, Target> result;
    g_autoptr(GstElement) sink = GST_ELEMENT(g_weak_ref_get(&sink_));
    g_autoptr(GstElement) pipeline = sink ? get_top_pipeline(sink) : nullptr;
    if (!pipeline) {
        return result;
    }
    const auto all = elements(pipeline);
    std::map<std::string, std::vector<Element>> names;
    for (const auto &element : all) {
        names[elementName(element.get())].push_back(element);
    }
    for (const auto &element : all) {
        if (factoryName(element.get()) != "opkprompt" || result.size() == MaxTargets) {
            continue;
        }
        const std::string name = elementName(element.get());
        if (!validName(name) || names[name].size() != 1) {
            continue;
        }
        Target target{.ingress = element,
                      .destination = {},
                      .destinationName = property(element.get(), "destination-element"),
                      .inferenceInstance = property(element.get(), "inference-instance"),
                      .sourceName = property(element.get(), "source-name"),
                      .drawingSupported = false,
                      .reason = {}};
        const auto found = names.find(target.destinationName);
        if (!validName(target.destinationName) || !validName(target.inferenceInstance) ||
            !validName(target.sourceName)) {
            continue;
        }
        if (found == names.end() || found->second.size() != 1 ||
            factoryName(found->second.front().get()) != "opkinfer") {
            target.reason = "Missing or ambiguous inference destination";
        } else {
            target.destination = found->second.front();
            target.drawingSupported = route(element.get(), target.destination.get(), false) &&
                                      route(target.destination.get(), sink, true);
            if (!target.drawingSupported) {
                target.reason = "Display route is not a verified full-frame linear path";
            }
        }
        result.emplace(name, std::move(target));
    }
    return result;
}

nlohmann::json PromptControl::open() {
    g_autofree gchar *uuid = g_uuid_string_random();
    const auto session = std::make_shared<Session>();
    session->id = uuid;
    {
        const std::lock_guard lock(sessionsMutex_);
        sessions_.emplace(session->id, session);
    }
    json discovered = json::array();
    for (const auto &[name, target] : targets()) {
        discovered.push_back({{"target", name},
                              {"destination_element", target.destinationName},
                              {"inference_instance", target.inferenceInstance},
                              {"source_name", target.sourceName},
                              {"format", "BPRM"},
                              {"format_major", 1},
                              {"format_minor", 0},
                              {"drawing_supported", target.drawingSupported},
                              {"reason", target.reason}});
    }
    return {{"type", "prompt_capabilities"},
            {"version", 1},
            {"session_id", session->id},
            {"targets", std::move(discovered)}};
}

void PromptControl::close(const std::string &id) {
    std::shared_ptr<Session> session;
    {
        const std::lock_guard lock(sessionsMutex_);
        const auto found = sessions_.find(id);
        if (found == sessions_.end()) {
            return;
        }
        session = std::move(found->second);
        session->closed = true;
        sessions_.erase(found);
    }
    std::map<std::string, Element> touched;
    {
        const std::lock_guard lock(sessionsMutex_);
        for (const auto &[name, target] : session->touched) {
            if (!session->ended.contains(name) && !session->ending.contains(name) &&
                session->cleaning.insert(name).second) {
                touched.emplace(name, target.ingress);
            }
        }
    }
    if (touched.empty()) {
        return;
    }
    // This owns only ingress references, never a sink/server or adapter pointer.
    cleanup_([id, touched = std::move(touched)] {
        for (const auto &[name, target] : touched) {
            GstStructure *const structure = gst_structure_new("opk-prompt-command-v1",
                                                              "command",
                                                              G_TYPE_STRING,
                                                              "end-session",
                                                              "session-id",
                                                              G_TYPE_STRING,
                                                              id.c_str(),
                                                              nullptr);
            GstEvent *const event =
                gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, structure);
            if (!gst_element_send_event(target.get(), event)) {
                opk::log::warning("Unable to end prompt session {} at {}", id, name);
            }
        }
    });
}

nlohmann::json PromptControl::command(const std::string &id, const json &message) {
    try {
        if (!active_.load()) {
            return error(message, "Prompt control is stopped");
        }
        if (!message.at("version").is_number_integer() || message.at("version") != 1) {
            return error(message, "Unsupported prompt protocol version");
        }
        const std::string commandId = field(message, "command_id");
        const std::string targetName = field(message, "target");
        const std::string operation = field(message, "command");
        std::shared_ptr<Session> session;
        {
            const std::lock_guard lock(sessionsMutex_);
            const auto found = sessions_.find(id);
            if (found == sessions_.end()) {
                return error(message, "Prompt session is closed");
            }
            session = found->second;
        }
        return dispatch(session, commandId, targetName, operation, message);
    } catch (const std::exception &exception) {
        return error(message, std::string("Invalid prompt command: ") + exception.what());
    }
}

nlohmann::json PromptControl::dispatch(const std::shared_ptr<Session> &session,
                                       const std::string &commandId,
                                       const std::string &targetName,
                                       const std::string &operation,
                                       const json &message) {
    const auto available = targets();
    const auto found = available.find(targetName);
    if (found == available.end() || (operation == "submit" && !found->second.destination)) {
        return error(message, "Unknown or invalid prompt target");
    }
    const Target &target = found->second;
    {
        const std::lock_guard lock(sessionsMutex_);
        if (session->closed || !active_.load()) {
            return error(message, "Prompt session is closed");
        }
        if (session->ended.contains(targetName)) {
            return error(message, "Session has ended on this target; reconnect before submitting");
        }
        const auto previous = session->touched.find(targetName);
        if (previous != session->touched.end() && previous->second.ingress != target.ingress) {
            return error(message, "Prompt target was recreated; reconnect before submitting");
        }
    }
    if (operation == "submit" && !target.drawingSupported) {
        return error(message, "Prompt target has no verified display mapping");
    }
    guint64 requestId = 0;
    guint64 revision = 0;
    GstStructure *const structure = gst_structure_new("opk-prompt-command-v1",
                                                      "command",
                                                      G_TYPE_STRING,
                                                      operation.c_str(),
                                                      "session-id",
                                                      G_TYPE_STRING,
                                                      session->id.c_str(),
                                                      nullptr);
    g_autoptr(GstStructure) ownedStructure = structure;
    if (operation == "submit") {
        const auto &encoded = message.at("payload_b64").get_ref<const std::string &>();
        if (encoded.empty() || encoded.size() > ((MaxPacketBytes + 2) / 3) * 4 ||
            encoded.size() % 4 != 0) {
            return error(message, "Invalid or oversized prompt packet");
        }
        gsize size = 0;
        g_autofree guchar *bytes = g_base64_decode(encoded.c_str(), &size);
        g_autofree gchar *canonical = g_base64_encode(bytes, size);
        if (size > MaxPacketBytes || encoded != canonical) {
            return error(message, "Invalid or oversized base64 prompt packet");
        }
        const RequestIdentity identity = boxPacketIdentity({bytes, size});
        requestId = identity.id;
        revision = identity.revision;
        g_autoptr(GBytes) payload = g_bytes_new(bytes, size);
        gst_structure_set(structure, "payload", G_TYPE_BYTES, payload, nullptr);
    } else if (operation == "cancel") {
        requestId = integer(message, "request_id");
        revision = integer(message, "revision");
        gst_structure_set(structure,
                          "request-id",
                          G_TYPE_UINT64,
                          requestId,
                          "revision",
                          G_TYPE_UINT64,
                          revision,
                          nullptr);
    } else if (operation != "end-session") {
        return error(message, "Unknown prompt command");
    }
    if (operation == "end-session") {
        const std::lock_guard lock(sessionsMutex_);
        session->ending.insert(targetName);
    }
    Capture capture(
        this, GST_OBJECT(target.ingress.get()), session->id, operation, requestId, revision);
    GstEvent *const event =
        gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, g_steal_pointer(&ownedStructure));
    const bool accepted = gst_element_send_event(target.ingress.get(), event);
    const bool captured = capture.reply && (*capture.reply)["accepted"] == accepted;
    json reply = captured ? std::move(*capture.reply)
                          : json{{"session_id", session->id},
                                 {"command", operation},
                                 {"request_id", std::to_string(requestId)},
                                 {"revision", std::to_string(revision)},
                                 {"accepted", accepted},
                                 {"reason", "Bus acknowledgement unavailable"}};
    reply["type"] = "prompt_ack";
    reply["version"] = 1;
    reply["command_id"] = commandId;
    reply["target"] = targetName;
    reply["basis"] = captured ? "bus" : "event-return";
    bool cleanup = false;
    {
        const std::lock_guard lock(sessionsMutex_);
        if (accepted && operation == "submit") {
            const bool closed = session->closed || !active_.load();
            cleanup = closed && session->cleaning.insert(targetName).second;
            if (!closed) {
                session->touched.insert_or_assign(targetName, target);
            }
        }
        if (operation == "end-session") {
            session->ending.erase(targetName);
            if (accepted) {
                session->ended.insert(targetName);
            } else if (session->closed && session->touched.contains(targetName)) {
                cleanup = session->cleaning.insert(targetName).second;
            }
        }
    }
    // A host bus callback can stop the server while this submission is accepted.
    if (cleanup) {
        GstStructure *const end = gst_structure_new("opk-prompt-command-v1",
                                                    "command",
                                                    G_TYPE_STRING,
                                                    "end-session",
                                                    "session-id",
                                                    G_TYPE_STRING,
                                                    session->id.c_str(),
                                                    nullptr);
        GstEvent *const cleanupEvent = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, end);
        if (!gst_element_send_event(target.ingress.get(), cleanupEvent)) {
            opk::log::warning("Unable to end in-flight prompt session {}", session->id);
        }
    }
    return reply;
}

void PromptControl::busMessage(GstBus *const, GstMessage *const message, gpointer data) {
    const auto &weak = *static_cast<std::weak_ptr<PromptControl> *>(data);
    if (const auto owner = weak.lock()) {
        owner->observe(message);
    }
}

void PromptControl::observe(GstMessage *const message) {
    if (!active_.load()) {
        return;
    }
    const GstStructure *const structure = gst_message_get_structure(message);
    if (!structure) {
        return;
    }
    if (gst_structure_has_name(structure, "opk-prompt-ack-v1")) {
        Capture *const capture = capture_;
        if (!capture || capture->owner != this || capture->source != GST_MESSAGE_SRC(message) ||
            capture->reply) {
            return;
        }
        auto reply = acknowledgement(structure);
        if (reply && (*reply)["session_id"] == capture->session &&
            (*reply)["command"] == capture->operation &&
            (*reply)["request_id"] == std::to_string(capture->requestId) &&
            (*reply)["revision"] == std::to_string(capture->revision)) {
            capture->reply = std::move(reply);
        }
        return;
    }
    if (!gst_structure_has_name(structure, "opk-prompt-failure-v1")) {
        return;
    }
    const char *const sessionId = gst_structure_get_string(structure, "session-id");
    const char *const reason = gst_structure_get_string(structure, "reason");
    const char *const boundary = gst_structure_get_string(structure, "boundary");
    guint64 requestId = 0;
    guint64 revision = 0;
    if (!sessionId || !validName(sessionId) || !reason || !boundary ||
        strnlen(reason, MaxReasonBytes + 1) > MaxReasonBytes ||
        !g_utf8_validate(reason, -1, nullptr) || !validName(boundary) ||
        std::string_view(boundary) != "opkinfer" ||
        !gst_structure_get_uint64(structure, "request-id", &requestId) ||
        !gst_structure_get_uint64(structure, "revision", &revision)) {
        return;
    }
    std::shared_ptr<Session> session;
    std::string targetName;
    {
        const std::lock_guard lock(sessionsMutex_);
        const auto found = sessions_.find(sessionId);
        if (found == sessions_.end()) {
            return;
        }
        session = found->second;
        for (const auto &[name, target] : session->touched) {
            if (GST_OBJECT(target.destination.get()) == GST_MESSAGE_SRC(message)) {
                targetName = name;
                break;
            }
        }
    }
    if (targetName.empty() || session->overflow.load()) {
        return;
    }
    const bool overflow = session->pending.fetch_add(1) >= MaxPendingNotifications;
    if (overflow && session->overflow.exchange(true)) {
        session->pending.fetch_sub(1);
        return;
    }
    const json notification{{"type", "prompt_failure"},
                            {"version", 1},
                            {"session_id", sessionId},
                            {"target", targetName},
                            {"request_id", std::to_string(requestId)},
                            {"revision", std::to_string(revision)},
                            {"reason", reason},
                            {"boundary", boundary}};
    const auto weak = weak_from_this();
    post_([weak, session, notification, overflow] {
        session->pending.fetch_sub(1);
        if (const auto owner = weak.lock(); owner && owner->active_.load()) {
            bool connected = false;
            {
                const std::lock_guard lock(owner->sessionsMutex_);
                connected = owner->sessions_.contains(session->id);
            }
            if (connected) {
                owner->notify_(session->id, notification, overflow);
            }
        }
    });
}

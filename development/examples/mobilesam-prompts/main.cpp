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

#include "open_perception_kit.h"

#include <cstdint>
#include <cstdio>
#include <gst/gst.h>
#include <memory>
#include <string>

namespace {
using namespace open_perception_kit::metadata;

struct Application {
    GstElement *prompt = nullptr;
    GMainLoop *loop = nullptr;
    std::string session;
    guint step = 0;
    bool failed = false;
};

bool submit(Application &application,
            const uint64_t id,
            const uint64_t revision,
            const double left,
            const double top,
            const double right,
            const double bottom) {
    BoxPromptInputsT input;
    input.request = std::make_unique<RequestEnvelopeT>();
    input.request->request_id = id;
    input.request->revision = revision;
    input.request->destination = std::make_unique<RequestDestinationT>();
    input.request->destination->inference_instance = "mobileSam";
    input.request->destination->source_name = "pipelineVideoFrame";
    input.payload = std::make_unique<BoxPromptPayloadT>();
    input.payload->box = std::make_unique<PromptBoxT>();
    *input.payload->box = PromptBoxT{};
    input.payload->box->left = left;
    input.payload->box->top = top;
    input.payload->box->right = right;
    input.payload->box->bottom = bottom;
    flatbuffers::FlatBufferBuilder builder;
    FinishBoxPromptInputsBuffer(builder, BoxPromptInputs::Pack(builder, &input));
    GBytes *const payload = g_bytes_new(builder.GetBufferPointer(), builder.GetSize());
    GstStructure *const command = gst_structure_new("opk-prompt-command-v1",
                                                    "command",
                                                    G_TYPE_STRING,
                                                    "submit",
                                                    "session-id",
                                                    G_TYPE_STRING,
                                                    application.session.c_str(),
                                                    "payload",
                                                    G_TYPE_BYTES,
                                                    payload,
                                                    nullptr);
    g_bytes_unref(payload);
    return gst_element_send_event(application.prompt,
                                  gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, command));
}

bool command(Application &application,
             const char *const operation,
             const uint64_t id,
             const uint64_t revision) {
    return gst_element_send_event(
        application.prompt,
        gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
                             gst_structure_new("opk-prompt-command-v1",
                                               "command",
                                               G_TYPE_STRING,
                                               operation,
                                               "session-id",
                                               G_TYPE_STRING,
                                               application.session.c_str(),
                                               "request-id",
                                               G_TYPE_UINT64,
                                               static_cast<guint64>(id),
                                               "revision",
                                               G_TYPE_UINT64,
                                               static_cast<guint64>(revision),
                                               nullptr)));
}

// A finite script demonstrates updates during a continuing stream without a GUI/control server.
gboolean advance(gpointer data) {
    auto &application = *static_cast<Application *>(data);
    bool accepted = true;
    constexpr uint64_t RequestId = (uint64_t{1} << 63) + 1;
    switch (application.step++) {
    case 0:
        accepted = submit(application, RequestId, 1, 0.1, 0.1, 0.9, 0.9);
        break;
    case 1:
        accepted = submit(application, RequestId, 2, 0.25, 0.25, 0.75, 0.75);
        break;
    case 2:
        accepted = command(application, "cancel", RequestId, 2);
        break;
    case 3:
        accepted = submit(application, RequestId + 1, 1, 0.1, 0.1, 0.9, 0.9);
        break;
    case 4:
        accepted = command(application, "end-session", 0, 0);
        break;
    default:
        g_main_loop_quit(application.loop);
        return G_SOURCE_REMOVE;
    }
    if (!accepted) {
        application.failed = true;
        g_main_loop_quit(application.loop);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

gboolean busMessage(GstBus *, GstMessage *const message, gpointer data) {
    auto &application = *static_cast<Application *>(data);
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *error = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        std::fprintf(stderr, "%s\n", error->message);
        g_clear_error(&error);
        g_free(debug);
        application.failed = true;
        g_main_loop_quit(application.loop);
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        g_main_loop_quit(application.loop);
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ELEMENT) {
        const GstStructure *const structure = gst_message_get_structure(message);
        if (structure) {
            gchar *const text = gst_structure_to_string(structure);
            std::fprintf(stderr, "%s\n", text);
            g_free(text);
        }
    }
    return G_SOURCE_CONTINUE;
}

} // namespace

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    if (argc < 2 || argc > 3) {
        std::fprintf(
            stderr, "Usage: %s <opchain.json> [source pipeline, default v4l2src]\n", argv[0]);
        return 2;
    }
    const std::string source = argc == 3 ? argv[2] : "v4l2src";
    const std::string description =
        source +
        " ! videoconvert ! video/x-raw,format=BGRA ! opkprompt name=prompts "
        "inference-instance=mobileSam destination-element=mobileSam ! opkinfer name=mobileSam "
        "! opkcomm method=file file-name=- ! fakesink sync=false";
    GError *error = nullptr;
    GstElement *const pipeline = gst_parse_launch(description.c_str(), &error);
    if (error || !pipeline) {
        std::fprintf(stderr, "%s\n", error ? error->message : "Unable to create pipeline");
        g_clear_error(&error);
        if (pipeline) {
            gst_object_unref(pipeline);
        }
        return 1;
    }
    Application application;
    application.loop = g_main_loop_new(nullptr, FALSE);
    application.prompt = gst_bin_get_by_name(GST_BIN(pipeline), "prompts");
    GstElement *const inference = gst_bin_get_by_name(GST_BIN(pipeline), "mobileSam");
    g_object_set(inference, "opchain-path", argv[1], nullptr);
    gst_object_unref(inference);
    gchar *const session = g_uuid_string_random();
    application.session = session;
    g_free(session);
    GstBus *const bus = gst_element_get_bus(pipeline);
    const guint watch = gst_bus_add_watch(bus, busMessage, &application);
    gst_object_unref(bus);
    // Ten-second stages leave time for this CPU model; this is a demonstration, not an FPS claim.
    const guint timer = g_timeout_add_seconds(10, advance, &application);
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        application.failed = true;
    } else {
        g_main_loop_run(application.loop);
    }
    // Explicitly end the session on early exit too; already-bound work remains valid.
    command(application, "end-session", 0, 0);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    if (g_main_context_find_source_by_id(nullptr, timer)) {
        g_source_remove(timer);
    }
    g_source_remove(watch);
    gst_object_unref(application.prompt);
    gst_object_unref(pipeline);
    g_main_loop_unref(application.loop);
    return application.failed ? 1 : 0;
}

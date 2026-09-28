/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include <cstdio>

#include <glib.h>
#include <gst/gst.h>

int main(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(
            stderr, "Usage: %s <opksink-plugin> <factory|operation> <failure-name>\n", argv[0]);
        return 2;
    }

    g_log_set_always_fatal(static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL));
    g_unsetenv("OPK_TEST_OPKSINK_FAIL_ELEMENT");
    g_unsetenv("OPK_TEST_OPKSINK_FAIL_OPERATION");
    if (g_strcmp0(argv[2], "factory") == 0) {
        g_setenv("OPK_TEST_OPKSINK_FAIL_ELEMENT", argv[3], TRUE);
    } else if (g_strcmp0(argv[2], "operation") == 0) {
        g_setenv("OPK_TEST_OPKSINK_FAIL_OPERATION", argv[3], TRUE);
    } else {
        std::fprintf(stderr, "Unknown OpkSink failure kind: %s\n", argv[2]);
        return 2;
    }

    gst_init(nullptr, nullptr);

    GError *error = nullptr;
    GstPlugin *plugin = gst_plugin_load_file(argv[1], &error);
    if (!plugin) {
        std::fprintf(stderr,
                     "Failed to load OpkSink plugin: %s\n",
                     error ? error->message : "unknown error");
        g_clear_error(&error);
        gst_deinit();
        return 1;
    }

    GstElement *sink = gst_element_factory_make("opksink", "factory_failure_sink");
    if (!sink) {
        std::fprintf(stderr, "Failed to create the OpkSink test instance\n");
        gst_object_unref(plugin);
        gst_deinit();
        return 1;
    }

    GstPad *video_pad = gst_element_get_static_pad(sink, "sink");
    if (video_pad) {
        GstPad *upstream_pad = gst_pad_new("factory_failure_src", GST_PAD_SRC);
        gst_pad_set_active(upstream_pad, TRUE);
        gst_pad_set_active(video_pad, TRUE);
        if (gst_pad_link(upstream_pad, video_pad) != GST_PAD_LINK_OK) {
            std::fprintf(stderr, "Failed to link the factory-failure event test pad\n");
            gst_object_unref(upstream_pad);
            gst_object_unref(video_pad);
            gst_object_unref(sink);
            gst_object_unref(plugin);
            gst_deinit();
            return 1;
        }

        GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
                                               gst_structure_new("opk-model-unregister",
                                                                 "element-name",
                                                                 G_TYPE_STRING,
                                                                 "factory-failure-model",
                                                                 nullptr));
        if (gst_pad_push_event(upstream_pad, event)) {
            std::fprintf(stderr, "Partially initialized OpkSink accepted a model event\n");
            gst_pad_unlink(upstream_pad, video_pad);
            gst_pad_set_active(upstream_pad, FALSE);
            gst_object_unref(upstream_pad);
            gst_object_unref(video_pad);
            gst_object_unref(sink);
            gst_object_unref(plugin);
            gst_deinit();
            return 1;
        }

        gst_pad_unlink(upstream_pad, video_pad);
        gst_pad_set_active(upstream_pad, FALSE);
        gst_object_unref(upstream_pad);
        gst_object_unref(video_pad);
    }

    GstPad *audio_pad = gst_element_request_pad_simple(sink, "audiosink");
    if (audio_pad) {
        std::fprintf(stderr, "Partially initialized OpkSink created an audio request pad\n");
        gst_element_release_request_pad(sink, audio_pad);
        gst_object_unref(audio_pad);
        gst_object_unref(sink);
        gst_object_unref(plugin);
        gst_deinit();
        return 1;
    }

    if (gst_element_set_state(sink, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE) {
        std::fprintf(stderr, "Partially initialized OpkSink accepted a state change\n");
        gst_element_set_state(sink, GST_STATE_NULL);
        gst_object_unref(sink);
        gst_object_unref(plugin);
        gst_deinit();
        return 1;
    }

    gst_object_unref(sink);
    gst_object_unref(plugin);
    gst_deinit();
    return 0;
}

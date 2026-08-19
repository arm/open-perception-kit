/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gst/gst.h>

#include <cstdio>

static bool wait_for_message(GstBus *bus, GstMessageType expected) {
    GstMessage *message = gst_bus_timed_pop_filtered(
        bus, 5 * 60 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    if (!message) {
        std::fputs("Timed out waiting for PEKinfer pipeline result\n", stderr);
        return false;
    }

    const GstMessageType actual = GST_MESSAGE_TYPE(message);
    if (actual == GST_MESSAGE_ERROR && expected != GST_MESSAGE_ERROR) {
        GError *error = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        std::fprintf(stderr, "PEKinfer retry failed: %s\n", error->message);
        g_clear_error(&error);
        g_free(debug);
    }

    gst_message_unref(message);
    return actual == expected;
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);

    if (argc != 2) {
        std::fputs("Usage: pekinfer-retry-valgrind-test <valid-opchain-path>\n", stderr);
        return 1;
    }

    GError *parse_error = nullptr;
    GstElement *pipeline =
        gst_parse_launch("videotestsrc num-buffers=1 ! videoconvert ! video/x-raw,format=BGRA ! "
                         "pekinfer name=infer ! fakesink",
                         &parse_error);
    if (!pipeline || parse_error) {
        std::fprintf(stderr,
                     "Could not create PEKinfer pipeline: %s\n",
                     parse_error ? parse_error->message : "unknown error");
        g_clear_error(&parse_error);
        if (pipeline)
            gst_object_unref(pipeline);
        return 1;
    }

    GstElement *infer = gst_bin_get_by_name(GST_BIN(pipeline), "infer");
    GstBus *bus = gst_element_get_bus(pipeline);

    // Fail after allocating the members object.
    g_object_set(infer, "opchain-path", "/does-not-exist/opchain.json", nullptr);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    bool passed = wait_for_message(bus, GST_MESSAGE_ERROR);

    // Retry the same element successfully; the old code lost the first allocation here.
    gst_element_set_state(pipeline, GST_STATE_NULL);
    g_object_set(infer, "opchain-path", argv[1], nullptr);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    passed = wait_for_message(bus, GST_MESSAGE_EOS) && passed;

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(infer);
    gst_object_unref(pipeline);
    gst_deinit();
    return passed ? 0 : 1;
}

/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gst/gst.h>

#include <cstdio>

#include "Inference.h"
#include "pek/ModelDescriptor.h"

static bool check_inference_lifecycle(const char *model_descriptor_path) {
    auto descriptor = pek::ModelDescriptor::fromFile(model_descriptor_path);
    if (!descriptor) {
        std::fprintf(stderr, "Could not load model descriptor: %s\n", model_descriptor_path);
        return false;
    }

    pek::onnx::Inference inference;
    auto invalid_descriptor = *descriptor;
    invalid_descriptor.modelFile = "/does-not-exist/model.onnx";

    if (inference.setup(invalid_descriptor) || inference.isReady()) {
        std::fputs("Invalid ONNX setup unexpectedly succeeded\n", stderr);
        return false;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        auto setup_result = inference.setup(*descriptor);
        if (!setup_result) {
            std::fprintf(stderr, "ONNX setup attempt %d failed\n", attempt + 1);
            return false;
        }

        auto inference_result = inference.inference();
        if (!inference_result) {
            std::fprintf(stderr, "ONNX inference attempt %d failed\n", attempt + 1);
            return false;
        }
    }

    return true;
}

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

static bool check_pekinfer_retry(const char *opchain_path) {
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
        return false;
    }

    GstElement *infer = gst_bin_get_by_name(GST_BIN(pipeline), "infer");
    GstBus *bus = gst_element_get_bus(pipeline);

    // Fail after allocating the members object.
    g_object_set(infer, "opchain-path", "/does-not-exist/opchain.json", nullptr);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    bool passed = wait_for_message(bus, GST_MESSAGE_ERROR);

    // Retry the same element successfully; the old code lost the first allocation here.
    gst_element_set_state(pipeline, GST_STATE_NULL);
    g_object_set(infer, "opchain-path", opchain_path, nullptr);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    passed = wait_for_message(bus, GST_MESSAGE_EOS) && passed;

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(infer);
    gst_object_unref(pipeline);
    return passed;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fputs("Usage: pekinfer-retry-valgrind-test <valid-opchain-path> "
                   "<valid-model-descriptor-path>\n",
                   stderr);
        return 1;
    }

    gst_init(&argc, &argv);
    const bool inference_lifecycle_passed = check_inference_lifecycle(argv[2]);
    const bool pekinfer_retry_passed = check_pekinfer_retry(argv[1]);
    gst_deinit();
    return inference_lifecycle_passed && pekinfer_retry_passed ? 0 : 1;
}

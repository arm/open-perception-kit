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

#include "gst/FrameResultsMeta.h"
#include "opk/QueryPromptInputs.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace open_perception_kit::metadata;
using Element = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;
using Sample = std::unique_ptr<GstSample, decltype(&gst_sample_unref)>;
constexpr GstClockTime Timeout = 30 * GST_SECOND;
constexpr uint64_t RequestId = (uint64_t{1} << 63) + 7;

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
struct Pipeline {
    GstElement *value = nullptr;
    explicit Pipeline(const char *const description) {
        GError *error = nullptr;
        value = gst_parse_launch(description, &error);
        if (error) {
            const std::string message = error->message;
            g_clear_error(&error);
            if (value) {
                gst_object_unref(value);
            }
            throw std::runtime_error(message);
        }
        require(value != nullptr, "Unable to create pipeline");
    }
    ~Pipeline() {
        gst_element_set_state(value, GST_STATE_NULL);
        gst_object_unref(value);
    }
    Element get(const char *const name) const {
        return {gst_bin_get_by_name(GST_BIN(value), name), gst_object_unref};
    }
    void start() const {
        require(gst_element_set_state(value, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
                "Unable to start pipeline");
    }
};

Sample readImage(const char *const path, const bool rgb) {
    Pipeline pipeline(rgb ? "filesrc name=file ! decodebin ! videoconvert ! video/x-raw,format=RGB "
                            "! appsink name=output sync=false"
                          : "filesrc name=file ! decodebin ! videoconvert ! "
                            "video/x-raw,format=BGRA ! appsink name=output sync=false");
    const auto file = pipeline.get("file");
    const auto output = pipeline.get("output");
    g_object_set(file.get(), "location", path, nullptr);
    pipeline.start();
    Sample sample{gst_app_sink_try_pull_sample(GST_APP_SINK(output.get()), Timeout),
                  gst_sample_unref};
    require(sample != nullptr, "Unable to decode input image");
    return sample;
}

std::unique_ptr<PromptReferenceImageT> readReference(const char *const path) {
    const auto sample = readImage(path, true);
    GstVideoInfo info;
    require(gst_video_info_from_caps(&info, gst_sample_get_caps(sample.get())),
            "Invalid image caps");
    const size_t width = GST_VIDEO_INFO_WIDTH(&info);
    const size_t height = GST_VIDEO_INFO_HEIGHT(&info);
    require(width > 0 && height > 0 && width <= opk::MaxPromptReferenceDimension &&
                height <= opk::MaxPromptReferenceDimension &&
                width * height * 3 <= opk::MaxPromptReferenceBytes,
            "Reference image exceeds supported bounds");
    auto image = std::make_unique<PromptReferenceImageT>();
    image->width = width;
    image->height = height;
    image->encoding = PromptReferenceEncoding::RGB8;
    image->pixels.resize(width * height * 3);
    GstVideoFrame frame;
    require(gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample.get()), GST_MAP_READ),
            "Unable to map decoded reference");
    const auto *const pixels = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    const auto stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
    if (stride <= 0 || static_cast<size_t>(stride) < width * 3) {
        gst_video_frame_unmap(&frame);
        throw std::runtime_error("Decoded reference has unsupported row stride");
    }
    for (size_t row = 0; row < height; ++row) {
        std::memcpy(image->pixels.data() + row * width * 3, pixels + row * stride, width * 3);
    }
    gst_video_frame_unmap(&frame);
    return image;
}

std::unique_ptr<RequestEnvelopeT> request(const uint64_t revision) {
    auto result = std::make_unique<RequestEnvelopeT>();
    result->request_id = RequestId;
    result->revision = revision;
    result->destination = std::make_unique<RequestDestinationT>();
    result->destination->inference_instance = "yoloe";
    result->destination->source_name = "pipelineVideoFrame";
    return result;
}

bool command(GstElement *const ingress, const char *const operation, const uint64_t revision) {
    return gst_element_send_event(
        ingress,
        gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
                             gst_structure_new("opk-prompt-command-v1",
                                               "command",
                                               G_TYPE_STRING,
                                               operation,
                                               "session-id",
                                               G_TYPE_STRING,
                                               "native-demo",
                                               "request-id",
                                               G_TYPE_UINT64,
                                               static_cast<guint64>(RequestId),
                                               "revision",
                                               G_TYPE_UINT64,
                                               static_cast<guint64>(revision),
                                               nullptr)));
}

void submit(GstElement *const ingress, const flatbuffers::FlatBufferBuilder &builder) {
    GBytes *const payload = g_bytes_new(builder.GetBufferPointer(), builder.GetSize());
    GstStructure *const structure = gst_structure_new("opk-prompt-command-v1",
                                                      "command",
                                                      G_TYPE_STRING,
                                                      "submit",
                                                      "session-id",
                                                      G_TYPE_STRING,
                                                      "native-demo",
                                                      "payload",
                                                      G_TYPE_BYTES,
                                                      payload,
                                                      nullptr);
    g_bytes_unref(payload);
    require(gst_element_send_event(
                ingress, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, structure)),
            "Prompt command rejected");
}

void run(const int argc, char **const argv) {
    require(argc >= 5,
            "Usage: query-prompts MODEL_DIR TARGET_IMAGE text LABEL... | visual REFERENCE LEFT TOP "
            "RIGHT BOTTOM [...]");
    const std::filesystem::path directory = std::filesystem::absolute(argv[1]);
    const bool visual = std::string_view(argv[3]) == "visual";
    require(visual || std::string_view(argv[3]) == "text", "Mode must be text or visual");
    const auto target = readImage(argv[2], false);
    Pipeline pipeline(
        "appsrc name=source format=time is-live=true ! opkprompt name=prompts ! opkinfer "
        "name=query active=true ! appsink name=output sync=false async=false");
    const auto source = pipeline.get("source");
    const auto ingress = pipeline.get("prompts");
    const auto infer = pipeline.get("query");
    const auto output = pipeline.get("output");
    const auto chain = directory / (visual ? "opchain-visual.json" : "opchain.json");
    g_object_set(infer.get(), "opchain-path", chain.c_str(), nullptr);
    g_object_set(ingress.get(),
                 "destination-element",
                 "query",
                 "inference-instance",
                 "yoloe",
                 "formats",
                 visual ? "VPRM" : "TPRM",
                 nullptr);
    gst_app_src_set_caps(GST_APP_SRC(source.get()), gst_sample_get_caps(target.get()));
    pipeline.start();
    VisualPromptInputsT examples;
    if (visual) {
        require((argc - 4) % 5 == 0 && (argc - 4) / 5 <= static_cast<int>(opk::MaxPromptReferences),
                "Visual groups need REFERENCE LEFT TOP RIGHT BOTTOM, at most four references");
        examples.payload = std::make_unique<VisualPromptPayloadT>();
        for (int index = 4; index < argc; index += 5) {
            auto group = std::make_unique<VisualPromptGroupT>();
            group->group_id = "example-" + std::to_string((index - 4) / 5);
            group->reference_index = examples.payload->references.size();
            group->box = std::make_unique<VisualPromptBoxT>();
            group->box->left = std::stod(argv[index + 1]);
            group->box->top = std::stod(argv[index + 2]);
            group->box->right = std::stod(argv[index + 3]);
            group->box->bottom = std::stod(argv[index + 4]);
            examples.payload->references.push_back(readReference(argv[index]));
            examples.payload->groups.push_back(std::move(group));
        }
    }
    uint64_t revision = 0;
    const auto frame = [&](const bool expected) {
        GstBuffer *const buffer = gst_buffer_copy_deep(gst_sample_get_buffer(target.get()));
        require(buffer != nullptr, "Unable to copy target frame");
        GST_BUFFER_PTS(buffer) = revision * GST_SECOND;
        GST_BUFFER_DURATION(buffer) = GST_SECOND;
        require(gst_app_src_push_buffer(GST_APP_SRC(source.get()), buffer) == GST_FLOW_OK,
                "Frame rejected");
        const Sample sample{gst_app_sink_try_pull_sample(GST_APP_SINK(output.get()), Timeout),
                            gst_sample_unref};
        require(sample != nullptr,
                "Inference did not return a frame; inspect GStreamer diagnostics");
        const auto *const meta = opk::FrameResultsMeta::get(gst_sample_get_buffer(sample.get()));
        const auto association =
            meta && meta->payload ? meta->payload->get<PromptAssociationT>() : std::nullopt;
        require(static_cast<bool>(association) == expected,
                "Unexpected stale or missing prompt association");
        if (association) {
            require((*association)->request->revision == revision &&
                        (*association)->request->request_id == RequestId,
                    "Result identity does not match submitted request");
            std::cout << "revision=" << revision << " matches=" << (*association)->matches.size();
            for (const auto &match : (*association)->matches) {
                std::cout << ' ' << match->object_id << ':' << match->group_id;
            }
            std::cout << '\n';
        }
    };
    frame(false);
    const int updates = visual ? 2 : argc - 4;
    for (int update = 0; update < updates; ++update) {
        ++revision;
        flatbuffers::FlatBufferBuilder builder;
        if (visual) {
            examples.request = request(revision);
            if (update > 0) {
                std::reverse(examples.payload->groups.begin(), examples.payload->groups.end());
            }
            FinishVisualPromptInputsBuffer(builder, VisualPromptInputs::Pack(builder, &examples));
        } else {
            TextPromptInputsT text;
            text.request = request(revision);
            text.payload = std::make_unique<TextPromptPayloadT>();
            auto group = std::make_unique<TextPromptGroupT>();
            group->group_id = "category-" + std::to_string(update);
            group->description = argv[update + 4];
            text.payload->groups.push_back(std::move(group));
            FinishTextPromptInputsBuffer(builder, TextPromptInputs::Pack(builder, &text));
        }
        submit(ingress.get(), builder);
        builder.Clear(); // The command owns bytes; submission storage is immediately reusable.
        frame(true);
        frame(true); // Stable revision reuses prepared conditioning.
    }
    require(command(ingress.get(), "cancel", revision), "Cancel rejected");
    frame(false);
    require(command(ingress.get(), "end-session", ++revision), "Session end rejected");
    frame(false);
    std::cout << "Native prompt lifecycle complete\n";
}
} // namespace

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    try {
        run(argc, argv);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

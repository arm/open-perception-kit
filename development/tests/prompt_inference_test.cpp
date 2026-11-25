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
#include "gst/PromptInputsMeta.h"
#include "opk/BoxPromptInputs.h"
#include "opk/QueryPromptInputs.h"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace open_perception_kit::metadata;
using Sample = std::unique_ptr<GstSample, decltype(&gst_sample_unref)>;
using Message = std::unique_ptr<GstMessage, decltype(&gst_message_unref)>;
constexpr uint64_t RequestId = (uint64_t{1} << 63) + 7;
constexpr GstClockTime Timeout = 5 * GST_SECOND;

std::shared_ptr<const opk::PromptSnapshot> snapshot(const uint64_t revision = 1,
                                                    const double left = 0.0) {
    RequestEnvelopeT request;
    request.request_id = RequestId;
    request.revision = revision;
    request.destination = std::make_unique<RequestDestinationT>();
    request.destination->inference_instance = "sam";
    request.destination->source_name = "pipelineVideoFrame";
    PromptBoxT box;
    box.left = left;
    box.right = 1.0;
    box.bottom = 1.0;
    const auto payload = std::make_shared<const opk::BoxPromptPayload>(box);
    return std::make_shared<const opk::PromptSnapshot>("first", std::move(request), payload);
}

class PromptInference : public ::testing::Test {
  protected:
    GstElement *pipeline = nullptr;
    GstElement *source = nullptr;
    GstElement *infer = nullptr;
    GstElement *ingress = nullptr;
    GstElement *sink = nullptr;
    GstBus *bus = nullptr;
    std::atomic<bool> reentryAttempted{false};
    std::atomic<bool> reentryAccepted{false};
    std::array<std::atomic<unsigned>, 2> calls{};
    std::array<std::atomic<unsigned>, 2> promptedCalls{};
    std::filesystem::path directory;
    std::mutex gateMutex;
    std::condition_variable gateChanged;
    bool gateBlocked = false;
    GstPad *gatePad = nullptr;
    gulong gateProbeId = 0;
    std::mutex capturedMutex;
    std::vector<std::shared_ptr<const opk::PromptSnapshot>> captured;

    void SetUp() override {
        gst_init(nullptr, nullptr);
        g_autoptr(GError) error = nullptr;
        gchar *const path = g_dir_make_tmp("opk-prompt-inference-XXXXXX", &error);
        ASSERT_EQ(error, nullptr) << (error ? error->message : "");
        ASSERT_NE(path, nullptr);
        directory = path;
        g_free(path);
    }

    void TearDown() override {
        releasePairedBuffer();
        if (bus) {
            gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        }
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
        }
        for (GstElement *const element : {source, infer, ingress, sink}) {
            if (element) {
                gst_object_unref(element);
            }
        }
        if (bus) {
            gst_object_unref(bus);
        }
        if (pipeline) {
            gst_object_unref(pipeline);
        }
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }

    std::filesystem::path chain(const bool supported, const size_t index) {
        const nlohmann::json descriptor = {
            {"version", "3.0.0"},
            {"name", "PromptProbe"},
            {"description", "Test-only inference scheduling and prompt routing probe."},
            {"execution", {{"preprocessing", "cpu"}}},
            {"ops",
             {{{"role", "intermediate-process"},
               {"id",
                supported ? "opk-test-prompt-probe/PromptInference"
                          : "opk-test-prompt-probe/PlainInference"},
               {"instanceId", "sam"},
               {"attributes",
                {{"counterAddress", reinterpret_cast<std::uintptr_t>(&calls[index])},
                 {"promptCounterAddress",
                  reinterpret_cast<std::uintptr_t>(&promptedCalls[index])}}}}}}};
        const auto path = directory / ("opchain-" + std::to_string(index) + ".json");
        std::ofstream output(path);
        output << descriptor.dump();
        EXPECT_TRUE(output.good());
        return path;
    }

    bool start(const bool supported = true,
               const bool withIngress = false,
               const bool withDownstream = false,
               const bool active = true,
               const std::string &bridge = "",
               const std::string &formats = "BPRM") {
        std::string description = "appsrc name=source format=time ! ";
        if (withIngress) {
            description +=
                "opkprompt name=prompts inference-instance=sam destination-element=infer formats=" +
                formats + " ! ";
        }
        description += bridge;
        description += "opkinfer name=infer opchain-path=" + chain(supported, 0).string() + " ";
        description += active ? "active=true ! " : "active=false ! ";
        if (withDownstream) {
            // Same internal ID, but no box capability: any leaked prompt must fail here.
            description += "opkinfer name=unrelated opchain-path=" + chain(false, 1).string() +
                           " active=true ! ";
        }
        description += "appsink name=output sync=false async=false";
        GError *error = nullptr;
        pipeline = gst_parse_launch(description.c_str(), &error);
        if (error) {
            ADD_FAILURE() << error->message;
            g_clear_error(&error);
            return false;
        }
        if (!pipeline) {
            return false;
        }
        source = gst_bin_get_by_name(GST_BIN(pipeline), "source");
        infer = gst_bin_get_by_name(GST_BIN(pipeline), "infer");
        ingress = gst_bin_get_by_name(GST_BIN(pipeline), "prompts");
        sink = gst_bin_get_by_name(GST_BIN(pipeline), "output");
        bus = gst_element_get_bus(pipeline);
        GstCaps *const caps =
            gst_caps_from_string("video/x-raw,format=BGRA,width=2,height=2,framerate=1/1");
        gst_app_src_set_caps(GST_APP_SRC(source), caps);
        gst_caps_unref(caps);
        return gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE;
    }

    bool push(const GstClockTime pts = 0,
              std::shared_ptr<const opk::PromptSnapshot> prompt = nullptr,
              const std::string &target = "infer") {
        GstBuffer *const buffer = gst_buffer_new_allocate(nullptr, 16, nullptr);
        gst_buffer_memset(buffer, 0, 0, 16);
        GST_BUFFER_PTS(buffer) = pts;
        GST_BUFFER_DURATION(buffer) = GST_SECOND;
        if (prompt && !opk::PromptInputsMeta::add(buffer, std::move(prompt), target, 2, 2)) {
            gst_buffer_unref(buffer);
            return false;
        }
        return gst_app_src_push_buffer(GST_APP_SRC(source), buffer) == GST_FLOW_OK;
    }

    Sample pull() {
        return {gst_app_sink_try_pull_sample(GST_APP_SINK(sink), Timeout), gst_sample_unref};
    }

    void finishStream() {
        ASSERT_EQ(gst_app_src_end_of_stream(GST_APP_SRC(source)), GST_FLOW_OK);
        const Message message(
            gst_bus_timed_pop_filtered(
                bus, Timeout, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)),
            gst_message_unref);
        ASSERT_TRUE(message);
        ASSERT_EQ(GST_MESSAGE_TYPE(message.get()), GST_MESSAGE_EOS);
    }

    bool submit(const char *const session, const uint64_t revision, const double left = 0.0) {
        const auto input = snapshot(revision, left);
        BoxPromptInputsT wire;
        auto request = input->copyRequest();
        wire.request = std::make_unique<RequestEnvelopeT>(std::move(request));
        wire.payload = std::make_unique<BoxPromptPayloadT>();
        const auto &box = dynamic_cast<const opk::BoxPromptPayload &>(*input->payload).box;
        wire.payload->box = std::make_unique<PromptBoxT>(box);
        flatbuffers::FlatBufferBuilder builder;
        FinishBoxPromptInputsBuffer(builder, BoxPromptInputs::Pack(builder, &wire));
        return submitBytes(session, {builder.GetBufferPointer(), builder.GetSize()});
    }

    bool submitBytes(const char *const session, const std::span<const uint8_t> payload) {
        GBytes *const bytes = g_bytes_new(payload.data(), payload.size());
        GstStructure *const structure = gst_structure_new("opk-prompt-command-v1",
                                                          "command",
                                                          G_TYPE_STRING,
                                                          "submit",
                                                          "session-id",
                                                          G_TYPE_STRING,
                                                          session,
                                                          "payload",
                                                          G_TYPE_BYTES,
                                                          bytes,
                                                          nullptr);
        g_bytes_unref(bytes);
        return gst_element_send_event(
            ingress, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, structure));
    }

    bool submitQuery(const bool visual,
                     const uint64_t revision,
                     const bool valid = true,
                     const char *const session = "first") {
        const auto identity = snapshot(revision)->copyRequest();
        flatbuffers::FlatBufferBuilder builder;
        if (visual) {
            VisualPromptInputsT wire;
            wire.request = std::make_unique<RequestEnvelopeT>(identity);
            wire.payload = std::make_unique<VisualPromptPayloadT>();
            auto reference = std::make_unique<PromptReferenceImageT>();
            reference->width = 1;
            reference->height = 1;
            reference->encoding = PromptReferenceEncoding::RGB8;
            reference->pixels = {10, 20, 30};
            wire.payload->references.push_back(std::move(reference));
            auto group = std::make_unique<VisualPromptGroupT>();
            group->group_id = "example";
            group->box = std::make_unique<VisualPromptBoxT>();
            group->box->right = valid ? 1 : -1;
            group->box->bottom = 1;
            wire.payload->groups.push_back(std::move(group));
            FinishVisualPromptInputsBuffer(builder, VisualPromptInputs::Pack(builder, &wire));
        } else {
            TextPromptInputsT wire;
            wire.request = std::make_unique<RequestEnvelopeT>(identity);
            wire.payload = std::make_unique<TextPromptPayloadT>();
            auto group = std::make_unique<TextPromptGroupT>();
            group->group_id = "people";
            group->description = valid ? "person" : "";
            wire.payload->groups.push_back(std::move(group));
            FinishTextPromptInputsBuffer(builder, TextPromptInputs::Pack(builder, &wire));
        }
        return submitBytes(session, {builder.GetBufferPointer(), builder.GetSize()});
    }

    bool command(const char *const operation, const char *const session, const uint64_t revision) {
        return gst_element_send_event(
            ingress,
            gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
                                 gst_structure_new("opk-prompt-command-v1",
                                                   "command",
                                                   G_TYPE_STRING,
                                                   operation,
                                                   "session-id",
                                                   G_TYPE_STRING,
                                                   session,
                                                   "request-id",
                                                   G_TYPE_UINT64,
                                                   static_cast<guint64>(RequestId),
                                                   "revision",
                                                   G_TYPE_UINT64,
                                                   static_cast<guint64>(revision),
                                                   nullptr)));
    }

    static GstPadProbeReturn capture(GstPad *, GstPadProbeInfo *const info, gpointer userData) {
        auto &self = *static_cast<PromptInference *>(userData);
        const auto *const meta = opk::PromptInputsMeta::get(GST_PAD_PROBE_INFO_BUFFER(info));
        const std::lock_guard lock(self.capturedMutex);
        self.captured.push_back(meta ? meta->snapshot : nullptr);
        return GST_PAD_PROBE_OK;
    }

    void captureBeforeConsumption() {
        GstPad *const pad = gst_element_get_static_pad(infer, "sink");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, capture, this, nullptr);
        gst_object_unref(pad);
    }

    std::shared_ptr<const opk::PromptSnapshot> bound(const size_t index) {
        const std::lock_guard lock(capturedMutex);
        return captured.at(index);
    }

    static GstPadProbeReturn holdPairedBuffer(GstPad *, GstPadProbeInfo *, gpointer userData) {
        auto &self = *static_cast<PromptInference *>(userData);
        const std::lock_guard lock(self.gateMutex);
        self.gateBlocked = true;
        self.gateChanged.notify_all();
        return GST_PAD_PROBE_OK;
    }

    void blockPairedBuffer() {
        gatePad = gst_element_get_static_pad(infer, "sink");
        gateProbeId = gst_pad_add_probe(
            gatePad,
            static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER),
            holdPairedBuffer,
            this,
            nullptr);
    }

    bool waitForPairedBuffer() {
        std::unique_lock lock(gateMutex);
        return gateChanged.wait_for(lock, std::chrono::seconds(5), [this] { return gateBlocked; });
    }

    void releasePairedBuffer() {
        GstPad *const pad = std::exchange(gatePad, nullptr);
        const gulong id = std::exchange(gateProbeId, 0);
        if (pad) {
            gst_pad_remove_probe(pad, id);
            gst_object_unref(pad);
        }
    }

    static GstBusSyncReply reenterSubmit(GstBus *, GstMessage *const message, gpointer userData) {
        auto &self = *static_cast<PromptInference *>(userData);
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR &&
            GST_MESSAGE_SRC(message) == GST_OBJECT(self.ingress) &&
            !self.reentryAttempted.exchange(true)) {
            self.reentryAccepted.store(self.submit("first", 2, 0.25));
        }
        return GST_BUS_PASS;
    }

    void expectErrorBeforeInvocation(const unsigned previousCalls = 0) {
        const Message message(gst_bus_timed_pop_filtered(bus, Timeout, GST_MESSAGE_ERROR),
                              gst_message_unref);
        ASSERT_TRUE(message);
        EXPECT_EQ(GST_MESSAGE_SRC(message.get()), GST_OBJECT(infer));
        EXPECT_EQ(calls[0].load(), previousCalls);
        EXPECT_EQ(promptedCalls[0].load(), 0U);
    }
};

class TestPayload final : public opk::PromptPayload {
  public:
    explicit TestPayload(const opk::PromptFormat format) : value(format) {}
    size_t ownedBytes() const override {
        return sizeof(*this);
    }
    opk::PromptFormat format() const override {
        return value;
    }

  private:
    const opk::PromptFormat value;
};

TEST_F(PromptInference, GenericPayloadReachesConsumerAndAssociatesNonMaskResult) {
    ASSERT_TRUE(start());
    auto prompt = std::make_shared<opk::PromptSnapshot>(*snapshot());
    prompt->payload = std::make_shared<const TestPayload>(opk::PromptFormat{"TEST", 1, 0});
    ASSERT_TRUE(push(0, prompt));
    const auto sample = pull();
    ASSERT_TRUE(sample);
    const auto results = opk::FrameResultsMeta::read(gst_sample_get_buffer(sample.get()));
    ASSERT_TRUE(results);
    EXPECT_EQ(results->count<BoxDetectionsT>(), 1U);
    EXPECT_EQ(results->count<InstanceSegmentationMasksT>(), 0U);
    EXPECT_EQ(results->count<PromptAssociationT>(), 1U);
    results->for_each<PromptAssociationT>([](const auto &association) {
        EXPECT_EQ(association.session_id, "first");
        EXPECT_EQ(association.request->request_id, RequestId);
        EXPECT_EQ(association.request->revision, 1U);
        ASSERT_NE(association.layer->producer, nullptr);
        EXPECT_EQ(association.layer->producer->implementation, "scheduling-probe");
        EXPECT_NE(association.parent_id, 0U);
    });
}

TEST_F(PromptInference, EmptyPayloadIsConsumedAsAbsenceWithoutFabricatedAssociation) {
    ASSERT_TRUE(start());
    auto prompt = std::make_shared<opk::PromptSnapshot>(*snapshot());
    prompt->payload.reset();
    ASSERT_TRUE(push(0, std::move(prompt)));
    const auto sample = pull();
    ASSERT_TRUE(sample);
    EXPECT_EQ(opk::PromptInputsMeta::get(gst_sample_get_buffer(sample.get())), nullptr);
    EXPECT_EQ(calls[0].load(), 1U);
    EXPECT_EQ(promptedCalls[0].load(), 0U);
    const auto results = opk::FrameResultsMeta::read(gst_sample_get_buffer(sample.get()));
    ASSERT_TRUE(results);
    EXPECT_EQ(results->count<PromptAssociationT>(), 0U);
}

TEST_F(PromptInference, UnsupportedPayloadFormatFailsBeforeConsumerInvocation) {
    ASSERT_TRUE(start());
    auto prompt = std::make_shared<opk::PromptSnapshot>(*snapshot());
    prompt->payload = std::make_shared<const TestPayload>(opk::PromptFormat{"UNKNOWN", 1, 0});
    ASSERT_TRUE(push(0, prompt));
    expectErrorBeforeInvocation();
}

TEST_F(PromptInference, UnsupportedPayloadVersionFailsBeforeConsumerInvocation) {
    ASSERT_TRUE(start());
    auto prompt = std::make_shared<opk::PromptSnapshot>(*snapshot());
    prompt->payload = std::make_shared<const TestPayload>(opk::PromptFormat{"BPRM", 2, 0});
    ASSERT_TRUE(push(0, prompt));
    expectErrorBeforeInvocation();
}

TEST_F(PromptInference, MatchingInferenceIdWithoutBoxCapabilityFailsBeforePromptedInvocation) {
    ASSERT_TRUE(start(false));
    ASSERT_TRUE(push());
    const auto unprompted = pull();
    ASSERT_TRUE(unprompted);
    ASSERT_EQ(calls[0].load(), 1U);
    ASSERT_TRUE(push(GST_SECOND, snapshot()));
    expectErrorBeforeInvocation(1U);
}

TEST_F(PromptInference, ExplicitElementNameRejectsMisrouteDespiteMatchingInternalInferenceId) {
    ASSERT_TRUE(start());
    ASSERT_TRUE(push(0, snapshot(), "other-branch-infer"));
    expectErrorBeforeInvocation();
}

TEST_F(PromptInference, InactiveTargetConsumesAndReleasesPromptBeforeUnrelatedInference) {
    ASSERT_TRUE(start(true, false, true, false));
    auto prompt = snapshot();
    const std::weak_ptr<const opk::PromptSnapshot> lifetime = prompt;
    ASSERT_TRUE(push(0, std::move(prompt)));
    const auto sample = pull();
    ASSERT_TRUE(sample);
    EXPECT_EQ(opk::PromptInputsMeta::get(gst_sample_get_buffer(sample.get())), nullptr);
    // EOS follows delivery on the source task, after temporary input references release.
    finishStream();
    EXPECT_TRUE(lifetime.expired());
    EXPECT_EQ(calls[0].load(), 0U);
    EXPECT_EQ(calls[1].load(), 1U);
    EXPECT_EQ(promptedCalls[1].load(), 0U);
}

TEST_F(PromptInference, QosSkippedTargetConsumesAndReleasesPromptBeforeUnrelatedInference) {
    ASSERT_TRUE(start(true, false, true));
    ASSERT_TRUE(push());
    const auto prime = pull();
    ASSERT_TRUE(prime);
    ASSERT_EQ(calls[0].load(), 1U);
    g_object_set(infer, "qos-enabled", TRUE, nullptr);
    GstPad *const pad = gst_element_get_static_pad(infer, "src");
    const bool accepted =
        gst_pad_send_event(pad, gst_event_new_qos(GST_QOS_TYPE_UNDERFLOW, 0.75, 2 * GST_SECOND, 0));
    gst_object_unref(pad);
    ASSERT_TRUE(accepted);
    auto prompt = snapshot();
    const std::weak_ptr<const opk::PromptSnapshot> lifetime = prompt;
    ASSERT_TRUE(push(GST_SECOND, std::move(prompt)));
    const auto sample = pull();
    ASSERT_TRUE(sample);
    EXPECT_EQ(opk::PromptInputsMeta::get(gst_sample_get_buffer(sample.get())), nullptr);
    EXPECT_EQ(calls[0].load(), 1U);
    EXPECT_EQ(calls[1].load(), 2U);
    EXPECT_EQ(promptedCalls[1].load(), 0U);
    const Message message(gst_bus_timed_pop_filtered(bus, Timeout, GST_MESSAGE_QOS),
                          gst_message_unref);
    ASSERT_TRUE(message);
    EXPECT_EQ(GST_MESSAGE_SRC(message.get()), GST_OBJECT(infer));
    finishStream();
    EXPECT_TRUE(lifetime.expired());
}

TEST_F(PromptInference, QueryIngressCopiesOwnedPixelsAndPreservesInvalidReplacement) {
    ASSERT_TRUE(start(true, true, false, true, "identity ! queue ! ", "TPRM,VPRM"));
    captureBeforeConsumption();
    gchar *capabilityText = nullptr;
    g_object_get(ingress, "capabilities", &capabilityText, nullptr);
    ASSERT_NE(capabilityText, nullptr);
    const auto capabilities = nlohmann::json::parse(capabilityText);
    g_free(capabilityText);
    ASSERT_EQ(capabilities.size(), 2);
    EXPECT_EQ(capabilities[0]["format"], "TPRM");
    EXPECT_EQ(capabilities[1]["maxReferenceBytes"], opk::MaxPromptReferenceBytes);
    ASSERT_TRUE(submitQuery(false, 1));
    EXPECT_FALSE(submitQuery(false, 2, false));
    EXPECT_FALSE(submit("first", 2)); // BPRM is not enabled on this ingress.
    ASSERT_TRUE(push());
    ASSERT_TRUE(pull());
    ASSERT_TRUE(bound(0));
    EXPECT_EQ(bound(0)->revision(), 1);
    EXPECT_EQ(bound(0)->payload->format(), opk::TextPromptFormat);
    ASSERT_TRUE(submitQuery(true, 2));
    EXPECT_FALSE(submitQuery(true, 3, false));
    ASSERT_TRUE(push(GST_SECOND));
    ASSERT_TRUE(pull());
    ASSERT_TRUE(bound(1));
    EXPECT_EQ(bound(1)->revision(), 2);
    const auto owner = std::dynamic_pointer_cast<const opk::VisualPromptPayload>(bound(1)->payload);
    ASSERT_TRUE(owner);
    EXPECT_EQ(owner->value().references()->Get(0)->pixels()->Get(2), 30);
}

TEST_F(PromptInference, BoxIngressRejectsQueryFormatWithoutReplacingActiveState) {
    ASSERT_TRUE(start(true, true));
    captureBeforeConsumption();
    ASSERT_TRUE(submit("first", 1));
    EXPECT_FALSE(submitQuery(false, 2));
    EXPECT_FALSE(submitQuery(true, 2));
    ASSERT_TRUE(push());
    ASSERT_TRUE(pull());
    ASSERT_TRUE(bound(0));
    EXPECT_EQ(bound(0)->revision(), 1);
    EXPECT_EQ(bound(0)->payload->format(), opk::BoxPromptFormat);
}

TEST_F(PromptInference, IdentityAndQueuePreservePromptToNamedInference) {
    ASSERT_TRUE(start(true, true, false, true, "identity ! queue ! "));
    ASSERT_TRUE(submit("first", 1));
    ASSERT_TRUE(push());
    const auto sample = pull();
    ASSERT_TRUE(sample);
    EXPECT_EQ(calls[0].load(), 1U);
    EXPECT_EQ(promptedCalls[0].load(), 1U);
    EXPECT_EQ(opk::PromptInputsMeta::get(gst_sample_get_buffer(sample.get())), nullptr);
}

TEST_F(PromptInference, UnsupportedRouteRejectsBeforeInferenceAndBusHandlerCanReenterSubmit) {
    ASSERT_TRUE(start(true, true, false, true, "videoscale ! "));
    ASSERT_TRUE(submit("first", 1));
    gst_bus_set_sync_handler(bus, reenterSubmit, this, nullptr);
    ASSERT_TRUE(push());
    const Message message(gst_bus_timed_pop_filtered(bus, Timeout, GST_MESSAGE_ERROR),
                          gst_message_unref);
    ASSERT_TRUE(message);
    EXPECT_EQ(GST_MESSAGE_SRC(message.get()), GST_OBJECT(ingress));
    EXPECT_EQ(calls[0].load(), 0U);
    EXPECT_TRUE(reentryAttempted.load());
    EXPECT_TRUE(reentryAccepted.load());
    g_autoptr(GError) error = nullptr;
    gst_message_parse_error(message.get(), &error, nullptr);
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error->message).find("identity/queue"), std::string::npos);
}

TEST_F(PromptInference, PairedSnapshotsSurviveReplacementCancelAndSessionHandover) {
    ASSERT_TRUE(start(true, true));
    captureBeforeConsumption();
    ASSERT_TRUE(submit("first", 1));
    ASSERT_TRUE(push());
    const auto first = pull();
    ASSERT_TRUE(first);
    const auto original = bound(0);
    ASSERT_TRUE(original);
    EXPECT_EQ(original->revision(), 1U);
    ASSERT_TRUE(submit("first", 2, 0.25));
    EXPECT_FALSE(command("cancel", "first", 1));
    ASSERT_TRUE(push(GST_SECOND));
    const auto second = pull();
    ASSERT_TRUE(second);
    ASSERT_TRUE(bound(1));
    EXPECT_EQ(bound(1)->revision(), 2U);
    ASSERT_TRUE(command("cancel", "first", 2));
    ASSERT_TRUE(push(2 * GST_SECOND));
    const auto absent = pull();
    ASSERT_TRUE(absent);
    EXPECT_FALSE(bound(2));
    ASSERT_TRUE(command("end-session", "first", 0));
    EXPECT_FALSE(submit("first", 3));
    ASSERT_TRUE(submit("second", 1));
    ASSERT_TRUE(push(3 * GST_SECOND));
    const auto handover = pull();
    ASSERT_TRUE(handover);
    ASSERT_TRUE(bound(3));
    EXPECT_EQ(bound(3)->sessionId, "second");
    EXPECT_EQ(original->sessionId, "first");
    EXPECT_EQ(original->revision(), 1U);
    EXPECT_EQ(opk::PromptInputsMeta::get(gst_sample_get_buffer(first.get())), nullptr);
    EXPECT_EQ(promptedCalls[0].load(), 3U);
}

TEST_F(PromptInference, AlreadyPairedBufferExecutesAfterSessionHandoverWithOriginalAssociation) {
    ASSERT_TRUE(start(true, true));
    blockPairedBuffer();
    ASSERT_TRUE(submit("first", 1));
    ASSERT_TRUE(push());
    ASSERT_TRUE(waitForPairedBuffer());
    ASSERT_EQ(calls[0].load(), 0U);
    ASSERT_TRUE(submit("first", 2, 0.25));
    ASSERT_TRUE(command("cancel", "first", 2));
    ASSERT_TRUE(command("end-session", "first", 0));
    ASSERT_TRUE(submit("second", 1));
    releasePairedBuffer();
    const auto oldSample = pull();
    ASSERT_TRUE(oldSample);
    const auto oldResults = opk::FrameResultsMeta::read(gst_sample_get_buffer(oldSample.get()));
    ASSERT_TRUE(oldResults);
    const auto oldAssociation = oldResults->get<PromptAssociationT>();
    ASSERT_TRUE(oldAssociation);
    EXPECT_EQ((*oldAssociation)->session_id, "first");
    ASSERT_TRUE((*oldAssociation)->request);
    EXPECT_EQ((*oldAssociation)->request->request_id, RequestId);
    EXPECT_EQ((*oldAssociation)->request->revision, 1U);
    const auto oldMedia = oldResults->get<FrameContextT>();
    ASSERT_TRUE(oldMedia);
    EXPECT_EQ((*oldAssociation)->parent_id, (*oldMedia)->video->object->id);
    EXPECT_NE((*oldAssociation)->parent_id, 0U);
    EXPECT_EQ(GST_BUFFER_PTS(gst_sample_get_buffer(oldSample.get())), 0U);
    ASSERT_TRUE(push(GST_SECOND));
    const auto newSample = pull();
    ASSERT_TRUE(newSample);
    const auto newResults = opk::FrameResultsMeta::read(gst_sample_get_buffer(newSample.get()));
    ASSERT_TRUE(newResults);
    const auto newAssociation = newResults->get<PromptAssociationT>();
    ASSERT_TRUE(newAssociation);
    EXPECT_EQ((*newAssociation)->session_id, "second");
    ASSERT_TRUE((*newAssociation)->request);
    EXPECT_EQ((*newAssociation)->request->request_id, RequestId);
    EXPECT_EQ((*newAssociation)->request->revision, 1U);
    EXPECT_NE((*newAssociation)->parent_id, (*oldAssociation)->parent_id);
    EXPECT_EQ(calls[0].load(), 2U);
    EXPECT_EQ(promptedCalls[0].load(), 2U);
}

TEST_F(PromptInference, SourceRestartRetainsContinuingPromptAndRebindsFreshMedia) {
    ASSERT_TRUE(start(true, true));
    ASSERT_TRUE(submit("first", 1));
    ASSERT_TRUE(push());
    const auto first = pull();
    ASSERT_TRUE(first);
    const auto firstResults = opk::FrameResultsMeta::read(gst_sample_get_buffer(first.get()));
    ASSERT_TRUE(firstResults);
    const auto firstAssociation = firstResults->get<PromptAssociationT>();
    ASSERT_TRUE(firstAssociation);
    ASSERT_EQ(calls[0].load(), 1U);
    ASSERT_NE(gst_element_set_state(pipeline, GST_STATE_READY), GST_STATE_CHANGE_FAILURE);
    ASSERT_EQ(gst_element_get_state(pipeline, nullptr, nullptr, Timeout), GST_STATE_CHANGE_SUCCESS);
    ASSERT_NE(gst_element_set_state(pipeline, GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    ASSERT_TRUE(push());
    const auto resumed = pull();
    ASSERT_TRUE(resumed);
    const auto resumedResults = opk::FrameResultsMeta::read(gst_sample_get_buffer(resumed.get()));
    ASSERT_TRUE(resumedResults);
    const auto association = resumedResults->get<PromptAssociationT>();
    ASSERT_TRUE(association);
    EXPECT_EQ((*association)->session_id, "first");
    ASSERT_TRUE((*association)->request);
    EXPECT_EQ((*association)->request->request_id, RequestId);
    EXPECT_EQ((*association)->request->revision, 1U);
    EXPECT_NE((*association)->parent_id, (*firstAssociation)->parent_id);
    const auto media = resumedResults->get<FrameContextT>();
    ASSERT_TRUE(media);
    EXPECT_EQ((*association)->parent_id, (*media)->video->object->id);
    EXPECT_EQ((*firstAssociation)->session_id, "first");
    EXPECT_EQ((*firstAssociation)->request->revision, 1U);
    EXPECT_EQ(calls[0].load(), 2U);
    EXPECT_EQ(promptedCalls[0].load(), 2U);
    EXPECT_EQ(opk::PromptInputsMeta::get(gst_sample_get_buffer(resumed.get())), nullptr);
}

TEST_F(PromptInference, MalformedReplacementRetainsPromptAndBusReportsBoundaryRejection) {
    ASSERT_TRUE(start(true, true));
    captureBeforeConsumption();
    ASSERT_TRUE(submit("first", 1));
    EXPECT_FALSE(submit("first", 2, 1.5));
    bool acceptedSeen = false;
    bool rejectedSeen = false;
    while (GstMessage *const message = gst_bus_pop_filtered(bus, GST_MESSAGE_ELEMENT)) {
        const GstStructure *const structure = gst_message_get_structure(message);
        if (gst_structure_has_name(structure, "opk-prompt-ack-v1")) {
            gboolean accepted = FALSE;
            guint64 requestId = 0;
            EXPECT_TRUE(gst_structure_get_boolean(structure, "accepted", &accepted));
            EXPECT_TRUE(gst_structure_get_uint64(structure, "request-id", &requestId));
            EXPECT_EQ(requestId, RequestId);
            EXPECT_STREQ(gst_structure_get_string(structure, "boundary"), "opkprompt");
            acceptedSeen = acceptedSeen || accepted;
            rejectedSeen = rejectedSeen || !accepted;
        }
        gst_message_unref(message);
    }
    EXPECT_TRUE(acceptedSeen);
    EXPECT_TRUE(rejectedSeen);
    ASSERT_TRUE(push());
    const auto sample = pull();
    ASSERT_TRUE(sample);
    ASSERT_TRUE(bound(0));
    EXPECT_EQ(bound(0)->revision(), 1U);
    EXPECT_EQ(promptedCalls[0].load(), 1U);
}

TEST_F(PromptInference, ActualQueryPreparationSurvivesCancelAndSessionHandoverAndRecreates) {
    const char *const assetPath = std::getenv("OPK_QUERY_TEST_ASSETS");
    if (!assetPath) {
        GTEST_SKIP() << "Requires qualified actual query models and fixtures";
    }
    const std::filesystem::path assets(assetPath);
    nlohmann::json descriptor;
    std::ifstream(assets / "opchain.json") >> descriptor;
    const std::filesystem::path model =
        descriptor["ops"][0]["attributes"]["modelDescriptor"].get<std::string>();
    const auto ready = directory / "ready";
    const auto proceed = directory / "proceed";
    const auto counts = directory / "calls";
    const auto tokenizer = directory / "tokenizer.py";
    {
        std::ofstream script(tokenizer);
        script << "import importlib.util, time\nfrom pathlib import Path\n"
               << "def create_tokenizer(vocabulary):\n"
               << "    spec=importlib.util.spec_from_file_location('qualified_tokenizer', "
               << nlohmann::json((model.parent_path() / "scripts/tokenizer.py").string()).dump()
               << ")\n"
               << "    module=importlib.util.module_from_spec(spec)\n    "
                  "spec.loader.exec_module(module)\n"
               << "    original=module.create_tokenizer(vocabulary)\n"
               << "    def encode(texts):\n"
               << "        with open(" << nlohmann::json(counts.string()).dump()
               << ", 'a') as output: output.write('call\\n')\n"
               << "        Path(" << nlohmann::json(ready.string()).dump() << ").touch()\n"
               << "        deadline=time.monotonic()+20\n"
               << "        while not Path(" << nlohmann::json(proceed.string()).dump()
               << ").exists():\n"
               << "            if time.monotonic()>deadline: raise RuntimeError('test preparation "
                  "barrier timed out')\n"
               << "            time.sleep(0.001)\n"
               << "        return original(texts)\n    return encode\n";
    }
    for (auto &operation : descriptor["ops"]) {
        if (operation.value("instanceId", "") == "yoloe") {
            operation["instanceId"] = "sam";
        }
        if (operation["attributes"].contains("inferenceInstance")) {
            operation["attributes"]["inferenceInstance"] = "sam";
        }
    }
    descriptor["ops"][0]["attributes"]["tokenizerScript"] = tokenizer.string();
    const auto actual = directory / "opchain-actual.json";
    std::ofstream(actual) << descriptor.dump();
    ASSERT_TRUE(start(true, true, true, true, "", "TPRM"));
    ASSERT_NE(gst_element_set_state(pipeline, GST_STATE_NULL), GST_STATE_CHANGE_FAILURE);
    g_object_set(infer, "opchain-path", actual.c_str(), nullptr);
    nlohmann::json oracle;
    std::ifstream(assets / "expected.json") >> oracle;
    GstCaps *const caps = gst_caps_new_simple("video/x-raw",
                                              "format",
                                              G_TYPE_STRING,
                                              "BGRA",
                                              "width",
                                              G_TYPE_INT,
                                              oracle["width"].get<int>(),
                                              "height",
                                              G_TYPE_INT,
                                              oracle["height"].get<int>(),
                                              "framerate",
                                              GST_TYPE_FRACTION,
                                              1,
                                              1,
                                              nullptr);
    gst_app_src_set_caps(GST_APP_SRC(source), caps);
    gst_caps_unref(caps);
    std::ifstream input(assets / "target.bgra", std::ios::binary);
    const std::vector<uint8_t> pixels{std::istreambuf_iterator<char>(input),
                                      std::istreambuf_iterator<char>()};
    const auto pushTarget = [&]() {
        GstBuffer *const buffer = gst_buffer_new_allocate(nullptr, pixels.size(), nullptr);
        gst_buffer_fill(buffer, 0, pixels.data(), pixels.size());
        GST_BUFFER_PTS(buffer) = 0;
        GST_BUFFER_DURATION(buffer) = GST_SECOND;
        return gst_app_src_push_buffer(GST_APP_SRC(source), buffer) == GST_FLOW_OK;
    };
    const auto countPreparations = [&]() {
        std::ifstream callsFile(counts);
        return std::count(
            std::istreambuf_iterator<char>(callsFile), std::istreambuf_iterator<char>(), '\n');
    };
    ASSERT_NE(gst_element_set_state(pipeline, GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    ASSERT_TRUE(submitQuery(false, 1));
    ASSERT_TRUE(pushTarget());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!std::filesystem::exists(ready) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool preparing = std::filesystem::exists(ready);
    if (!preparing) {
        std::ofstream(proceed).put('1');
    }
    ASSERT_TRUE(preparing);
    EXPECT_TRUE(command("cancel", "first", 1));
    EXPECT_TRUE(command("end-session", "first", 0));
    EXPECT_TRUE(submitQuery(false, 1, true, "second"));
    std::ofstream(proceed).put('1');
    const auto old = pull();
    ASSERT_TRUE(old);
    const auto oldResults = opk::FrameResultsMeta::read(gst_sample_get_buffer(old.get()));
    ASSERT_TRUE(oldResults);
    const auto oldAssociation = oldResults->get<PromptAssociationT>();
    ASSERT_TRUE(oldAssociation);
    EXPECT_EQ((*oldAssociation)->session_id, "first");
    EXPECT_EQ((*oldAssociation)->request->revision, 1);
    EXPECT_EQ(oldResults->count<PromptAssociationT>(), 1);
    EXPECT_FALSE((*oldAssociation)->matches.empty());
    EXPECT_EQ(countPreparations(), 1);
    for (int repeat = 0; repeat < 2; ++repeat) {
        ASSERT_TRUE(pushTarget());
        const auto sample = pull();
        ASSERT_TRUE(sample);
        const auto results = opk::FrameResultsMeta::read(gst_sample_get_buffer(sample.get()));
        ASSERT_TRUE(results);
        const auto association = results->get<PromptAssociationT>();
        ASSERT_TRUE(association);
        EXPECT_EQ((*association)->session_id, "second");
        EXPECT_EQ(results->count<PromptAssociationT>(), 1);
        EXPECT_EQ(countPreparations(), 2);
    }
    ASSERT_NE(gst_element_set_state(pipeline, GST_STATE_NULL), GST_STATE_CHANGE_FAILURE);
    ASSERT_NE(gst_element_set_state(pipeline, GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    ASSERT_TRUE(pushTarget());
    const auto recreated = pull();
    ASSERT_TRUE(recreated);
    EXPECT_EQ(countPreparations(), 3); // No prepared features survive runtime recreation.
    const auto results = opk::FrameResultsMeta::read(gst_sample_get_buffer(recreated.get()));
    ASSERT_TRUE(results);
    const auto association = results->get<PromptAssociationT>();
    ASSERT_TRUE(association);
    EXPECT_EQ((*association)->session_id, "second");
}

} // namespace

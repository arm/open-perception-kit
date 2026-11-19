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

#include "../elements/opksink/prompt_control.h"

#include <fb/box_prompt_inputs_generated.h>
#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using json = nlohmann::json;
using namespace open_perception_kit::metadata;
constexpr uint64_t RequestId = (uint64_t{1} << 63) + 7;

std::vector<uint8_t> packet(const uint64_t requestId = RequestId, const uint64_t revision = 1) {
    BoxPromptInputsT root;
    root.request = std::make_unique<RequestEnvelopeT>();
    root.request->request_id = requestId;
    root.request->revision = revision;
    root.request->destination = std::make_unique<RequestDestinationT>();
    root.request->destination->inference_instance = "sam";
    root.request->destination->source_name = "pipelineVideoFrame";
    root.payload = std::make_unique<BoxPromptPayloadT>();
    root.payload->box = std::make_unique<PromptBoxT>();
    root.payload->box->right = 1;
    root.payload->box->bottom = 1;
    flatbuffers::FlatBufferBuilder builder;
    const auto offset = BoxPromptInputs::Pack(builder, &root);
    FinishBoxPromptInputsBuffer(builder, offset);
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

json submit(const uint64_t requestId = RequestId, const uint64_t revision = 1) {
    const auto bytes = packet(requestId, revision);
    g_autofree gchar *encoded = g_base64_encode(bytes.data(), bytes.size());
    return {{"type", "prompt_command"},
            {"version", 1},
            {"command_id", "command"},
            {"target", "prompts"},
            {"command", "submit"},
            {"payload_b64", encoded}};
}

bool nativeSubmit(GstElement *const ingress,
                  const std::string &session,
                  const uint64_t requestId,
                  const uint64_t revision = 1) {
    const auto bytes = packet(requestId, revision);
    g_autoptr(GBytes) payload = g_bytes_new(bytes.data(), bytes.size());
    GstStructure *const structure = gst_structure_new("opk-prompt-command-v1",
                                                      "command",
                                                      G_TYPE_STRING,
                                                      "submit",
                                                      "session-id",
                                                      G_TYPE_STRING,
                                                      session.c_str(),
                                                      "payload",
                                                      G_TYPE_BYTES,
                                                      payload,
                                                      nullptr);
    return gst_element_send_event(ingress,
                                  gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, structure));
}

class PromptControlTest : public ::testing::Test {
  protected:
    GstElement *pipeline = nullptr;
    GstElement *sink = nullptr;
    GstElement *ingress = nullptr;
    GstElement *infer = nullptr;
    GstBus *bus = nullptr;
    std::shared_ptr<PromptControl> control;
    std::vector<std::function<void()>> posted;
    std::vector<json> notifications;
    std::vector<bool> overflows;
    std::string session;
    bool nested = false;
    json nestedReply;

    void SetUp() override {
        gst_init(nullptr, nullptr);
        g_autoptr(GError) error = nullptr;
        pipeline = gst_parse_launch(
            "videotestsrc ! opkprompt name=prompts "
            "inference-instance=sam destination-element=infer ! opkinfer name=infer ! "
            "opksink name=sink",
            &error);
        ASSERT_EQ(error, nullptr) << (error ? error->message : "");
        ASSERT_NE(pipeline, nullptr);
        sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
        ingress = gst_bin_get_by_name(GST_BIN(pipeline), "prompts");
        infer = gst_bin_get_by_name(GST_BIN(pipeline), "infer");
        bus = gst_element_get_bus(pipeline);
        control = makeControl();
        const auto capabilities = control->open();
        ASSERT_EQ(capabilities["targets"].size(), 1);
        ASSERT_TRUE(capabilities["targets"][0]["drawing_supported"].get<bool>())
            << capabilities.dump();
        session = capabilities["session_id"];
    }

    std::shared_ptr<PromptControl> makeControl() {
        auto result = std::make_shared<PromptControl>(
            sink,
            [this](std::function<void()> callback) { posted.push_back(std::move(callback)); },
            [this](const std::string &, const json &message, const bool overflow) {
                notifications.push_back(message);
                overflows.push_back(overflow);
            });
        result->start();
        return result;
    }

    void TearDown() override {
        gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        control->stop();
        control.reset();
        posted.clear();
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(sink);
        gst_object_unref(ingress);
        gst_object_unref(infer);
        gst_object_unref(pipeline);
    }

    json stateCommand(const std::string &operation) const {
        return {{"type", "prompt_command"},
                {"version", 1},
                {"command_id", "state"},
                {"target", "prompts"},
                {"command", operation},
                {"request_id", std::to_string(RequestId)},
                {"revision", "2"}};
    }

    void failure(GstElement *const source, const std::string &owner) const {
        GstStructure *const structure = gst_structure_new("opk-prompt-failure-v1",
                                                          "boundary",
                                                          G_TYPE_STRING,
                                                          "opkinfer",
                                                          "session-id",
                                                          G_TYPE_STRING,
                                                          owner.c_str(),
                                                          "request-id",
                                                          G_TYPE_UINT64,
                                                          static_cast<guint64>(RequestId),
                                                          "revision",
                                                          G_TYPE_UINT64,
                                                          static_cast<guint64>(1),
                                                          "reason",
                                                          G_TYPE_STRING,
                                                          "synthetic failure",
                                                          nullptr);
        gst_element_post_message(source, gst_message_new_element(GST_OBJECT(source), structure));
    }

    static GstBusSyncReply nestedNative(GstBus *, GstMessage *const message, gpointer data) {
        auto &self = *static_cast<PromptControlTest *>(data);
        const GstStructure *const structure = gst_message_get_structure(message);
        if (!self.nested && structure && gst_structure_has_name(structure, "opk-prompt-ack-v1")) {
            self.nested = true;
            EXPECT_TRUE(nativeSubmit(self.ingress, self.session, RequestId + 1));
        }
        return GST_BUS_PASS;
    }

    static GstBusSyncReply nestedAdapter(GstBus *, GstMessage *const message, gpointer data) {
        auto &self = *static_cast<PromptControlTest *>(data);
        const GstStructure *const structure = gst_message_get_structure(message);
        if (!self.nested && structure && gst_structure_has_name(structure, "opk-prompt-ack-v1")) {
            self.nested = true;
            self.nestedReply = self.control->command(self.session, submit(RequestId + 1));
        }
        return GST_BUS_PASS;
    }

    static GstBusSyncReply closeOnSubmit(GstBus *, GstMessage *const message, gpointer data) {
        auto &self = *static_cast<PromptControlTest *>(data);
        const GstStructure *const structure = gst_message_get_structure(message);
        const char *const operation =
            structure ? gst_structure_get_string(structure, "command") : nullptr;
        if (operation && std::string_view(operation) == "submit") {
            self.control->close(self.session);
        }
        return GST_BUS_PASS;
    }

    static GstBusSyncReply stopOnAck(GstBus *, GstMessage *const message, gpointer data) {
        auto &self = *static_cast<PromptControlTest *>(data);
        const GstStructure *const structure = gst_message_get_structure(message);
        if (!self.nested && structure && gst_structure_has_name(structure, "opk-prompt-ack-v1")) {
            self.nested = true;
            self.control->stop();
        }
        return GST_BUS_PASS;
    }
};

TEST_F(PromptControlTest, HighIdentityReplacementCancellationAndExclusiveOwnership) {
    const json first = control->command(session, submit());
    ASSERT_TRUE(first["accepted"].get<bool>()) << first.dump();
    EXPECT_EQ(first["request_id"], std::to_string(RequestId));
    EXPECT_EQ(first["basis"], "bus");
    EXPECT_TRUE(control->command(session, submit(RequestId, 2))["accepted"].get<bool>());
    EXPECT_FALSE(control->command(session, submit())["accepted"].get<bool>());
    const std::string other = control->open()["session_id"];
    EXPECT_FALSE(control->command(other, submit())["accepted"].get<bool>());
    EXPECT_TRUE(control->command(session, stateCommand("cancel"))["accepted"].get<bool>());
    EXPECT_FALSE(control->command(other, submit())["accepted"].get<bool>());
    control->close(session);
    EXPECT_TRUE(control->command(other, submit())["accepted"].get<bool>());
}

TEST_F(PromptControlTest, TransportValidationDoesNotDispatchOrClaimOwnership) {
    for (const auto &change : {json{{"version", "1"}},
                               json{{"version", 1.0}},
                               json{{"payload_b64", "@@=="}},
                               json{{"payload_b64", ""}},
                               json{{"command_id", std::string(129, 'x')}},
                               json{{"target", "missing"}}}) {
        auto command = submit();
        command.update(change);
        EXPECT_EQ(control->command(session, command)["type"], "prompt_error");
    }
    auto cancel = stateCommand("cancel");
    for (const char *const value : {"0", "-1", "18446744073709551616", "1x"}) {
        cancel["request_id"] = value;
        EXPECT_EQ(control->command(session, cancel)["type"], "prompt_error");
    }
    auto malformed = submit();
    malformed["payload_b64"] = "AAAA";
    const json rejected = control->command(session, malformed);
    EXPECT_EQ(rejected["type"], "prompt_ack");
    EXPECT_FALSE(rejected["accepted"].get<bool>());
    EXPECT_EQ(rejected["request_id"], "0");
    control->close(session);
    bool ended = false;
    while (GstMessage *const message = gst_bus_pop_filtered(bus, GST_MESSAGE_ELEMENT)) {
        const GstStructure *const structure = gst_message_get_structure(message);
        const char *const command = gst_structure_get_string(structure, "command");
        ended = ended || (command && std::string_view(command) == "end-session");
        gst_message_unref(message);
    }
    EXPECT_FALSE(ended);
}

TEST_F(PromptControlTest, StateCommandsRemainAvailableWithoutInferenceDestination) {
    ASSERT_TRUE(control->command(session, submit())["accepted"].get<bool>());
    ASSERT_TRUE(control->command(session, submit(RequestId, 2))["accepted"].get<bool>());
    ASSERT_TRUE(gst_bin_remove(GST_BIN(pipeline), infer));
    const json cancelled = control->command(session, stateCommand("cancel"));
    EXPECT_TRUE(cancelled["accepted"].get<bool>()) << cancelled.dump();
    EXPECT_TRUE(control->command(session, stateCommand("end-session"))["accepted"].get<bool>());
}

TEST_F(PromptControlTest, NestedNativeReplyCannotSatisfyOuterCommand) {
    gst_bus_set_sync_handler(bus, nestedNative, this, nullptr);
    const json reply = control->command(session, submit());
    EXPECT_TRUE(nested);
    EXPECT_EQ(reply["basis"], "bus");
    EXPECT_EQ(reply["request_id"], std::to_string(RequestId));
}

TEST_F(PromptControlTest, NestedAdapterCaptureRestoresOuterIdentity) {
    gst_bus_set_sync_handler(bus, nestedAdapter, this, nullptr);
    const json reply = control->command(session, submit());
    EXPECT_EQ(nestedReply["request_id"], std::to_string(RequestId + 1));
    EXPECT_EQ(nestedReply["basis"], "bus");
    EXPECT_EQ(reply["request_id"], std::to_string(RequestId));
    EXPECT_EQ(reply["basis"], "bus");
}

TEST_F(PromptControlTest, DroppedBusAcknowledgementUsesExplicitEventReturn) {
    gst_bus_set_sync_handler(
        bus,
        [](GstBus *, GstMessage *const message, gpointer) {
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ELEMENT) {
                gst_message_unref(message);
                return GST_BUS_DROP;
            }
            return GST_BUS_PASS;
        },
        nullptr,
        nullptr);
    const json reply = control->command(session, submit());
    EXPECT_TRUE(reply["accepted"].get<bool>());
    EXPECT_EQ(reply["basis"], "event-return");
    EXPECT_EQ(reply["request_id"], std::to_string(RequestId));
}

TEST_F(PromptControlTest, AcceptedSubmissionClosedFromHostCallbackReleasesIngress) {
    gst_bus_set_sync_handler(bus, stopOnAck, this, nullptr);
    const json reply = control->command(session, submit());
    EXPECT_TRUE(reply["accepted"].get<bool>());
    EXPECT_TRUE(nested);
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    EXPECT_TRUE(nativeSubmit(ingress, "next-native-session", RequestId));
}

TEST_F(PromptControlTest, FailureNotificationsRequireOwnedSourceSessionAndBoundQueuedWork) {
    ASSERT_TRUE(control->command(session, submit())["accepted"].get<bool>());
    failure(ingress, session);
    failure(infer, "foreign-session");
    EXPECT_TRUE(posted.empty());
    for (size_t index = 0; index < PromptControl::MaxPendingNotifications + 10; ++index) {
        failure(infer, session);
    }
    ASSERT_EQ(posted.size(), PromptControl::MaxPendingNotifications + 1);
    for (const auto &callback : posted) {
        callback();
    }
    EXPECT_EQ(notifications.size(), posted.size());
    EXPECT_TRUE(overflows.back());
    EXPECT_EQ(notifications.front()["request_id"], std::to_string(RequestId));
    posted.clear();
    notifications.clear();
    control->close(session);
    failure(infer, session);
    EXPECT_TRUE(posted.empty());
}

TEST_F(PromptControlTest, NotificationCallbackCanCloseSessionWithoutDeadlock) {
    control->stop();
    control = std::make_shared<PromptControl>(
        sink,
        [this](std::function<void()> callback) { posted.push_back(std::move(callback)); },
        [this](const std::string &id, const json &, bool) { control->close(id); });
    control->start();
    session = control->open()["session_id"];
    ASSERT_TRUE(control->command(session, submit())["accepted"].get<bool>());
    failure(infer, session);
    ASSERT_EQ(posted.size(), 1);
    posted.front()();
    EXPECT_TRUE(nativeSubmit(ingress, "after-notification", RequestId));
}

TEST_F(PromptControlTest, StopDiscardsOldNotificationsAndDoesNotConsumeHostMessages) {
    ASSERT_TRUE(control->command(session, submit())["accepted"].get<bool>());
    failure(infer, session);
    control->stop();
    for (const auto &callback : posted) {
        callback();
    }
    EXPECT_TRUE(notifications.empty());
    GstMessage *const message = gst_bus_pop_filtered(bus, GST_MESSAGE_ELEMENT);
    ASSERT_NE(message, nullptr);
    gst_message_unref(message);
    control->start();
    EXPECT_NE(control->open()["session_id"], session);
}

TEST_F(PromptControlTest, UnknownDownstreamGeometryDisablesDrawing) {
    gst_element_unlink(infer, sink);
    GstElement *const scale = gst_element_factory_make("videoscale", nullptr);
    ASSERT_NE(scale, nullptr);
    ASSERT_TRUE(gst_bin_add(GST_BIN(pipeline), scale));
    ASSERT_TRUE(gst_element_link_many(infer, scale, sink, nullptr));
    const json capabilities = control->open();
    EXPECT_FALSE(capabilities["targets"][0]["drawing_supported"].get<bool>());
    EXPECT_EQ(control->command(session, submit())["type"], "prompt_error");
}

TEST_F(PromptControlTest, InvalidBindingNamesAreNotAdvertised) {
    const std::string invalid(129, 'x');
    g_object_set(ingress, "inference-instance", invalid.c_str(), nullptr);
    EXPECT_TRUE(control->open()["targets"].empty());
}

TEST_F(PromptControlTest, MaximumUint64AndExactDecodedPacketLimitArePreserved) {
    const json reply = control->command(session, submit(std::numeric_limits<uint64_t>::max()));
    EXPECT_TRUE(reply["accepted"].get<bool>());
    EXPECT_EQ(reply["request_id"], "18446744073709551615");
    for (const size_t size : {size_t{4096}, size_t{4097}}) {
        std::vector<guchar> bytes(size);
        g_autofree gchar *encoded = g_base64_encode(bytes.data(), bytes.size());
        auto command = submit();
        command["payload_b64"] = encoded;
        EXPECT_EQ(control->command(session, command)["type"],
                  size == 4096 ? "prompt_ack" : "prompt_error");
    }
}

TEST_F(PromptControlTest, DuplicateNamesAcrossNestedBinsAreNotAdvertised) {
    GstElement *const nested = gst_bin_new("nested");
    ASSERT_TRUE(gst_bin_add(GST_BIN(pipeline), nested));
    GstElement *const duplicate = gst_element_factory_make("opkprompt", "prompts");
    ASSERT_TRUE(gst_bin_add(GST_BIN(nested), duplicate));
    EXPECT_TRUE(control->open()["targets"].empty());
}

TEST_F(PromptControlTest, NestedTransparentBinRouteReachesActualVideoInput) {
    g_autoptr(GstPad) ingressSink = gst_element_get_static_pad(ingress, "sink");
    g_autoptr(GstPad) upstreamPad = gst_pad_get_peer(ingressSink);
    g_autoptr(GstElement) upstream = gst_pad_get_parent_element(upstreamPad);
    gst_element_unlink(upstream, ingress);
    gst_element_unlink(infer, sink);
    gst_element_unlink(ingress, infer);
    ASSERT_TRUE(gst_bin_remove(GST_BIN(pipeline), ingress));
    ASSERT_TRUE(gst_bin_remove(GST_BIN(pipeline), infer));
    GstElement *const nested = gst_bin_new("nested");
    ASSERT_TRUE(gst_bin_add(GST_BIN(pipeline), nested));
    ASSERT_TRUE(gst_bin_add(GST_BIN(nested), ingress));
    ASSERT_TRUE(gst_bin_add(GST_BIN(nested), infer));
    ASSERT_TRUE(gst_element_link(ingress, infer));
    g_autoptr(GstPad) output = gst_element_get_static_pad(infer, "src");
    ASSERT_TRUE(gst_element_add_pad(nested, gst_ghost_pad_new("sink", ingressSink)));
    ASSERT_TRUE(gst_element_add_pad(nested, gst_ghost_pad_new("src", output)));
    ASSERT_TRUE(gst_element_link_many(upstream, nested, sink, nullptr));
    const json capabilities = control->open();
    ASSERT_EQ(capabilities["targets"].size(), 1);
    EXPECT_TRUE(capabilities["targets"][0]["drawing_supported"].get<bool>()) << capabilities.dump();
}

TEST_F(PromptControlTest, RecreatedIngressCannotReceiveAnOldSessionsCommands) {
    ASSERT_TRUE(control->command(session, submit())["accepted"].get<bool>());
    ASSERT_TRUE(gst_bin_remove(GST_BIN(pipeline), ingress));
    GstElement *const replacement = gst_element_factory_make("opkprompt", "prompts");
    g_object_set(replacement, "inference-instance", "sam", "destination-element", "infer", nullptr);
    ASSERT_TRUE(gst_bin_add(GST_BIN(pipeline), replacement));
    const json reply = control->command(session, stateCommand("cancel"));
    EXPECT_EQ(reply["type"], "prompt_error");
    control->close(session);
    EXPECT_TRUE(nativeSubmit(ingress, "retained-old-element", RequestId));
}

TEST_F(PromptControlTest, MultipleObserversBalanceHostSignalEmissionAndReleaseOwnership) {
    unsigned observed = 0;
    gst_bus_enable_sync_message_emission(bus);
    const gulong handler = g_signal_connect(bus,
                                            "sync-message::element",
                                            G_CALLBACK(+[](GstBus *, GstMessage *, gpointer data) {
                                                ++*static_cast<unsigned *>(data);
                                            }),
                                            &observed);
    auto other = makeControl();
    const std::string otherSession = other->open()["session_id"];
    control->stop();
    EXPECT_TRUE(other->command(otherSession, submit())["accepted"].get<bool>());
    other->stop();
    const unsigned before = observed;
    failure(infer, "host-owned");
    EXPECT_EQ(observed, before + 1);
    g_signal_handler_disconnect(bus, handler);
    gst_bus_disable_sync_message_emission(bus);
    EXPECT_TRUE(nativeSubmit(ingress, "after-observers", RequestId));
}

TEST_F(PromptControlTest, StandaloneSinkReportsNoTargetsAndCannotDispatch) {
    GstElement *const standalone = gst_element_factory_make("opksink", "standalone");
    ASSERT_NE(standalone, nullptr);
    auto adapter = std::make_shared<PromptControl>(
        standalone,
        [](std::function<void()> callback) { callback(); },
        [](const std::string &, const json &, bool) {});
    adapter->start();
    const json capabilities = adapter->open();
    EXPECT_TRUE(capabilities["targets"].empty());
    EXPECT_EQ(adapter->command(capabilities["session_id"], submit())["type"], "prompt_error");
    adapter->stop();
    gst_object_unref(standalone);
}

TEST_F(PromptControlTest, CloseRacingAcceptedSubmissionCannotRetainController) {
    struct Gate {
        std::mutex mutex;
        std::condition_variable changed;
        bool posted = false;
        bool release = false;
    } gate;
    gst_bus_set_sync_handler(
        bus,
        [](GstBus *, GstMessage *const message, gpointer data) {
            auto &state = *static_cast<Gate *>(data);
            const GstStructure *const structure = gst_message_get_structure(message);
            const char *const operation =
                structure ? gst_structure_get_string(structure, "command") : nullptr;
            if (operation && std::string_view(operation) == "submit") {
                std::unique_lock lock(state.mutex);
                state.posted = true;
                state.changed.notify_one();
                state.changed.wait(lock, [&state] { return state.release; });
            }
            return GST_BUS_PASS;
        },
        &gate,
        nullptr);
    json reply;
    std::thread worker([&] { reply = control->command(session, submit()); });
    bool reached = false;
    {
        std::unique_lock lock(gate.mutex);
        reached = gate.changed.wait_for(lock, std::chrono::seconds(5), [&] { return gate.posted; });
    }
    control->close(session);
    {
        const std::lock_guard lock(gate.mutex);
        gate.release = true;
    }
    gate.changed.notify_one();
    worker.join();
    ASSERT_TRUE(reached);
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    EXPECT_TRUE(reply["accepted"].get<bool>());
    EXPECT_TRUE(nativeSubmit(ingress, "after-racing-close", RequestId));
}

TEST_F(PromptControlTest, OrderedCleanupSurvivesAdapterReleaseAndRunsOnceAfterUpdateRace) {
    control->stop();
    std::vector<std::function<void()>> cleanup;
    control = std::make_shared<PromptControl>(
        sink,
        [this](std::function<void()> callback) { posted.push_back(std::move(callback)); },
        [](const std::string &, const json &, bool) {},
        [&cleanup](std::function<void()> callback) { cleanup.push_back(std::move(callback)); });
    control->start();
    session = control->open()["session_id"];
    ASSERT_TRUE(control->command(session, submit())["accepted"].get<bool>());
    gst_bus_set_sync_handler(bus, closeOnSubmit, this, nullptr);
    EXPECT_TRUE(control->command(session, submit(RequestId, 2))["accepted"].get<bool>());
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    control->stop();
    ASSERT_EQ(cleanup.size(), 1);
    EXPECT_FALSE(nativeSubmit(ingress, "before-ordered-cleanup", RequestId));
    control.reset();
    cleanup.front()();
    EXPECT_TRUE(nativeSubmit(ingress, "after-ordered-cleanup", RequestId));
    control = makeControl();
}

} // namespace

/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gst/gst.h>

#include "webrtc_session.h"

namespace {

static_assert(noexcept(std::declval<PekSinkWebRtcSession &>().cleanup()));

void ensure_gstreamer() {
    static bool initialized = false;
    if (!initialized) {
        gst_init(nullptr, nullptr);
        initialized = true;
    }
}

GstElement *make_element(const char *factory, const std::string &name) {
    return gst_element_factory_make(factory, name.c_str());
}

int count_src_request_pads(GstElement *element) {
    int count = 0;
    GstIterator *it = gst_element_iterate_pads(element);
    GValue item = G_VALUE_INIT;

    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        auto *pad = GST_PAD(g_value_get_object(&item));
        if (pad && std::strncmp(GST_PAD_NAME(pad), "src_", 4) == 0) {
            ++count;
        }
        g_value_reset(&item);
    }

    g_value_unset(&item);
    gst_iterator_free(it);
    return count;
}

bool has_child(GstElement *bin, const std::string &name) {
    GstElement *child = gst_bin_get_by_name(GST_BIN(bin), name.c_str());
    if (child) {
        gst_object_unref(child);
        return true;
    }
    return false;
}

struct SessionFixture {
    GstElement *bin = nullptr;
    GstElement *video_tee = nullptr;
    GstElement *audio_tee = nullptr;

    SessionFixture() {
        ensure_gstreamer();
        bin = gst_bin_new("owner_bin");
        video_tee = make_element("tee", "video_tee");
        audio_tee = make_element("tee", "audio_tee");
        if (bin && video_tee && audio_tee) {
            gst_bin_add_many(GST_BIN(bin), video_tee, audio_tee, nullptr);
        }
    }

    ~SessionFixture() {
        if (bin) {
            gst_object_unref(bin);
        }
    }

    bool valid() const {
        return bin && video_tee && audio_tee;
    }
};

std::unique_ptr<PekSinkWebRtcSession> make_linked_session(SessionFixture &fixture, int suffix) {
    auto session =
        std::make_unique<PekSinkWebRtcSession>(fixture.bin, fixture.video_tee, fixture.audio_tee);

    session->queue = make_element("queue", "client_v_queue_" + std::to_string(suffix));
    session->audio_queue = make_element("queue", "client_a_queue_" + std::to_string(suffix));
    session->v_pay = make_element("identity", "client_v_pay_" + std::to_string(suffix));
    session->a_pay = make_element("identity", "client_a_pay_" + std::to_string(suffix));
    session->v_capsfilter = make_element("capsfilter", "client_v_caps_" + std::to_string(suffix));
    session->a_capsfilter = make_element("capsfilter", "client_a_caps_" + std::to_string(suffix));
    session->webrtcbin = make_element("webrtcbin", "client_webrtc_" + std::to_string(suffix));

    auto fail = [&session]() -> std::unique_ptr<PekSinkWebRtcSession> {
        session->cleanup();
        return nullptr;
    };

    if (!session->queue || !session->audio_queue || !session->v_pay || !session->a_pay ||
        !session->v_capsfilter || !session->a_capsfilter || !session->webrtcbin) {
        return fail();
    }

    gst_bin_add_many(GST_BIN(fixture.bin),
                     session->queue,
                     session->audio_queue,
                     session->v_pay,
                     session->a_pay,
                     session->v_capsfilter,
                     session->a_capsfilter,
                     session->webrtcbin,
                     nullptr);

    session->tee_src_pad = gst_element_request_pad_simple(fixture.video_tee, "src_%u");
    session->audio_tee_src_pad = gst_element_request_pad_simple(fixture.audio_tee, "src_%u");
    session->webrtc_sink_pad = gst_element_request_pad_simple(session->webrtcbin, "sink_%u");
    session->audio_webrtc_sink_pad = gst_element_request_pad_simple(session->webrtcbin, "sink_%u");

    if (!session->tee_src_pad || !session->audio_tee_src_pad || !session->webrtc_sink_pad ||
        !session->audio_webrtc_sink_pad) {
        return fail();
    }

    GstPad *video_queue_sink = gst_element_get_static_pad(session->queue, "sink");
    GstPad *audio_queue_sink = gst_element_get_static_pad(session->audio_queue, "sink");
    if (!video_queue_sink || !audio_queue_sink) {
        if (video_queue_sink) {
            gst_object_unref(video_queue_sink);
        }
        if (audio_queue_sink) {
            gst_object_unref(audio_queue_sink);
        }
        return fail();
    }

    const bool linked =
        gst_pad_link(session->tee_src_pad, video_queue_sink) == GST_PAD_LINK_OK &&
        gst_pad_link(session->audio_tee_src_pad, audio_queue_sink) == GST_PAD_LINK_OK;
    gst_object_unref(video_queue_sink);
    gst_object_unref(audio_queue_sink);

    if (!linked) {
        return fail();
    }

    return session;
}

} // namespace

TEST(WebRtcSessionCleanupTest, ReleasesVideoAndAudioTeeRequestPads) {
    SessionFixture fixture;
    ASSERT_TRUE(fixture.valid());

    auto session = make_linked_session(fixture, 1);
    if (!session) {
        GTEST_SKIP() << "Required GStreamer elements for WebRTC session cleanup are unavailable";
    }

    EXPECT_EQ(count_src_request_pads(fixture.video_tee), 1);
    EXPECT_EQ(count_src_request_pads(fixture.audio_tee), 1);

    session->cleanup();

    EXPECT_EQ(count_src_request_pads(fixture.video_tee), 0);
    EXPECT_EQ(count_src_request_pads(fixture.audio_tee), 0);
    EXPECT_EQ(session->active_resource_count(), 0u);
}

TEST(WebRtcSessionCleanupTest, RemovesPerClientElementsFromBin) {
    SessionFixture fixture;
    ASSERT_TRUE(fixture.valid());

    auto session = make_linked_session(fixture, 2);
    if (!session) {
        GTEST_SKIP() << "Required GStreamer elements for WebRTC session cleanup are unavailable";
    }

    ASSERT_TRUE(has_child(fixture.bin, "client_v_queue_2"));
    ASSERT_TRUE(has_child(fixture.bin, "client_webrtc_2"));

    session->cleanup();

    EXPECT_FALSE(has_child(fixture.bin, "client_v_queue_2"));
    EXPECT_FALSE(has_child(fixture.bin, "client_a_queue_2"));
    EXPECT_FALSE(has_child(fixture.bin, "client_v_pay_2"));
    EXPECT_FALSE(has_child(fixture.bin, "client_a_pay_2"));
    EXPECT_FALSE(has_child(fixture.bin, "client_v_caps_2"));
    EXPECT_FALSE(has_child(fixture.bin, "client_a_caps_2"));
    EXPECT_FALSE(has_child(fixture.bin, "client_webrtc_2"));
}

TEST(WebRtcSessionCleanupTest, RepeatedCleanupIsIdempotent) {
    SessionFixture fixture;
    ASSERT_TRUE(fixture.valid());

    auto session = make_linked_session(fixture, 3);
    if (!session) {
        GTEST_SKIP() << "Required GStreamer elements for WebRTC session cleanup are unavailable";
    }

    session->cleanup();
    session->cleanup();

    EXPECT_TRUE(session->cleaned_up());
    EXPECT_EQ(session->active_resource_count(), 0u);
    EXPECT_EQ(count_src_request_pads(fixture.video_tee), 0);
    EXPECT_EQ(count_src_request_pads(fixture.audio_tee), 0);
}

TEST(WebRtcSessionCleanupTest, ConcurrentCleanupOnlyTearsDownOnce) {
    SessionFixture fixture;
    ASSERT_TRUE(fixture.valid());

    auto session = make_linked_session(fixture, 6);
    if (!session) {
        GTEST_SKIP() << "Required GStreamer elements for WebRTC session cleanup are unavailable";
    }

    std::vector<std::thread> cleanup_threads;
    for (int i = 0; i < 8; ++i) {
        cleanup_threads.emplace_back([session = session.get()]() { session->cleanup(); });
    }

    for (auto &thread : cleanup_threads) {
        thread.join();
    }

    EXPECT_TRUE(session->cleaned_up());
    EXPECT_EQ(session->active_resource_count(), 0u);
    EXPECT_EQ(count_src_request_pads(fixture.video_tee), 0);
    EXPECT_EQ(count_src_request_pads(fixture.audio_tee), 0);
}

TEST(WebRtcSessionCleanupTest, PartialFailedAttachCleanupLeavesNoHalfBuiltSession) {
    SessionFixture fixture;
    ASSERT_TRUE(fixture.valid());

    PekSinkWebRtcSession session(fixture.bin, fixture.video_tee, fixture.audio_tee);
    session.queue = make_element("queue", "partial_queue");
    ASSERT_NE(session.queue, nullptr);
    gst_bin_add(GST_BIN(fixture.bin), session.queue);

    session.tee_src_pad = gst_element_request_pad_simple(fixture.video_tee, "src_%u");
    ASSERT_NE(session.tee_src_pad, nullptr);

    session.cleanup();

    EXPECT_EQ(session.active_resource_count(), 0u);
    EXPECT_EQ(count_src_request_pads(fixture.video_tee), 0);
    EXPECT_FALSE(has_child(fixture.bin, "partial_queue"));
}

TEST(WebRtcSessionCleanupTest, MultipleSessionCleanupDrainsActiveResources) {
    SessionFixture fixture;
    ASSERT_TRUE(fixture.valid());

    auto first = make_linked_session(fixture, 4);
    auto second = make_linked_session(fixture, 5);
    if (!first || !second) {
        GTEST_SKIP() << "Required GStreamer elements for WebRTC session cleanup are unavailable";
    }

    EXPECT_EQ(count_src_request_pads(fixture.video_tee), 2);
    EXPECT_EQ(count_src_request_pads(fixture.audio_tee), 2);

    first->cleanup();
    second->cleanup();

    EXPECT_EQ(first->active_resource_count(), 0u);
    EXPECT_EQ(second->active_resource_count(), 0u);
    EXPECT_EQ(count_src_request_pads(fixture.video_tee), 0);
    EXPECT_EQ(count_src_request_pads(fixture.audio_tee), 0);
}

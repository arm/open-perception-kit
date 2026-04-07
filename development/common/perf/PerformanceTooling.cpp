/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "perf/PerformanceTooling.h"

#include <mutex>

#ifndef DISABLE_STREAMLINE_ANNOTATIONS
#define ANNOTATE_ENABLE
#include "perf/streamline_annotate.h"
#endif

using namespace amp;

#ifndef DISABLE_STREAMLINE_ANNOTATIONS
// Streamline annotations are active
void Streamline::setup() {
    static std::once_flag channels_flag;
    std::call_once(channels_flag, [] {
        createChannel(Channel::Preprocess, 1, "Preprocess");
        createChannel(Channel::Inference, 1, "Inference");
        createChannel(Channel::Postprocess, 1, "Postprocess");
    });

    // should run once per thread
    thread_local bool annotate_ready = false;
    if (!annotate_ready) {
        ANNOTATE_SETUP;
        annotate_ready = true;
    }
}

void Streamline::createChannel(int channelId, int groupId, const std::string &channelName) {
    ANNOTATE_NAME_CHANNEL(channelId, groupId, channelName.c_str());
}

void Streamline::start(int channelId, const std::string &annotation) {
    setup();
    ANNOTATE_CHANNEL(channelId, annotation.c_str());
}

void Streamline::end(int channelId) {
    setup();
    ANNOTATE_CHANNEL_END(channelId);
}
#else
// Streamline annotations are disabled - provide empty implementations
void Streamline::setup() {}
void Streamline::createChannel(int channelId, int groupId, const std::string &channelName) {}
void Streamline::start(int channelId, const std::string &annotation) {}
void Streamline::end(int channelId) {}
#endif

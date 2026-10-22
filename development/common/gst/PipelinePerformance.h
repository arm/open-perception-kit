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
#pragma once

#include "perf/PipelineSample.h"

#include <gst/gst.h>

#include <memory>

namespace opk::gst {

/**
 * Observes one reachable terminal video sink, including downstream conversion,
 * queues and encoding. Latency ends at sink arrival, not presentation or a browser.
 * Does not alter buffers, scheduling, QoS or sink properties.
 */
class PipelinePerformance {
  public:
    using Snapshot = opk::perf::PipelineSample;

    explicit PipelinePerformance(GstElement *const anchor);
    ~PipelinePerformance();
    PipelinePerformance(const PipelinePerformance &) = delete;
    PipelinePerformance &operator=(const PipelinePerformance &) = delete;

    /** Record every input frame, including frames whose inference will be skipped. */
    void markInput(const GstClockTime timestamp);

    /**
     * Refresh topology/configuration and return cumulative measurements. Call at
     * calibration boundaries, not for every frame. A changed generation invalidates
     * the previous window. Empty endpoint or no matched output means unmeasured.
     * Configuration hashes factories, settings and negotiated video caps upstream
     * of the measured sink, without instance names or RTP/WebRTC session identity.
     * Endpoint names remain local to live probe matching. Callers add model, memory
     * layout, stage policy, hardware and implementation identities to cache keys.
     */
    Snapshot snapshot();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace opk::gst

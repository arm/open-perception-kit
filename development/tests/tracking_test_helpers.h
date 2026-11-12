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
#include "opk/Tracking.h"

inline std::unique_ptr<open_perception_kit::metadata::TrackIdentityT>
testTrackIdentity(const uint64_t id) {
    auto value = std::make_unique<open_perception_kit::metadata::TrackIdentityT>();
    value->tracker_id = "test/tracker";
    value->session_id = "test-session";
    value->track_id = id;
    return value;
}
inline std::unique_ptr<open_perception_kit::metadata::TrackingSampleT>
testTrackingSample(const uint64_t index) {
    auto value = std::make_unique<open_perception_kit::metadata::TrackingSampleT>();
    value->frame_index = index;
    value->elapsed_ms = static_cast<double>(index) * 10;
    value->state = open_perception_kit::metadata::TrackObservationState::Observed;
    value->time_source = open_perception_kit::metadata::TrackingTimeSource::MediaDelta;
    return value;
}

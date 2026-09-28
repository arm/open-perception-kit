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

#pragma once

#include <string_view>

namespace opk::content_requirement_event {

/**
 * GstStructure name for the custom upstream event emitted when an active
 * inference element requires content from an upstream inference element.
 * Matching upstream producers activate themselves and continue forwarding the
 * event so that every matching producer can be enabled. The event never
 * disables elements.
 */
inline constexpr std::string_view k_name = "opk-content-required";

/** String field containing the required FrameResults layer content type. */
inline constexpr std::string_view k_content_type_field = "content-type";

} // namespace opk::content_requirement_event

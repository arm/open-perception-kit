/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <string_view>

namespace pek::content_requirement_event {

/**
 * GstStructure name for the custom upstream event emitted when an active
 * inference element requires content from an upstream inference element.
 * Matching upstream producers activate themselves and continue forwarding the
 * event so that every matching producer can be enabled. The event never
 * disables elements.
 */
inline constexpr std::string_view k_name = "pek-content-required";

/** String field containing the required FrameResults layer content type. */
inline constexpr std::string_view k_content_type_field = "content-type";

} // namespace pek::content_requirement_event

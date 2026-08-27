/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <string_view>

namespace pek::model_registration_event {

/**
 * GstStructure contract used by pekinfer to publish model identity, state, and
 * declared content dependencies downstream to peksink.
 */
inline constexpr std::string_view k_name = "pek-model-register";
inline constexpr std::string_view k_model_name_field = "model-name";
inline constexpr std::string_view k_element_name_field = "element-name";
inline constexpr std::string_view k_active_field = "active";
inline constexpr std::string_view k_display_name_field = "display-name";
inline constexpr std::string_view k_task_field = "task";
inline constexpr std::string_view k_runtime_field = "runtime";
inline constexpr std::string_view k_provided_content_types_field = "provided-content-types";
inline constexpr std::string_view k_required_content_types_field = "required-content-types";

} // namespace pek::model_registration_event

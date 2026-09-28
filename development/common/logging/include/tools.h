/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <string>
#include <string_view>

namespace opk::log::tools {

/// Wraps text in a Unicode frame with an optional title.
std::string enframe(const std::string &text, const std::string &title = "");

/// Applies ANSI reverse styling to text.
std::string invert(std::string_view text);

} // namespace opk::log::tools

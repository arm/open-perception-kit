/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <string_view>

namespace pek::log {

// Log levels are ordered by increasing verbosity. A configured level includes messages at that
// level and every less verbose level below it.
enum class Level : int { Off = 0, Error = 1, Warn = 2, Notice = 3, Info = 4, Debug = 5 };

enum class TargetType { Stdout, Stderr, File };

inline constexpr Level defaultLogLevel{Level::Info};

namespace color {
inline constexpr std::string_view BrightRed = "\033[1;31m";
inline constexpr std::string_view BrightYellow = "\033[1;33m";
inline constexpr std::string_view BrightCyan = "\033[1;36m";
inline constexpr std::string_view ResetColor = "\033[0m";
} // namespace color

} // namespace pek::log

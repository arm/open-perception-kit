/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

namespace pek::log {

// Log levels are ordered by increasing verbosity. A configured level includes messages at that
// level and every less verbose level below it.
enum class Level : int { Off = 0, Error = 1, Warn = 2, Notice = 3, Info = 4 };

enum class TargetType { Stdout, Stderr, File };

inline constexpr Level defaultLogLevel{Level::Info};

} // namespace pek::log

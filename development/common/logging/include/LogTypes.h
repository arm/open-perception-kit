/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

namespace pek {

// Log levels are ordered by increasing verbosity. A configured level includes messages at that
// level and every less verbose level below it.
enum class LogLevel : int { Off = 0, Error = 1, Warn = 2, Notice = 3, Info = 4 };

enum class LogTargetType { Stdout, Stderr };

inline constexpr LogLevel defaultLogLevel{LogLevel::Info};

} // namespace pek

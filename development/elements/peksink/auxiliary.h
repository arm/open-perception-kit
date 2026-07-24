/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __AUXILIARY_H__
#define __AUXILIARY_H__

#include <string_view>

namespace peksink::debug_color {
inline constexpr std::string_view BrightRed = "\033[1;31m";
inline constexpr std::string_view BrightYellow = "\033[1;33m";
inline constexpr std::string_view BrightCyan = "\033[1;36m";
inline constexpr std::string_view ResetColor = "\033[0m";
} // namespace peksink::debug_color

#ifndef NDEBUG

#include <filesystem>
#include <utility>

#include "Log.h"

template <class... Args> void dbg(fmt::string_view fmt, Args &&...args) {
    pek::log::infoRuntime(fmt, std::forward<Args>(args)...);
    pek::log::info("\n");
}

#define DBG(fmt, ...)                                                                              \
    do {                                                                                           \
        pek::log::info("{}[{}:{}] {}",                                                             \
                       peksink::debug_color::BrightCyan,                                           \
                       std::filesystem::path(__FILE__).filename().string(),                        \
                       __LINE__,                                                                   \
                       peksink::debug_color::ResetColor);                                          \
        dbg(fmt __VA_OPT__(, ) __VA_ARGS__);                                                       \
    } while (0)

#else

template <class... Args> inline void dbg(std::string_view, Args &&...) {
    // shall do nothing in release version
}

#define DBG(fmt, ...)                                                                              \
    do {                                                                                           \
    } while (0)

#endif // !NDEBUG

#endif // !__AUX_H__

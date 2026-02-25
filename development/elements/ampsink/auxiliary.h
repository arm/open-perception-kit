/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#ifndef __AUXILIARY_H__
#define __AUXILIARY_H__

#ifndef NDEBUG

#include <filesystem>
#include <format>
#include <iostream>
#include <string_view>

enum class _DbgColor {
    Reset,
    BrightRed,
    BrightYellow,
    BrightCyan,
};

inline std::ostream &operator<<(std::ostream &os, _DbgColor c) {
#if defined(_WIN32)
    // Modern Windows terminals support ANSI (Windows 10+)
    // If not, colors will just be ignored
#endif

    switch (c) {
    case _DbgColor::Reset:
        return os << "\033[0m";
    case _DbgColor::BrightRed:
        return os << "\033[1;31m";
    case _DbgColor::BrightYellow:
        return os << "\033[1;33m";
    case _DbgColor::BrightCyan:
        return os << "\033[1;36m";
    }
    return os;
}

template <class... Args> void dbg(std::string_view fmt, Args &&...args) {
    std::cout << std::vformat(fmt, std::make_format_args(args...)) << std::endl;
}

#define DBG(fmt, ...)                                                                              \
    do {                                                                                           \
        std::cout << _DbgColor::BrightCyan << "["                                                  \
                  << std::filesystem::path(__FILE__).filename().string() << ":" << __LINE__        \
                  << "] " << _DbgColor::Reset;                                                     \
        dbg(fmt __VA_OPT__(, ) __VA_ARGS__);                                                       \
    } while (0)

#else

#include <string_view>

template <class... Args> inline void dbg(std::string_view, Args &&...) {
    // shall do nothing in release version
}

#define DBG(fmt, ...)                                                                              \
    do {                                                                                           \
    } while (0)

#endif // !NDEBUG

#endif // !__AUX_H__

#pragma once

#include <fmt/format.h>
#include <utility>

namespace amp {

// Keep it simple for now; easy to extend later (channels/sinks/etc.)
enum class LogLevel { Info, Warn, Error };

// Single chokepoint for output routing/prefixing.
// Later you can add timestamps, thread id, channel, etc. here.
inline void log_write(LogLevel lvl, fmt::string_view msg) {
    switch (lvl) {
    case LogLevel::Info:
        fmt::print("{}", msg);
        break;
    case LogLevel::Warn:
        fmt::print("W: {}", msg);
        break;
    case LogLevel::Error:
        fmt::print("E: {}", msg);
        break;
    }
}

// --- Primary API: compile-time checked formatting (drop-in for fmt::print) ---
// This is what you want to use in normal code.
// It avoids the ambiguity you hit with a string_view overload.
template <typename... Args> inline void log(fmt::format_string<Args...> fmtstr, Args &&...args) {
    // Long-string-proof (dynamic allocation as needed)
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    log_write(LogLevel::Info, s);
}

template <typename... Args> inline void logw(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    log_write(LogLevel::Warn, s);
}

template <typename... Args> inline void loge(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    log_write(LogLevel::Error, s);
}

// --- Optional runtime-format API ---
// Use this only when the format string is not a literal / not known at compile-time.
// Named differently to avoid overload ambiguity with string literals.
template <typename... Args> inline void log_runtime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(std::forward<Args>(args)...));
    log_write(LogLevel::Info, s);
}

template <typename... Args> inline void logw_runtime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(std::forward<Args>(args)...));
    log_write(LogLevel::Warn, s);
}

template <typename... Args> inline void loge_runtime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(std::forward<Args>(args)...));
    log_write(LogLevel::Error, s);
}

} // namespace amp

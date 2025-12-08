#include "Result.h"

#include "amp/String.h"

#include <fmt/format.h>

using namespace amp;

std::string Error::toString() const {
    using fmt::format;
    using fmt::color;
    using fmt::emphasis;
    using fmt::bg;

    auto RED   = bg(color::red);
    auto BLUE  = bg(color::blue);
    auto GREEN = bg(color::green);
    auto YELLOW = bg(color::yellow);

    auto green = [&](auto&& v) {
        return format(GREEN, "{}", std::forward<decltype(v)>(v));
    };

    auto yellow = [&](auto&& v) {
        return format(YELLOW, "{}", std::forward<decltype(v)>(v));
    };

    std::string ret = format("{}{}\n{}{}",
        format(RED, "Error:\n"), yellow(magic_enum::enum_name(flag)),
        format(RED, "Because:\n"), green(info)
    );

    if (sourceLocation.file_name() && sourceLocation.file_name()[0]) {
        ret += format("\n{}{}{}{}{}{}",
            format(BLUE, "Where:\n"), green(sourceLocation.file_name()),
            format(BLUE, "\n"), green(sourceLocation.function_name()),
            format(BLUE, "\n"), green(sourceLocation.line())
        );
    }

    return ret;
}
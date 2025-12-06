#include "Result.h"

#include "amp/String.h"

#include <fmt/format.h>

using namespace amp;

std::string Error::toString() {
    using fmt::format;
    using fmt::color;
    using fmt::emphasis;
    using fmt::bg;

    auto RED   = bg(color::red);
    auto BLUE  = bg(color::blue);
    auto GREEN = bg(color::green);

    auto green = [&](auto&& v) {
        return format(GREEN, "{}", std::forward<decltype(v)>(v));
    };

    std::string ret = format("{}{}{}{}",
        format(RED, "What?"), green(magic_enum::enum_name(resultFlag)),
        format(RED, "Why?"), green(info)
    );

    if (sourceLocation.file_name() && sourceLocation.file_name()[0]) {
        ret += format("{}{}{}{}{}{}",
            format(BLUE, "Where?"), green(sourceLocation.file_name()),
            format(BLUE, "+"), green(sourceLocation.function_name()),
            format(BLUE, "+"), green(sourceLocation.line())
        );
    }

    return ret;
}
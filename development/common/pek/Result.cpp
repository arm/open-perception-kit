/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Result.h"

#include "pek/String.h"

using namespace pek;

std::string Error::toString() const {
    using fmt::bg;
    using fmt::color;
    using fmt::format;

    auto RED = bg(color::red);
    auto BLUE = bg(color::blue);
    auto GREEN = bg(color::green);
    auto YELLOW = bg(color::yellow);

    auto green = [&](auto &&v) { return format(GREEN, "{}", std::forward<decltype(v)>(v)); };

    auto yellow = [&](auto &&v) { return format(YELLOW, "{}", std::forward<decltype(v)>(v)); };

    std::string ret = format("{}{}\n{}{}",
                             format(RED, "Error:\n"),
                             yellow(magic_enum::enum_name(flag)),
                             format(RED, "Because:\n"),
                             green(info));

    if (!file.empty()) {
        ret += format("\n{}{}{}{}{}{}",
                      format(BLUE, "Where:\n"),
                      green(file),
                      format(BLUE, "\n"),
                      green(function),
                      format(BLUE, "\n"),
                      green(line));
    }

    return ret;
}
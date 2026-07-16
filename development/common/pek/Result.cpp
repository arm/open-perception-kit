/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Result.h"
#include "magic_enum/magic_enum.hpp"

using namespace pek;

std::string Error::toString() const {
    using fmt::bg;
    using fmt::color;
    using fmt::fg;
    using fmt::format;

    auto ERROR = fg(color::black) | bg(color::red);
    auto LABEL = fg(color::black) | bg(color::yellow);

    auto INFO = fg(color::green);
    auto WHERE = fg(color::white) | bg(color::blue);

    std::string ret;

    ret += "\n";
    ret += format(ERROR, "Error:");
    ret += "\n";

    ret += format(LABEL, "{}", magic_enum::enum_name(flag));
    ret += "\n";

    ret += format(ERROR, "Reason:");
    ret += "\n";

    ret += format(INFO, "{}", info);

    if (!file.empty()) {
        ret += "\n";
        ret += format(WHERE, "Where");
        ret += "\n";

        ret += format(INFO, "{}\n{}\n{}", file, function, line);
    }

    return ret;
}

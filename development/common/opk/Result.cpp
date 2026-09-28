/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Result.h"
#include "magic_enum/magic_enum.hpp"

using namespace opk;

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

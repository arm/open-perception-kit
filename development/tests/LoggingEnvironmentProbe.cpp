/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "Log.h"

#include <cstdlib>
#include <string_view>

namespace {

void printTargets() {
    for (const auto target : opk::log::getEnabledLogTargets()) {
        switch (target) {
        case opk::log::TargetType::Stdout:
            opk::log::instantInfo("stdout\n");
            break;
        case opk::log::TargetType::Stderr:
            opk::log::instantInfo("stderr\n");
            break;
        case opk::log::TargetType::File:
            opk::log::instantInfo("file\n");
            break;
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        return 1;
    }

    const std::string_view action(argv[1]);
    if (action == "early-output") {
        opk::log::instantInfo("ui stdout\n");
        opk::log::instantError("ui stderr\n");
        opk::log::flush();
        // Early UI output/flush must not capture the environment or start the logger.
        if (setenv("OPK_LOG_LEVEL", "4", 1) != 0 ||        // NOLINT(concurrency-mt-unsafe)
            setenv("OPK_LOG_TARGETS", "stderr", 1) != 0) { // NOLINT(concurrency-mt-unsafe)
            return 1;
        }
        opk::log::info("late info\n");
        opk::log::flush();
        return 0;
    }
    if (action == "level") {
        opk::log::instantInfo("{}\n", opk::log::getLogLevel());
        return 0;
    }
    if (action == "targets") {
        printTargets();
        return 0;
    }
    if (action == "emit") {
        opk::log::info("info\n");
        opk::log::error("error\n");
        opk::log::flush();
        return 0;
    }
    if (action == "debug") {
        opk::log::debug("debug {}", 7);
        opk::log::flush();
        return 0;
    }
    if (action == "enable") {
        opk::log::setLogTargetState(opk::log::TargetType::File, true);
        return 0;
    }
    if (action == "toggle") {
        opk::log::setLogTargetState(opk::log::TargetType::File, true);
        opk::log::info("before");
        opk::log::flush();
        opk::log::setLogTargetState(opk::log::TargetType::File, false);
        opk::log::info("disabled");
        opk::log::flush();
        opk::log::setLogTargetState(opk::log::TargetType::File, true);
        opk::log::info("after");
        opk::log::flush();
        return 0;
    }
    if (action == "emit-targets") {
        opk::log::info("info\n");
        opk::log::flush();
        printTargets();
        return 0;
    }
    return 1;
}

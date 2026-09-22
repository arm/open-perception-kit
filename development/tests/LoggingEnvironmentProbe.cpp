/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

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

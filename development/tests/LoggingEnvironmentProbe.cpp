/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Log.h"

#include <string_view>

namespace {

void printTargets() {
    for (const auto target : pek::log::getEnabledLogTargets()) {
        switch (target) {
        case pek::log::TargetType::Stdout:
            pek::log::instantInfo("stdout\n");
            break;
        case pek::log::TargetType::Stderr:
            pek::log::instantInfo("stderr\n");
            break;
        case pek::log::TargetType::File:
            pek::log::instantInfo("file\n");
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
    if (action == "level") {
        pek::log::instantInfo("{}\n", pek::log::getLogLevel());
        return 0;
    }
    if (action == "targets") {
        printTargets();
        return 0;
    }
    if (action == "emit") {
        pek::log::info("info\n");
        pek::log::error("error\n");
        pek::log::flush();
        return 0;
    }
    if (action == "enable") {
        pek::log::setLogTargetState(pek::log::TargetType::File, true);
        return 0;
    }
    if (action == "toggle") {
        pek::log::setLogTargetState(pek::log::TargetType::File, true);
        pek::log::info("before");
        pek::log::flush();
        pek::log::setLogTargetState(pek::log::TargetType::File, false);
        pek::log::info("disabled");
        pek::log::flush();
        pek::log::setLogTargetState(pek::log::TargetType::File, true);
        pek::log::info("after");
        pek::log::flush();
        return 0;
    }
    if (action == "emit-targets") {
        pek::log::info("info\n");
        pek::log::flush();
        printTargets();
        return 0;
    }
    return 1;
}

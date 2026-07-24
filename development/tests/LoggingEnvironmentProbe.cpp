/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Log.h"

#include <string_view>

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
        for (const auto target : pek::log::getEnabledLogTargets()) {
            pek::log::instantInfo("{}\n",
                                  target == pek::log::LogTargetType::Stdout ? "stdout" : "stderr");
        }
        return 0;
    }
    if (action == "emit") {
        pek::log::info("info\n");
        pek::log::error("error\n");
        pek::log::logFlush();
        return 0;
    }
    return 1;
}

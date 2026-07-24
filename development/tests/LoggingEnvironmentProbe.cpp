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
        pek::forceLog("{}\n", pek::getLogLevel());
        return 0;
    }
    if (action == "targets") {
        for (const auto target : pek::getEnabledLogTargets()) {
            pek::forceLog("{}\n", target == pek::LogTargetType::Stdout ? "stdout" : "stderr");
        }
        return 0;
    }
    if (action == "emit") {
        pek::log("info\n");
        pek::loge("error\n");
        pek::logFlush();
        return 0;
    }
    return 1;
}

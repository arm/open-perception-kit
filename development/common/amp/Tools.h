#pragma once

#include <string>

namespace amp {

struct Tools {

    static bool isRunningInDocker() {
        return true;
    }
    static std::string getLocalIp();
    static void abort();
};
} // namespace amp

#define AMP_ABORT ::abort();

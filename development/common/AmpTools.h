#pragma once

#include <string>

struct AmpTools {

    static bool isRunningInDocker() { return true; }
    static std::string getLocalIp();

};
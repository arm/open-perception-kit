#include "Tools.h"
#include "fmt/base.h"

using namespace amp;

std::string Tools::getLocalIp() {
    // HTTP server must bind to 0.0.0.0 (all interfaces) to accept connections
    // from host machine through Docker port mapping
    return "0.0.0.0";
}

void Tools::abort() {
    fmt::print("Amp is aborting the pipeline..\n");
    ::abort();
}

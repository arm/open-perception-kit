/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/OpChain.h"
#include "runtime/Tools.h"
#include "runtime/VideoFrame.h"

#include <fmt/core.h>

#include <cstddef>
#include <string>
#include <utility>

namespace {

void printUsage(const char *programName) {
    fmt::print(stderr, "Usage: {} <opchain.json> <image.png|image.jpg|image.jpeg>\n", programName);
}

} // namespace

int main(int argc, char **argv) {
    // opchain-exec intentionally has the smallest useful interface for the
    // direct runtime API: one OpChain JSON file and one image file that becomes
    // the synthetic pipelineVideoFrame consumed by existing image opchains.
    if (argc != 3) {
        printUsage(argv[0]);
        return 2;
    }

    const std::string opchainPath = argv[1];
    const std::string imagePath = argv[2];

    // Tools and VideoFrame are part of the public runtime layer. The example
    // does not construct OpChainContext, Perception, or mediaio objects directly.
    std::size_t width = 0;
    std::size_t height = 0;
    auto bgraPixels = pek::runtime::Tools::loadImageFileBgra(imagePath, width, height);
    if (!bgraPixels) {
        fmt::print(stderr, "{}\n", bgraPixels.error().toString());
        return 1;
    }

    auto frame = pek::runtime::VideoFrame::moveBgra(std::move(*bgraPixels), width, height);
    if (!frame) {
        fmt::print(stderr, "{}\n", frame.error().toString());
        return 1;
    }

    auto opChain = pek::runtime::OpChain::fromJsonFile(opchainPath);
    if (!opChain) {
        fmt::print(stderr, "{}\n", opChain.error().toString());
        return 1;
    }

    auto perceptionJson = opChain->run(*frame, "opchain-exec");
    if (!perceptionJson) {
        fmt::print(stderr, "{}\n", perceptionJson.error().toString());
        return 1;
    }

    fmt::print("{}\n", *perceptionJson);
    return 0;
}

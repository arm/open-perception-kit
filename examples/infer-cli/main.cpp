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

<<<<<<< HEAD:examples/infer-cli/main.cpp
pek::Result<void> executeOpChain(const std::string &opchainPath,
                                 std::shared_ptr<pek::mediaio::VideoFrame> frame,
                                 pek::Perception &perception) {
    // The first CLI argument is a normal OpChain JSON file. setupFromFile()
    // parses the descriptor, creates the configured ops, and prepares them for
    // execution.
    pek::op::OpChain opChain;
    auto setupResult = opChain.setupFromFile(opchainPath);
    if (!setupResult) {
        return tl::make_unexpected(std::move(setupResult.error()));
    }

    // OpChainContext is the per-execution state bag shared by ops. For this POC
    // we provide only the pieces required by existing inference chains:
    //   - inferElementId: a stable identifier for diagnostics/perf metadata
    //   - perception: output object populated by postprocessing ops
    //   - pipelineVideoFrame: input frame consumed by image preprocessing ops
    pek::op::OpChainContext opChainContext;
    opChainContext.inferenceInfo.inferElementId = "infer-cli";
    opChainContext.perception = &perception;
    opChainContext.videoFrames["pipelineVideoFrame"] = std::move(frame);

    // execute() runs the configured ops in order. Preprocessing reads the
    // VideoFrame, inference writes tensors, and postprocessing appends
    // structured results to the Perception object supplied above.
    auto executeResult = opChain.execute(opChainContext);
    if (!executeResult) {
        return tl::make_unexpected(std::move(executeResult.error()));
    }

    return {};
=======
void printUsage(const char *programName) {
    fmt::print(stderr, "Usage: {} <opchain.json> <image.png|image.jpg|image.jpeg>\n", programName);
>>>>>>> abf198c (runtime api):examples/opchain-exec/main.cpp
}

} // namespace

int main(int argc, char **argv) {
<<<<<<< HEAD:examples/infer-cli/main.cpp
    // infer-cli intentionally has the smallest useful interface:
    //   1. an OpChain JSON file
    //   2. an image file that becomes the synthetic "pipelineVideoFrame"
    //
    // Keeping this narrow makes it useful as a smoke-test tool for model/opchain
    // work without also becoming a full pipeline runner.
=======
    // opchain-exec intentionally has the smallest useful interface for the
    // direct runtime API: one OpChain JSON file and one image file that becomes
    // the synthetic pipelineVideoFrame consumed by existing image opchains.
>>>>>>> abf198c (runtime api):examples/opchain-exec/main.cpp
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

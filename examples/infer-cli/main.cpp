/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "mediaio/PixelBufferVideoFrame.h"
#include "op/OpChain.h"
#include "op/OpChainContext.h"
#include "pek/Perception.h"
#include "pek/PerceptionSerializer.h"
#include "pek/Result.h"
#include "pek/Tools.h"
#include "pek/Types.h"

#include <fmt/core.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace {

pek::Result<void> executeOpChain(const std::string &opchainPath,
                                 std::shared_ptr<pek::mediaio::VideoFrame> frame,
                                 pek::Perception &perception) {
    // The first CLI argument is a normal OpChain JSON file. setupFromFile()
    // parses the descriptor, creates the configured ops, and prepares them for
    // execution exactly like the GStreamer element would do.
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
}

} // namespace

int main(int argc, char **argv) {
    // infer-cli intentionally has the smallest useful interface:
    //   1. an OpChain JSON file
    //   2. an image file that becomes the synthetic "pipelineVideoFrame"
    //
    // Keeping this narrow makes it useful as a smoke-test tool for model/opchain
    // work without also becoming a full pipeline runner.
    if (argc != 3) {
        fmt::print(stderr, "Usage: <opchain.json> <image.png|image.jpg|image.jpeg>\n");
        return 2;
    }

    const std::string opchainPath = argv[1];
    const std::string imagePath = argv[2];

    // Decode PNG/JPEG input directly into tightly packed BGRA bytes. The loader
    // infers the decoder from the filename extension and writes the image
    // dimensions into width/height.
    size_t width = 0;
    size_t height = 0;
    auto bgraPixels = pek::Tools::loadImageFileBgra(imagePath, width, height);
    if (!bgraPixels) {
        fmt::print(stderr, "{}\n", bgraPixels.error().toString());
        return 1;
    }

    // Wrap the owned BGRA byte vector as a mediaio::VideoFrame. This is the key
    // bridge that lets an image file look like a video frame to the existing
    // opchain. The PixelBufferVideoFrame owns host memory, exposes one plane,
    // and supports read access for preprocessing.
    auto frame = pek::mediaio::makeOwnedPixelBufferVideoFrame(std::move(*bgraPixels),
                                                              static_cast<uint32_t>(width),
                                                              static_cast<uint32_t>(height),
                                                              pek::DataKind::ImageBgraHwc,
                                                              static_cast<uint32_t>(width * 4U),
                                                              pek::AccessMode::Read);
    if (!frame) {
        fmt::print(stderr, "Failed to create PixelBufferVideoFrame\n");
        return 1;
    }

    // OpChainContext stores VideoFrame objects by shared_ptr. The concrete
    // PixelBufferVideoFrame stays alive for the full execution because the
    // shared pointer is moved into the context in executeOpChain().
    std::shared_ptr<pek::mediaio::VideoFrame> sharedFrame(std::move(frame));

    // Perception is the structured runtime result. Postprocessing ops append
    // detections/classifications/etc. to this object while the chain runs.
    pek::Perception perception;
    auto runResult = executeOpChain(opchainPath, std::move(sharedFrame), perception);
    if (!runResult) {
        fmt::print(stderr, "{}\n", runResult.error().toString());
        return 1;
    }

    // PerceptionSerializer provides the nlohmann::json conversion used for the
    // final structured result.
    nlohmann::json perceptionJson = perception;
    fmt::print("{}\n", perceptionJson.dump(2));

    return 0;
}

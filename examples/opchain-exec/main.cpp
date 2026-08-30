/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "PerceptionPacket.h"
#include "TextDisplay.h"
#include "runtime/Logging.h"
#include "runtime/OpChain.h"
#include "runtime/Tools.h"
#include "runtime/VideoFrame.h"

#include <fmt/core.h>

#include <cstddef>
#include <string>
#include <utility>

namespace {

template <typename Payload>
void printPayloadBranch(const perception::container::envelope &frameResults,
                        std::size_t &printedPayloads) {
    PerceptionPacket::visitFrameResultsPayloads<Payload>(
        frameResults, [&printedPayloads](const Payload &payload) {
            ++printedPayloads;
            fmt::print("{}\n", TextDisplay::formatText(payload));
        });
}

void printTypedPayloadText(const perception::container::envelope &frameResults) {
    std::size_t printedPayloads = 0;

    // The packet has already been validated by the example-local PerceptionPacket helper.
    // From here on the example shows the normal typed SDK consumption pattern:
    // choose a generated payload root type, then run a lambda once for every
    // payload of that type in the envelope.
    printPayloadBranch<perception::metadata::FrameContextT>(frameResults, printedPayloads);
    printPayloadBranch<perception::metadata::BoxDetectionsT>(frameResults, printedPayloads);
    printPayloadBranch<perception::metadata::ObjectTracksT>(frameResults, printedPayloads);
    printPayloadBranch<perception::metadata::ClassificationsT>(frameResults, printedPayloads);
    printPayloadBranch<perception::metadata::PoseEstimationsT>(frameResults, printedPayloads);
    printPayloadBranch<perception::metadata::SegmentationMasksT>(frameResults, printedPayloads);
    printPayloadBranch<perception::metadata::ObjectEmbeddingsT>(frameResults, printedPayloads);
    printPayloadBranch<perception::metadata::TrackTracesT>(frameResults, printedPayloads);
    printPayloadBranch<perception::metadata::PerformanceOverlayT>(frameResults, printedPayloads);

    const auto totalPayloads = frameResults.size();
    if (printedPayloads == 0 && totalPayloads == 0) {
        fmt::print("FrameResults: no payloads\n");
    } else if (printedPayloads < totalPayloads) {
        for (std::size_t i = printedPayloads; i < totalPayloads; ++i) {
            fmt::print("Unknown payload type\n");
        }
    }
}

void printUsage(const char *programName) {
    fmt::print(stderr, "Usage: {} <opchain.json> <image.png|image.jpg|image.jpeg>\n", programName);
}

} // namespace

int main(int argc, char **argv) {
    pek::runtime::setLogLevel(pek::runtime::LogLevel::Error);
    pek::runtime::setLogTargetState(pek::runtime::LogTarget::Stdout, false);
    pek::runtime::setLogTargetState(pek::runtime::LogTarget::Stderr, true);
    pek::runtime::setLogTargetState(pek::runtime::LogTarget::File, false);

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

    // runPacket() returns the serialized Perception envelope bytes. Keeping
    // packet transport separate from typed access lets simple tools relay bytes,
    // while typed consumers decode only at the point where they need semantics.
    auto packet = opChain->runPacket(*frame, "opchain-exec");
    if (!packet) {
        fmt::print(stderr, "{}\n", packet.error().toString());
        return 1;
    }

    // The example-local helper validates the FlatBuffers envelope and checks
    // producer identity before generated SDK payload types are visited.
    auto frameResults = PerceptionPacket::decodeFrameResultsPacket(*packet);
    if (!frameResults) {
        fmt::print(stderr, "{}\n", frameResults.error().toString());
        return 1;
    }

    // The payload count helps distinguish an actually empty result from a
    // packet that contains payloads this example does not know how to visit.
    fmt::print("Perception packet bytes: {}, payloads={}\n", packet->size(), frameResults->size());
    printTypedPayloadText(*frameResults);
    return 0;
}

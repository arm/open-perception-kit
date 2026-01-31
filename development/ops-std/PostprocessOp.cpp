#include "PostprocessOp.h"

#include <fmt/core.h>
#include <memory>

#include "amp/TensorView.h"
#include "amp/Types.h"
#include "postproc/PaddleocrParser.h"
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"

#include <PerformanceTracer.h>
#define AMP_PERF_BLOCK(name)                                                                       \
    static amp::PerformanceTracer *tracer = amp::getGlobalTracer();                                \
    std::string traceName = name;                                                                  \
    amp::PerformanceTracer::ScopedTimer timer(tracer, traceName);

using namespace amp;

PostprocessOp::PostprocessOp() {}
PostprocessOp::~PostprocessOp() {}

amp::Result<void> PostprocessOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

amp::Result<void> PostprocessOp::configure(const amp::AttributeMap &attributes) {

    std::string parser = attributes.getStringOrDefault("parser", "");

    if (parser.empty()) {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                        fmt::format("No 'parser' attribute in postprocessor op")));
    }

    if (parser == "PaddleOcrDetectionParser") {
        this->parser = std::make_unique<PaddleOcrDetectionParser>();
    } else if (parser == "YoloLikeParser") {
        this->parser = std::make_unique<YoloLikeParser>();
    } else if (parser == "UltrafaceParser") {
        this->parser = std::make_unique<UltraFaceParser>();
    } else {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                        fmt::format("No tensor parser with name: [{}]", parser)));
    }

    return {};
}

amp::Result<void> PostprocessOp::process(amp::OpChainContext &opChainContext) {
    AMP_PERF_BLOCK(fmt::format("std/PostprocessOp/{}", opChainContext.inferenceInfo.modelFamily));

    amp::TensorParser::Input tensorParserInput;

    // populate tensors
    for (size_t i = 0; i < amp::MaxTensorCount; i++) {
        if (i < opChainContext.inferenceOutputTensorCount)
            tensorParserInput.tensors[i] = &opChainContext.inferenceOutputTensors[i];
        else
            tensorParserInput.tensors[i] = nullptr;
    }

    // copy active inference info
    tensorParserInput.inferenceInfo = opChainContext.inferenceInfo;

    amp::RawDetectionLayer rawDetectionLayer;
    rawDetectionLayer.modelFamily = opChainContext.inferenceInfo.modelFamily;
    auto parseResult = parser->parse(tensorParserInput, rawDetectionLayer);
    if (!parseResult) {
        return parseResult;
    }

    opChainContext.perceptionContext->rawDetections.push_back(rawDetectionLayer);

    // fmt::print("Detected rects: {}\n", rawDetectionLayer.rects.size());

    return {};
}

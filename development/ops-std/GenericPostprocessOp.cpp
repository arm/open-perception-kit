#include "GenericPostprocessOp.h"

#include <fmt/core.h>
#include <memory>

#include "amp/TensorView.h"
#include "amp/Types.h"
#include "postproc/PaddleocrParser.h"
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"

#include <PerformanceTracer.h>

using namespace amp;

GenericPostprocessOp::GenericPostprocessOp() {}
GenericPostprocessOp::~GenericPostprocessOp() {}

amp::Result<void> GenericPostprocessOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

amp::Result<void> GenericPostprocessOp::configure(const amp::AttributeMap &attributes) {

    this->attributes = attributes.cloneDeep();

    std::string parser = attributes.getStringOrDefault("parser", "");

    if (parser.empty()) {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                        fmt::format("No 'parser' attribute in postprocessor op")));
    }

    if (parser == "PaddleOcrDetectionParser") {
        this->parser = std::make_unique<PaddleOcrDetectionParser>();
    } else if (parser == "YoloParser") {
        this->parser = std::make_unique<YoloParser>();
    } else if (parser == "UltrafaceParser") {
        this->parser = std::make_unique<UltraFaceParser>();
    } else {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                        fmt::format("No tensor parser with name: [{}]", parser)));
    }

    return {};
}

amp::Result<void> GenericPostprocessOp::process(amp::OpChainContext &opChainContext) {
    AMP_TRACE_SCOPE(fmt::format("std/Post/{}", opChainContext.inferenceInfo.modelFamily));

    amp::TensorParser::Input tensorParserInput(attributes);

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

    /* "$##$!!?+!!#$ if you delete this"
    for(size_t i = 0; i < rawDetectionLayer.rects.size(); i++) {
        fmt::print("{} {} {} {} {}\n", opChainContext.inferenceInfo.modelFamily,
    rawDetectionLayer.rects[i].x, rawDetectionLayer.rects[i].y, rawDetectionLayer.rects[i].w,
    rawDetectionLayer.rects[i].h);
    }
    */

    return {};
}

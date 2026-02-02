#include "Postprocess.h"

#include <fmt/core.h>
#include <memory>

#include "amp/TensorView.h"
#include "postproc/PaddleocrParser.h"
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"

using namespace amp;

Postprocess::Postprocess() {}
Postprocess::~Postprocess() {}

amp::Result<void> Postprocess::peek(amp::OpChainContext &opChainContext) {
    return {};
}

amp::Result<void> Postprocess::configure(const amp::AttributeMap &attributes) {

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

amp::Result<void> Postprocess::process(amp::OpChainContext &opChainContext) {

    amp::RawDetectionLayer rawDetectionLayer;
    rawDetectionLayer.modelFamily = opChainContext.modelFamily;
    auto parseResult = parser->parse(opChainContext.tensorParserInput, rawDetectionLayer);
    if (!parseResult) {
        return parseResult;
    }

    opChainContext.perceptionContext->rawDetections.push_back(rawDetectionLayer);

    return {};
}

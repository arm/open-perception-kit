#include "Postprocess.h"

#include <fmt/core.h>
#include <memory>
#include <onnxruntime_cxx_api.h>

#include "postproc/PaddleocrParser.h"
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"

using namespace amp;

Postprocess::Postprocess() {}
Postprocess::~Postprocess() {}

amp::Result<void> Postprocess::configure(const amp::AttributeMap &attributes) {

    std::string parser;

    try {
        parser = attributes.getString("parser");
    } catch (const AttributeError &error) {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData, error.what()));
    }

    if (parser == "PaddleOcrDetectionParser") {
        this->parser = std::make_unique<PaddleOcrDetectionParser>();
    } else if (parser == "YoloParser") {
        this->parser = std::make_unique<YoloLikeParser>();
    } else if (parser == "UltrafaceParser") {
        this->parser = std::make_unique<UltraFaceParser>();
    } else {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                        fmt::format("No ensor parser with name: [{}]", parser)));
    }

    return {};
}

amp::Result<void> Postprocess::process(amp::OpChainContext &opChainContext) {
    return {};
}

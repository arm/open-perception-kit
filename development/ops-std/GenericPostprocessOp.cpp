/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "GenericPostprocessOp.h"

#include <fmt/core.h>
#include <functional>
#include <map>
#include <memory>

#include "pek/FrameResults.h"
#include "pek/Types.h"
#include <perf/PerformanceMetrics.h>
#include <perf/PerformanceTracer.h>

// parser class headers
#include "postproc/CameraContactParser.h"
#include "postproc/DummyParser.h"
#include "postproc/GazeDetectionParser.h"
#include "postproc/ImageNetClassificationParser.h"
#include "postproc/ModNetSegmentationParser.h"
#include "postproc/ObjectEmbeddingParser.h"
#include "postproc/PaddleocrParser.h"
#include "postproc/PersonClassificationParser.h"
#include "postproc/RvmParser.h"
#include "postproc/ScrfdParser.h"
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"
#include "postproc/YoloXParser.h"
// ... add new parser headers here

using namespace pek::stdop;

namespace {

using ParserCreator = std::function<std::unique_ptr<pek::TensorParser>()>;

template <class T> ParserCreator make() {
    return []() { return std::make_unique<T>(); };
}

// parser registry
const std::map<std::string, ParserCreator> &getParserRegistry() {
    static const std::map<std::string, ParserCreator> registry = {
        {"CameraContactParser", make<pek::stdop::postproc::CameraContactParser>()},
        {"DummyParser", make<pek::stdop::postproc::DummyParser>()},
        {"GazeDetectionParser", make<pek::stdop::postproc::GazeDetectionParser>()},
        {"ImageNetClassificationParser",
         make<pek::stdop::postproc::ImageNetClassificationParser>()},
        {"ModNetSegmentationParser", make<pek::stdop::postproc::ModNetSegmentationParser>()},
        {"ObjectEmbeddingParser", make<pek::stdop::postproc::ObjectEmbeddingParser>()},
        {"PaddleOcrDetectionParser", make<pek::stdop::postproc::PaddleOcrDetectionParser>()},
        {"PersonClassificationParser", make<pek::stdop::postproc::PersonClassificationParser>()},
        {"RvmParser", make<pek::stdop::postproc::RvmParser>()},
        {"ScrfdParser", make<pek::stdop::postproc::ScrfdParser>()},
        {"UltrafaceParser", make<pek::stdop::postproc::UltraFaceParser>()},
        {"YoloXParser", make<pek::stdop::postproc::YoloXParser>()},
        {"YoloParser", make<pek::stdop::postproc::YoloParser>()},
        // ... add new parsers here
    };
    return registry;
}

} // namespace

GenericPostprocessOp::GenericPostprocessOp() = default;
GenericPostprocessOp::~GenericPostprocessOp() = default;

pek::Result<void> GenericPostprocessOp::bind(size_t index, const std::vector<pek::op::Op *> &ops) {
    return {};
}

pek::Result<void> GenericPostprocessOp::configure(const pek::AttributeMap &attributes) {

    this->attributes = attributes.cloneDeep();

    parserName = attributes.getStringOrDefault("parser", "");
    if (instanceId.empty())
        instanceId = fmt::format("GenericPostprocess-{}", index);

    if (parserName.empty()) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                        fmt::format("No 'parser' attribute in postprocessor op")));
    }

    const auto &registry = getParserRegistry();
    auto it = registry.find(parserName);
    if (it == registry.end()) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("No tensor parser with name: [{}]", parserName)));
    }

    this->parser = it->second();

    return {};
}

std::vector<std::string_view> GenericPostprocessOp::getProvidedContentTypes() const {
    return parser ? parser->getProvidedContentTypes() : std::vector<std::string_view>{};
}

pek::Result<pek::op::OpSignal>
GenericPostprocessOp::process(pek::op::OpChainContext &opChainContext) {
    PEK_TRACE_SCOPE(fmt::format("std/Post/{}", opChainContext.inferenceInfo.modelName));
    PEK_PERF_SCOPE(fmt::format("std/Post/{}", opChainContext.inferenceInfo.modelName));

    pek::TensorParser::Input tensorParserInput(attributes);

    // populate tensors
    for (size_t i = 0; i < pek::MaxTensorCount; i++) {
        if (i < opChainContext.inferenceOutputTensorCount)
            tensorParserInput.tensors[i] = &opChainContext.inferenceOutputTensors[i];
        else
            tensorParserInput.tensors[i] = nullptr;
    }

    // copy active inference info
    tensorParserInput.inferenceInfo = opChainContext.inferenceInfo;
    tensorParserInput.producerInfo = producerInfo(
        opChainContext.inferenceInfo.inferElementId, parserName, "pek-std-ops/GenericPostprocess");

    if (auto parseResult = parser->parse(tensorParserInput, *opChainContext.frameResults);
        !parseResult) {
        return tl::unexpected(parseResult.error());
    }

    return pek::op::OpSignal::Continue;
}

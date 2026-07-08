/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "GenericPostprocessOp.h"

#include <fmt/core.h>
#include <functional>
#include <map>
#include <memory>

#include "pek/Perception.h"
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

    std::string parser = attributes.getStringOrDefault("parser", "");

    if (parser.empty()) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                        fmt::format("No 'parser' attribute in postprocessor op")));
    }

    const auto &registry = getParserRegistry();
    auto it = registry.find(parser);
    if (it == registry.end()) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                        fmt::format("No tensor parser with name: [{}]", parser)));
    }

    this->parser = it->second();

    return {};
}

pek::Result<pek::op::OpSignal>
GenericPostprocessOp::process(pek::op::OpChainContext &opChainContext) {
    PEK_TRACE_SCOPE(fmt::format("std/Post/{}", opChainContext.inferenceInfo.modelFamily));
    PEK_PERF_SCOPE(fmt::format("std/Post/{}", opChainContext.inferenceInfo.modelFamily));

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

    pek::Perception::Layer rawDetectionLayer;
    rawDetectionLayer.model = opChainContext.inferenceInfo.modelFamily;
    rawDetectionLayer.inferElementId = opChainContext.inferenceInfo.inferElementId;
    auto parseResult = parser->parse(tensorParserInput, rawDetectionLayer);
    if (!parseResult) {
        return tl::unexpected(parseResult.error());
    }

    // set parent uids
    for (auto &det : rawDetectionLayer.detections) {
        pek::Perception::Object &obj = std::visit(
            [](auto &v) -> pek::Perception::Object & {
                return static_cast<pek::Perception::Object &>(v);
            },
            det);

        obj.parentUuid = opChainContext.inferenceSourceUuid;
    }

    opChainContext.perception->layers.push_back(rawDetectionLayer);

    if (!opChainContext.rootLayer.detections.empty() && !opChainContext.hasRootLayer) {
        opChainContext.perception->layers.push_back(opChainContext.rootLayer);
        opChainContext.hasRootLayer = true;
    }

    return pek::op::OpSignal::Continue;
}

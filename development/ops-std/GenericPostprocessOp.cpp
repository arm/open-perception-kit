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
#include <PerformanceTracer.h>

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
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"
// ... add new parser headers here

using namespace pek;

namespace {

using ParserCreator = std::function<std::unique_ptr<pek::TensorParser>()>;

template <class T> ParserCreator make() {
    return []() { return std::make_unique<T>(); };
}

// parser registry
const std::map<std::string, ParserCreator> &getParserRegistry() {
    static const std::map<std::string, ParserCreator> registry = {
        {"CameraContactParser", make<pek::CameraContactParser>()},
        {"DummyParser", make<pek::DummyParser>()},
        {"GazeDetectionParser", make<pek::GazeDetectionParser>()},
        {"ImageNetClassificationParser", make<pek::ImageNetClassificationParser>()},
        {"ModNetSegmentationParser", make<pek::ModNetSegmentationParser>()},
        {"ObjectEmbeddingParser", make<pek::ObjectEmbeddingParser>()},
        {"PaddleOcrDetectionParser", make<pek::PaddleOcrDetectionParser>()},
        {"PersonClassificationParser", make<pek::PersonClassificationParser>()},
        {"RvmParser", make<pek::RvmParser>()},
        {"UltrafaceParser", make<pek::UltraFaceParser>()},
        {"YoloParser", make<pek::YoloParser>()},
        // ... add new parsers here
    };
    return registry;
}

} // namespace

GenericPostprocessOp::GenericPostprocessOp() {}
GenericPostprocessOp::~GenericPostprocessOp() {}

pek::Result<void> GenericPostprocessOp::bind(size_t index, const std::vector<pek::Op *> &ops) {
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

pek::Result<void> GenericPostprocessOp::process(pek::OpChainContext &opChainContext) {
    PEK_TRACE_SCOPE(fmt::format("std/Post/{}", opChainContext.inferenceInfo.modelFamily));

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
        return parseResult;
    }

    // set parent uids
    for (auto &det : rawDetectionLayer.detections) {
        Perception::Object &obj = std::visit(
            [](auto &v) -> Perception::Object & { return static_cast<Perception::Object &>(v); },
            det);

        obj.parentUuid = opChainContext.inferenceSourceUuid;
    }

    opChainContext.perception->layers.push_back(rawDetectionLayer);

    if (!opChainContext.rootLayer.detections.empty() && !opChainContext.hasRootLayer) {
        opChainContext.perception->layers.push_back(opChainContext.rootLayer);
        opChainContext.hasRootLayer = true;
    }

    return {};
}

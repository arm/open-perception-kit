/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "GenericPostprocessOp.h"

#include <fmt/core.h>
#include <functional>
#include <map>
#include <memory>

#include "amp/Perception.h"
#include "amp/Types.h"
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

using namespace amp;

namespace {

using ParserCreator = std::function<std::unique_ptr<amp::TensorParser>()>;

#define REG_PARSER(name, cls) {#name, []() { return std::make_unique<cls>(); }},

const std::map<std::string, ParserCreator>& getParserRegistry() {
    static const std::map<std::string, ParserCreator> registry = {
        // parser registration
        REG_PARSER(PaddleOcrDetectionParser, amp::PaddleOcrDetectionParser)
        REG_PARSER(YoloParser, amp::YoloParser)
        REG_PARSER(ImageNetClassificationParser, amp::ImageNetClassificationParser)
        REG_PARSER(PersonClassificationParser, amp::PersonClassificationParser)
        REG_PARSER(ObjectEmbeddingParser, amp::ObjectEmbeddingParser)
        REG_PARSER(DummyParser, amp::DummyParser)
        REG_PARSER(RvmParser, amp::RvmParser)
        REG_PARSER(GazeDetectionParser, amp::GazeDetectionParser)
        REG_PARSER(CameraContactParser, amp::CameraContactParser)
        REG_PARSER(UltrafaceParser, amp::UltraFaceParser)
        REG_PARSER(ModNetSegmentationParser, amp::ModNetSegmentationParser)
        // ... add new parsers here
    };
    return registry;
}

#undef REG_PARSER

} // namespace

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

    const auto& registry = getParserRegistry();
    auto it = registry.find(parser);
    if (it == registry.end()) {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::InvalidData,
                                        fmt::format("No tensor parser with name: [{}]", parser)));
    }

    this->parser = it->second();

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

    amp::Perception::Layer rawDetectionLayer;
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

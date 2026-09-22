/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceOp.h"
#include "Inference.h"
#include "opk/Result.h"
#include "tl/expected.hpp"

#include <cassert>
#include <fmt/core.h>
#include <memory>

#include <perf/PerformanceMetrics.h>

using namespace opk::extrch;

InferenceOp::InferenceOp() = default;
InferenceOp::~InferenceOp() = default;

opk::Result<void> InferenceOp::bind(size_t index, const std::vector<opk::op::Op *> &ops) {
    return {};
}

opk::Result<void> InferenceOp::configure(const opk::AttributeMap &attributes) {
    std::string modelDescPath;

    try {
        modelDescPath = attributes.getString("modelDescriptor");
    } catch (const opk::AttributeError &error) {

        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      fmt::format("Missing required attribute in InferenceOp: {}", error.what())));
    }

    try {
        inference = std::make_unique<opk::extrch::Inference>();

        auto setupResult = inference->setupFromJson(modelDescPath);
        if (!setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                        fmt::format("Executorch startup error: {}", e.what())));
    }

    return {};
}

opk::Result<opk::op::OpSignal> InferenceOp::process(opk::op::OpChainContext &opChainContext) {
    OPK_PERF_SCOPE(fmt::format("extrch/Infer/{}", opChainContext.inferenceInfo.modelName));

    // Preprocess has already written into Inference input buffers through OpInterfaceInference.
    auto inferenceResult = inference->inference();
    if (!inferenceResult) {
        return tl::unexpected(inferenceResult.error());
    }

    // Publish non-owning output views for the next op, usually GenericPostprocess.
    size_t outputTensorCount = inference->getModel().outputs.size();
    opChainContext.inferenceOutputTensorCount = outputTensorCount;
    for (size_t i = 0; i < outputTensorCount; i++) {
        opChainContext.inferenceOutputTensors[i] = inference->getModel().createOutputTensorView(
            i, inference->getOutputTensorDataAddress(i), inference->getOutputTensorFinalShape(i));
    }

    return opk::op::OpSignal::Continue;
}

const opk::Model &InferenceOp::getModel() const {
    assert(inference);
    return inference->getModel();
}

uint8_t *InferenceOp::getTensorDataAddress(size_t index) const {
    assert(inference);
    return inference->getInputTensorDataAddress(index);
}

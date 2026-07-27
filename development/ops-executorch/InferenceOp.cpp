/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceOp.h"
#include "Inference.h"
#include "pek/ModelDescriptor.h"
#include "pek/Result.h"
#include "tl/expected.hpp"

#include <cassert>
#include <fmt/core.h>
#include <memory>

#include <perf/PerformanceTracer.h>

using namespace pek::extrch;

InferenceOp::InferenceOp() = default;
InferenceOp::~InferenceOp() = default;

pek::Result<void> InferenceOp::bind(size_t index, const std::vector<pek::op::Op *> &ops) {
    return {};
}

pek::Result<void> InferenceOp::configure(const pek::AttributeMap &attributes,
                                         std::stop_token stopToken) {
    std::string modelDescPath;

    try {
        modelDescPath = attributes.getString("modelDescriptor");
    } catch (const pek::AttributeError &error) {

        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                      fmt::format("Missing required attribute in InferenceOp: {}", error.what())));
    }

    try {
        inference = std::make_unique<pek::extrch::Inference>();

        auto modelDescriptor = pek::ModelDescriptor::fromFile(modelDescPath, stopToken);
        if (!modelDescriptor)
            return tl::unexpected{modelDescriptor.error()};

        if (auto setupResult = inference->setup(*modelDescriptor); !setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InferenceRtStartupError,
                                        fmt::format("Executorch startup error: {}", e.what())));
    }

    return {};
}

pek::Result<pek::op::OpSignal> InferenceOp::process(pek::op::OpChainContext &opChainContext) {
    PEK_TRACE_SCOPE(fmt::format("extrch/Infer/{}", opChainContext.inferenceInfo.modelFamily));

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

    return pek::op::OpSignal::Continue;
}

const pek::Model &InferenceOp::getModel() const {
    assert(inference);
    return inference->getModel();
}

uint8_t *InferenceOp::getTensorDataAddress(size_t index) const {
    assert(inference);
    return inference->getInputTensorDataAddress(index);
}

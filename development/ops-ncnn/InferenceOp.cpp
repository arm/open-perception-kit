/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceOp.h"

#include <cassert>
#include <fmt/core.h>
#include <memory>

#include "Inference.h"
#include "op/OpSetupContext.h"
#include "pek/AttributeMap.h"
#include "pek/Result.h"

#include <perf/PerformanceTracer.h>

using namespace pek::ncnnrt;

InferenceOp::InferenceOp() = default;
InferenceOp::~InferenceOp() = default;

pek::Result<void> InferenceOp::bind(size_t index, const std::vector<pek::op::Op *> &ops) {
    return {};
}

pek::Result<void> InferenceOp::configure(const pek::AttributeMap &attributes,
                                         pek::op::OpSetupContext &setupContext) {
    std::string modelDescPath;

    try {
        modelDescPath = attributes.getString("modelDescriptor");
    } catch (const pek::AttributeError &error) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                      fmt::format("Missing required attribute in InferenceOp: {}", error.what())));
    }

    try {
        inference = std::make_unique<pek::ncnnrt::Inference>();

        auto modelDescriptor = setupContext.resolveModelDescriptor(modelDescPath);
        if (!modelDescriptor)
            return tl::unexpected{modelDescriptor.error()};

        auto setupResult = inference->setup(*modelDescriptor);
        if (!setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InferenceRtStartupError,
                                        fmt::format("NCNN startup error: {}", e.what())));
    }

    modelFamily = inference->getModel().modelFamily;

    return {};
}

pek::Result<pek::op::OpSignal> InferenceOp::process(pek::op::OpChainContext &opChainContext) {
    PEK_TRACE_SCOPE(fmt::format("ncnn/Infer/{}", opChainContext.inferenceInfo.modelFamily));

    auto inferenceResult = inference->inference();
    if (!inferenceResult) {
        return tl::unexpected(inferenceResult.error());
    }

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

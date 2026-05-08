/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceOp.h"

#include <fmt/core.h>
#include <memory>

#include "op/OpChainContext.h"
#include "pek/AttributeMap.h"
#include "pek/TensorView.h"

#include <PerformanceTracer.h>

using namespace hailort;

InferenceOp::InferenceOp() {}

InferenceOp::~InferenceOp() {}

pek::Result<void> InferenceOp::configure(const pek::AttributeMap &attributes) {
    std::string modelDescPath;

    try {
        modelDescPath = attributes.getString("modelDescriptor");
    } catch (const pek::AttributeError &error) {

        return tl::unexpected(PEK_ERROR(
            pek::ErrorFlag::InvalidOpChain,
            fmt::format("Missing required attribute in Hailo InferenceOp: {}", error.what())));
    }

    try {
        inference = std::make_unique<hailort::Inference>();

        auto setupResult = inference->setupFromJson(modelDescPath);
        if (!setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::OnnxStartupException,
                                        fmt::format("HailoRT startup error: {}", e.what())));
    }

    modelFamily = inference->getModel().modelFamily;

    return {};
}

pek::Result<void> InferenceOp::process(pek::OpChainContext &opChainContext) {
    PEK_TRACE_SCOPE(fmt::format("hailort/Infer/{}", opChainContext.inferenceInfo.modelFamily));

    auto inferenceResult = inference->inference();
    if (!inferenceResult) {
        fmt::print("HailoRT inference error: {}\n", inferenceResult.error().toString());
        return inferenceResult;
    }

    const pek::Model &model = inference->getModel();

    size_t outputTensorCount = model.outputs.size();
    opChainContext.inferenceOutputTensorCount = outputTensorCount;

    for (size_t i = 0; i < outputTensorCount; i++) {
        const pek::Shape outShape = inference->getOutputTensorFinalShape(i);
        const uint8_t *outData = inference->getOutputTensorDataAddress(i);

        opChainContext.inferenceOutputTensors[i] =
            model.createOutputTensorView(i, outData, outShape);
    }

    return {};
}

pek::Result<void> InferenceOp::bind(size_t index, const std::vector<pek::Op *> &ops) {
    return {};
}

const pek::Model &InferenceOp::getModel() const {
    assert(inference);
    return inference->getModel();
}

uint8_t *InferenceOp::getTensorDataAddress(size_t index) const {
    assert(inference);
    return inference->getInputTensorDataAddress(index);
}

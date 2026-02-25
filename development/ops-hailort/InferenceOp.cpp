/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceOp.h"

#include <fmt/core.h>
#include <memory>

#include "amp/AttributeMap.h"
#include "amp/TensorView.h"
#include "op/OpChainContext.h"

#include <PerformanceTracer.h>

using namespace hailort;

InferenceOp::InferenceOp() {}

InferenceOp::~InferenceOp() {}

amp::Result<void> InferenceOp::configure(const amp::AttributeMap &attributes) {
    std::string modelDescPath;

    try {
        modelDescPath = attributes.getString("modelDescriptor");
    } catch (const amp::AttributeError &error) {

        return tl::unexpected(AMP_ERROR(
            amp::ErrorFlag::InvalidOpChain,
            fmt::format("Missing required attribute in Hailo InferenceOp: {}", error.what())));
    }

    try {
        inference = std::make_unique<hailort::Inference>();

        auto setupResult = inference->setupFromJson(modelDescPath);
        if (!setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::OnnxStartupException,
                                        fmt::format("HailoRT startup error: {}", e.what())));
    }

    modelFamily = inference->getModel().modelFamily;

    return {};
}

amp::Result<void> InferenceOp::process(amp::OpChainContext &opChainContext) {
    AMP_TRACE_SCOPE(fmt::format("hailort/Infer/{}", opChainContext.inferenceInfo.modelFamily));

    auto inferenceResult = inference->inference();
    if (!inferenceResult) {
        fmt::print("HailoRT inference error: {}\n", inferenceResult.error().toString());
        return inferenceResult;
    }

    const amp::Model &model = inference->getModel();

    size_t outputTensorCount = model.outputs.size();
    opChainContext.inferenceOutputTensorCount = outputTensorCount;

    for (size_t i = 0; i < outputTensorCount; i++) {
        const amp::Shape outShape = inference->getOutputTensorFinalShape(i);
        const uint8_t *outData = inference->getOutputTensorDataAddress(i);

        opChainContext.inferenceOutputTensors[i] =
            model.createOutputTensorView(i, outData, outShape);
    }

    return {};
}

amp::Result<void> InferenceOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

const amp::Model &InferenceOp::getModel() const {
    assert(inference);
    return inference->getModel();
}

uint8_t *InferenceOp::getTensorDataAddress(size_t index) const {
    assert(inference);
    return inference->getInputTensorDataAddress(index);
}

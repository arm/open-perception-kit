/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "InferenceOp.h"

#include <fmt/core.h>
#include <memory>
#include <onnxruntime_cxx_api.h>

#include "Inference.h"
#include "glib-object.h"
#include "glib.h"
#include "gst/gstpad.h"
#include "op/OpChainContext.h"
#include "op/OpSetupContext.h"
#include "pek/AttributeMap.h"
#include "pek/ModelDescriptor.h"
#include "pek/TensorView.h"
#include "pek/Tools.h"

#include <perf/PerformanceTracer.h>

using namespace pek::onnx;

InferenceOp::InferenceOp() = default;

InferenceOp::~InferenceOp() = default;

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
        inference = std::make_unique<onnx::Inference>();

        auto modelDescriptor = setupContext.resolveModelDescriptor(modelDescPath);
        if (!modelDescriptor)
            return tl::unexpected{modelDescriptor.error()};

        auto setupResult = inference->setup(*modelDescriptor);
        if (!setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InferenceRtStartupError,
                                        fmt::format("OnnxRT startup error: {}", e.what())));
    }

    return {};
}

pek::Result<pek::op::OpSignal> InferenceOp::process(pek::op::OpChainContext &opChainContext) {
    PEK_TRACE_SCOPE(fmt::format("onnx/Infer/{}", opChainContext.inferenceInfo.modelFamily));

    // inference
    auto inferenceResult = inference->inference();
    if (!inferenceResult) {
        return tl::unexpected(inferenceResult.error());
    }

    // populate the context with views to the output tensors
    size_t outputTensorCount = inference->getModel().outputs.size();
    opChainContext.inferenceOutputTensorCount = outputTensorCount;
    for (size_t i = 0; i < outputTensorCount; i++) {
        opChainContext.inferenceOutputTensors[i] = inference->getModel().createOutputTensorView(
            i, inference->getOutputTensorDataAddress(i), inference->getOutputTensorFinalShape(i));
    }

    return pek::op::OpSignal::Continue;
}

pek::Result<void> InferenceOp::bind(size_t index, const std::vector<pek::op::Op *> &ops) {
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

/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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
#include "opk/AttributeMap.h"
#include "opk/ModelDescriptor.h"
#include "opk/TensorView.h"
#include "opk/Tools.h"

#include <perf/PerformanceMetrics.h>

using namespace opk::onnx;

InferenceOp::InferenceOp() = default;

InferenceOp::~InferenceOp() = default;

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
        inference = std::make_unique<onnx::Inference>();

        auto setupResult = inference->setupFromJson(modelDescPath);
        if (!setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                        fmt::format("OnnxRT startup error: {}", e.what())));
    }

    return {};
}

opk::Result<opk::op::OpSignal> InferenceOp::process(opk::op::OpChainContext &opChainContext) {
    OPK_PERF_SCOPE(fmt::format("onnx/Infer/{}", opChainContext.inferenceInfo.modelName));

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

    return opk::op::OpSignal::Continue;
}

opk::Result<void> InferenceOp::bind(size_t index, const std::vector<opk::op::Op *> &ops) {
    return {};
}

const opk::Model &InferenceOp::getModel() const {
    assert(inference);
    return inference->getModel();
}

uint8_t *InferenceOp::getTensorDataAddress(size_t index) const {
    assert(inference);
    return inference->getInputTensorDataAddress(index);
}

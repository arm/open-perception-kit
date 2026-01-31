#include "InferenceOp.h"

#include <fmt/core.h>
#include <memory>
#include <onnxruntime_cxx_api.h>

#include "Inference.h"
#include "ModelDescriptor.h"
#include "amp/AttributeMap.h"
#include "amp/BitmapView.h"
#include "amp/TensorView.h"
#include "amp/Tools.h"
#include "glib-object.h"
#include "glib.h"
#include "gst/gstpad.h"
#include "op/OpChainContext.h"

using namespace onnx;

InferenceOp::InferenceOp() {}

InferenceOp::~InferenceOp() {}

amp::Result<void> InferenceOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

amp::Result<void> InferenceOp::configure(const amp::AttributeMap &attributes) {
    std::string modelDescPath;

    try {
        modelDescPath = attributes.getString("modelDescriptor");
    } catch (const amp::AttributeError &error) {
        AMP_ABORT; // todo
    }

    try {
        inference = std::make_unique<onnx::Inference>();

        auto setupResult = inference->setupFromJson(modelDescPath);
        if (!setupResult) {
            fmt::print("{}\n", setupResult.error().toString());
            AMP_ABORT; // todo
        }
    } catch (const std::exception &e) {
        amp::Error err = AMP_ERROR(amp::ErrorFlag::OnnxStartupException, e.what());
        fmt::print("{}\n", err.toString());
        AMP_ABORT; // todo
    }

    modelFamily = inference->getModel().modelFamily;

    return {};
}

amp::Result<void> InferenceOp::process(amp::OpChainContext &opChainContext) {
    amp::BitmapView pipelineVideoFrame = opChainContext.bitmapViews["pipelineVideoFrame"];

    // inference
    auto inferenceResult = inference->inference();
    if (!inferenceResult) {
        return inferenceResult;
    }

    // populate the context with views to the output tensors
    size_t outputTensorCount = inference->getModel().outputs.size();
    opChainContext.inferenceOutputTensorCount = outputTensorCount;
    for (size_t i = 0; i < outputTensorCount; i++) {
        opChainContext.inferenceOutputTensors[i] = inference->getModel().createOutputTensorView(
            i, inference->getOutputTensorDataAddress(i), inference->getOutputTensorFinalShape(i));
    }

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

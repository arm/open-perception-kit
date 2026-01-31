#include "PreprocessAndInference.h"

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

using namespace onnx;

PreprocessAndInference::PreprocessAndInference() {}

PreprocessAndInference::~PreprocessAndInference() {}

amp::Result<void> PreprocessAndInference::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

amp::Result<void> PreprocessAndInference::configure(const amp::AttributeMap &attributes) {

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

    return {};
}

amp::Result<void> PreprocessAndInference::process(amp::OpChainContext &opChainContext) {
    amp::BitmapView pipelineVideoFrame = opChainContext.bitmapViews["pipelineVideoFrame"];

    // preprocess
    auto prepocessResult = inference->preprocessImageData(0,
                                                          pipelineVideoFrame.data,
                                                          amp::DataKind::ImageBgraHwc,
                                                          amp::Tdt::Uint8,
                                                          pipelineVideoFrame.width,
                                                          pipelineVideoFrame.height);

    if (!prepocessResult) {
        return prepocessResult;
    }

    // inference
    auto inferenceResult = inference->inference();
    if (!inferenceResult) {
        return inferenceResult;
    }

    inference->prepareForPostprocess(opChainContext.tensorParserInput);

    return {};
}

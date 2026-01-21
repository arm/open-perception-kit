#include "PreprocessInference.h"

#include <fmt/core.h>
#include <memory>
#include <onnxruntime_cxx_api.h>

#include "glib-object.h"
#include "glib.h"
#include "gst/gstpad.h"

using namespace onnx;

PreprocessInference::PreprocessInference() {}

PreprocessInference::~PreprocessInference() {}

amp::Result<void> PreprocessInference::configure(const amp::AttributeMap &attributes) {
    /*    try {
            onnxInference = std::make_shared<onnx::Inference>();

            auto setupResult = onnxInference->setupFromJson("modelPath");
            if (!setupResult) {
                fmt::print("{}\n", setupResult.error().toString());
                AMP_ABORT;
            }
        } catch (const std::exception &e) {
            amp::Error err = AMP_ERROR(amp::ErrorFlag::OnnxStartupException, e.what());
            fmt::print("{}\n", err.toString());
            AMP_ABORT;
        }*/
    return {};
}

amp::Result<void> PreprocessInference::process(amp::OpChainContext &opChainContext) {
    return {};
}

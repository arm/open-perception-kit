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

using namespace onnx;

InferenceOp::InferenceOp() {}

InferenceOp::~InferenceOp() {}

amp::Result<void> InferenceOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

amp::Result<void> InferenceOp::configure(const amp::AttributeMap &attributes) {

    return {};
}

amp::Result<void> InferenceOp::process(amp::OpChainContext &opChainContext) {

    return {};
}

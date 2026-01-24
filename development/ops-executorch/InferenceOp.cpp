#include "InferenceOp.h"
#include "Inference.h"

#include <fmt/core.h>
#include <memory>

using namespace exct;

InferenceOp::InferenceOp() {}
InferenceOp::~InferenceOp() {}

amp::Result<void> InferenceOp::peek(amp::OpChainContext &opChainContext) {
    return {};
}

amp::Result<void> InferenceOp::configure(const amp::AttributeMap &attributes) {

    new Inference();

    return {};
}

amp::Result<void> InferenceOp::process(amp::OpChainContext &opCainContext) {
    return {};
}

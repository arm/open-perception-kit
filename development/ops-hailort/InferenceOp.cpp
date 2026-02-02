#include "InferenceOp.h"

#include <fmt/core.h>
#include <memory>

using namespace hailort;

InferenceOp::InferenceOp() {}
InferenceOp::~InferenceOp() {}

amp::Result<void> InferenceOp::configure(const amp::AttributeMap &attributes) {

    return {};
}

amp::Result<void> InferenceOp::process(amp::OpChainContext &opCainContext) {
    return {};
}

amp::Result<void> InferenceOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

#include "InferenceOp.h"

#include <fmt/core.h>
#include <memory>

using namespace hailort;

InferrenceOp::InferrenceOp() {}
InferrenceOp::~InferrenceOp() {}

amp::Result<void> InferrenceOp::configure(const amp::AttributeMap &attributes) {

    return {};
}

amp::Result<void> InferrenceOp::process(amp::OpChainContext &opCainContext) {
    return {};
}

amp::Result<void> InferrenceOp::peek(amp::OpChainContext &opCainContext) {
    return {};
}

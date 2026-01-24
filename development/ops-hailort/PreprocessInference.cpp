#include "PreprocessInference.h"

#include <fmt/core.h>
#include <memory>

using namespace hailort;

PreprocessInference::PreprocessInference() {}
PreprocessInference::~PreprocessInference() {}

amp::Result<void> PreprocessInference::configure(const amp::AttributeMap &attributes) {

    return {};
}

amp::Result<void> PreprocessInference::process(amp::OpChainContext &opCainContext) {
    return {};
}

amp::Result<void> PreprocessInference::peek(amp::OpChainContext &opCainContext) {
    return {};
}

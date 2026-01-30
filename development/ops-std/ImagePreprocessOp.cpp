#include "ImagePreprocessOp.h"

#include <fmt/core.h>
#include <memory>

#include "amp/TensorView.h"
#include "postproc/PaddleocrParser.h"
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"

using namespace amp;

ImagePreprocessOp::ImagePreprocessOp() {}
ImagePreprocessOp::~ImagePreprocessOp() {}

amp::Result<void> ImagePreprocessOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

amp::Result<void> ImagePreprocessOp::configure(const amp::AttributeMap &attributes) {

    return {};
}

amp::Result<void> ImagePreprocessOp::process(amp::OpChainContext &opChainContext) {

    return {};
}

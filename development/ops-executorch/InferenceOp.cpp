/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceOp.h"
#include "Inference.h"
#include "amp/Result.h"
#include "tl/expected.hpp"

#include <fmt/core.h>
#include <memory>

using namespace exct;

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

        return tl::unexpected(
            AMP_ERROR(amp::ErrorFlag::InvalidOpChain,
                      fmt::format("Missing required attribute in InferenceOp: {}", error.what())));
    }

    try {
        inference = std::make_unique<exct::Inference>();

        auto setupResult = inference->setupFromJson(modelDescPath);
        if (!setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(AMP_ERROR(amp::ErrorFlag::OnnxStartupException,
                                        fmt::format("OnnxRT startup error: {}", e.what())));
    }

    // modelFamily = inference->getModel().modelFamily;

    return {};
}

amp::Result<void> InferenceOp::process(amp::OpChainContext &opCainContext) {
    return {};
}

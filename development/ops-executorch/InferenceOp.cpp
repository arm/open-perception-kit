/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceOp.h"
#include "Inference.h"
#include "pek/Result.h"
#include "tl/expected.hpp"

#include <fmt/core.h>
#include <memory>

using namespace pek::extrch;

InferenceOp::InferenceOp() {}
InferenceOp::~InferenceOp() {}

pek::Result<void> InferenceOp::bind(size_t index, const std::vector<pek::op::Op *> &ops) {
    return {};
}

pek::Result<void> InferenceOp::configure(const pek::AttributeMap &attributes) {
    std::string modelDescPath;

    try {
        modelDescPath = attributes.getString("modelDescriptor");
    } catch (const pek::AttributeError &error) {

        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                      fmt::format("Missing required attribute in InferenceOp: {}", error.what())));
    }

    try {
        inference = std::make_unique<pek::extrch::Inference>();

        auto setupResult = inference->setupFromJson(modelDescPath);
        if (!setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::OnnxStartupException,
                                        fmt::format("OnnxRT startup error: {}", e.what())));
    }

    // modelFamily = inference->getModel().modelFamily;

    return {};
}

pek::Result<pek::op::OpSignal> InferenceOp::process(pek::op::OpChainContext &opCainContext) {
    (void)opCainContext;
    return pek::op::OpSignal::Continue;
}

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

    std::string modelDescPath;

    try {
        modelDescPath = attributes.getString("modelDescriptor");
    } catch (const amp::AttributeError &error) {
        AMP_ABORT; // todo
    }

    try {
        inference = std::make_unique<exct::Inference>();

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

    //    modelFamily = inference->getModel().modelFamily;

    return {};

    //    inference = std::make_unique<Inference>();

    return {};
}

amp::Result<void> InferenceOp::process(amp::OpChainContext &opCainContext) {
    return {};
}

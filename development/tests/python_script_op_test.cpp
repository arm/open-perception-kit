/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <vector>

#include "PythonScriptOp.h"
#include "op/Op.h"
#include "pek/FrameResults.h"

#ifndef PYTHON_SCRIPT_OP_FIXTURES
#define PYTHON_SCRIPT_OP_FIXTURES ""
#endif

namespace {

class FakeInferenceOp final : public pek::op::Op, public pek::op::OpInterfaceInference {
  public:
    FakeInferenceOp() {
        model.outputs = {
            {.name = "scores", .valueType = pek::Dtype::Float32, .shape = pek::Shape(2, 2)},
            {.name = "classes", .valueType = pek::Dtype::Int8, .shape = pek::Shape(2)},
        };
    }

    pek::Result<void> configure(const pek::AttributeMap &) override {
        return {};
    }
    pek::Result<void> bind(size_t, const std::vector<pek::op::Op *> &) override {
        return {};
    }
    pek::Result<pek::op::OpSignal> process(pek::op::OpChainContext &) override {
        return pek::op::OpSignal::Continue;
    }
    const pek::Model &getModel() const override {
        return model;
    }
    uint8_t *getTensorDataAddress(size_t) const override {
        return nullptr;
    }

  private:
    pek::Model model;
};

pek::AttributeMap attributes(std::string_view script) {
    pek::AttributeMap result;
    result.set("script", (std::filesystem::path(PYTHON_SCRIPT_OP_FIXTURES) / script).string());
    return result;
}

} // namespace

TEST(PythonScriptOp, PreservesStateAndExposesReadOnlyTensors) {
    FakeInferenceOp inference;
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&inference, &script};
    ASSERT_TRUE(script.configure(attributes("stateful_tensors.py")));
    ASSERT_TRUE(script.bind(1, ops));

    perception::FrameResults results;
    std::array<float, 4> scores = {1.0F, 2.0F, 3.0F, 4.0F};
    std::array<int8_t, 2> classes = {1, 2};
    pek::op::OpChainContext context;
    context.frameResults = &results;
    context.inferenceOutputTensorCount = 2;
    context.inferenceOutputTensors[0] = pek::TensorView(
        scores.data(), sizeof(scores), pek::Shape(2, 2), pek::Dtype::Float32, 1.0F, 0.0F);
    context.inferenceOutputTensors[1] = pek::TensorView(
        classes.data(), sizeof(classes), pek::Shape(2), pek::Dtype::Int8, 0.5F, 1.0F);

    ASSERT_TRUE(script.process(context));
    scores[0] = 2.0F;
    ASSERT_TRUE(script.process(context));
}

TEST(PythonScriptOp, AllowsPlacementWithoutInferenceOutputs) {
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    perception::FrameResults results;
    pek::op::OpChainContext context;
    context.frameResults = &results;

    EXPECT_TRUE(script.process(context));
}

TEST(PythonScriptOp, ReturnsPythonTracebackAsRuntimeError) {
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("failing.py")));
    ASSERT_TRUE(script.bind(0, ops));

    perception::FrameResults results;
    pek::op::OpChainContext context;
    context.frameResults = &results;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().info.find("intentional Python failure"), std::string::npos);
    EXPECT_NE(result.error().info.find("failing.py"), std::string::npos);
}

TEST(PythonScriptOp, RejectsMissingScript) {
    pek::python::PythonScriptOp script;
    const auto result = script.configure(attributes("missing.py"));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::FileNotFound);
}

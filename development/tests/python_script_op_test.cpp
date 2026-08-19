/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

#include "PythonRuntime.h"
#include "PythonScriptOp.h"
#include "op/Op.h"
#include "pek/FrameResults.h"
#include "perf/PerformanceTracer.h"

#ifndef PYTHON_SCRIPT_OP_FIXTURES
#define PYTHON_SCRIPT_OP_FIXTURES ""
#endif

namespace {

class FakeInferenceOp final : public pek::op::Op, public pek::op::OpInterfaceInference {
  public:
    FakeInferenceOp() {
        model.outputs = {
            {.name = "scores",
             .valueType = pek::Dtype::Float32,
             .shape = pek::Shape(2, 2),
             .quantArguments = {}},
            {.name = "classes",
             .valueType = pek::Dtype::Int8,
             .shape = pek::Shape(2),
             .quantArguments = {}},
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

size_t loadedScriptModuleCount() {
    pek::python::ensureRuntime();
    pek::python::GILGuard gil;
    PyObject *modules = PyImport_GetModuleDict();
    PyObject *key = nullptr;
    PyObject *value = nullptr;
    Py_ssize_t position = 0;
    size_t count = 0;
    while (PyDict_Next(modules, &position, &key, &value) != 0) {
        const char *name = PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : nullptr;
        if (name != nullptr && std::string_view(name).starts_with("_pek_python_script_"))
            ++count;
    }
    return count;
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

TEST(PythonScriptOp, FailedReconfigurationKeepsLoadedScript) {
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    perception::FrameResults results;
    pek::op::OpChainContext context;
    context.frameResults = &results;
    ASSERT_TRUE(script.process(context));

    const auto reconfigure = script.configure(attributes("missing.py"));
    ASSERT_FALSE(reconfigure);
    EXPECT_TRUE(script.process(context));
}

TEST(PythonScriptOp, RemovesLoadedModuleOnDestruction) {
    const auto initialCount = loadedScriptModuleCount();
    {
        pek::python::PythonScriptOp script;
        ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
        EXPECT_EQ(loadedScriptModuleCount(), initialCount + 1U);
    }
    EXPECT_EQ(loadedScriptModuleCount(), initialCount);
}

TEST(PythonScriptOp, RestoresSysPathAfterScriptMutation) {
    perception::FrameResults results;
    pek::op::OpChainContext context;
    context.frameResults = &results;

    pek::python::PythonScriptOp mutatingScript;
    std::vector<pek::op::Op *> mutatingOps = {&mutatingScript};
    ASSERT_TRUE(mutatingScript.configure(attributes("mutate_sys_path.py")));
    ASSERT_TRUE(mutatingScript.bind(0, mutatingOps));
    ASSERT_TRUE(mutatingScript.process(context));

    pek::python::PythonScriptOp checkingScript;
    std::vector<pek::op::Op *> checkingOps = {&checkingScript};
    ASSERT_TRUE(checkingScript.configure(attributes("assert_sys_path_restored.py")));
    ASSERT_TRUE(checkingScript.bind(0, checkingOps));
    EXPECT_TRUE(checkingScript.process(context));
}

TEST(PythonScriptOp, UsesConfiguredContainerRuntime) {
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("runtime_environment.py")));
    ASSERT_TRUE(script.bind(0, ops));

    perception::FrameResults results;
    pek::op::OpChainContext context;
    context.frameResults = &results;

    EXPECT_TRUE(script.process(context));
}

TEST(PythonScriptOp, RecordsWholeOperationTiming) {
    auto *tracer = pek::perf::getGlobalTracer();
    tracer->reset();

    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    perception::FrameResults results;
    pek::op::OpChainContext context;
    context.frameResults = &results;
    context.inferenceInfo.modelName = "test-model";

    ASSERT_TRUE(script.process(context));
    tracer->endCycle();

    const auto stats = tracer->getStats("python/Script/test-model/pek-python-ops-PythonScript-0");
    EXPECT_EQ(stats.count, 1U);
    tracer->reset();
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

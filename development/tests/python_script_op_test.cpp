/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

#include "PythonRuntime.h"
#include "PythonScriptOp.h"
#include "TensorBridge.h"
#include "op/Op.h"
#include "opk/FrameResults.h"
#include "perf/PerformanceMetrics.h"

#ifndef PYTHON_SCRIPT_OP_FIXTURES
#define PYTHON_SCRIPT_OP_FIXTURES ""
#endif

extern "C" void opk_delete_op_instance(void *opInstance);
extern "C" void *opk_create_op_instance(const char *opName);

namespace {

class FakeInferenceOp final : public opk::op::Op, public opk::op::OpInterfaceInference {
  public:
    FakeInferenceOp() {
        model.outputs = {
            {.name = "scores",
             .valueType = opk::Dtype::Float32,
             .shape = opk::Shape(2, 2),
             .quantArguments = {}},
            {.name = "classes",
             .valueType = opk::Dtype::Int8,
             .shape = opk::Shape(2),
             .quantArguments = {}},
        };
    }

    opk::Result<void> configure(const opk::AttributeMap &) override {
        return {};
    }
    opk::Result<void> bind(size_t, const std::vector<opk::op::Op *> &) override {
        return {};
    }
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &) override {
        return opk::op::OpSignal::Continue;
    }
    const opk::Model &getModel() const override {
        return model;
    }
    uint8_t *getTensorDataAddress(size_t) const override {
        return nullptr;
    }

  private:
    opk::Model model;
};

opk::AttributeMap attributes(std::string_view script) {
    opk::AttributeMap result;
    result.set("script", (std::filesystem::path(PYTHON_SCRIPT_OP_FIXTURES) / script).string());
    return result;
}

size_t loadedScriptModuleCount() {
    opk::python::ensureRuntime();
    opk::python::GILGuard gil;
    PyObject *modules = PyImport_GetModuleDict();
    PyObject *key = nullptr;
    PyObject *value = nullptr;
    Py_ssize_t position = 0;
    size_t count = 0;
    while (PyDict_Next(modules, &position, &key, &value) != 0) {
        const char *name = PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : nullptr;
        if (name != nullptr && std::string_view(name).starts_with("_opk_python_script_"))
            ++count;
    }
    return count;
}

} // namespace

TEST(PythonOpsPlugin, CreatesOnlySupportedOperations) {
    EXPECT_EQ(opk_create_op_instance(nullptr), nullptr);
    EXPECT_EQ(opk_create_op_instance("Unsupported"), nullptr);

    void *instance = opk_create_op_instance("PythonScript");
    ASSERT_NE(instance, nullptr);
    opk_delete_op_instance(instance);
}

TEST(PythonRuntime, ReleasesOwnedPythonObjects) {
    opk::python::ensureRuntime();
    opk::python::GILGuard gil;
    opk::python::PyObjectPtr owned(PyLong_FromLong(42));

    PyObject *released = owned.release();

    ASSERT_NE(released, nullptr);
    EXPECT_EQ(PyLong_AsLong(released), 42);
    EXPECT_FALSE(owned);
    Py_DECREF(released);
}

TEST(PythonRuntime, FormatsMissingPythonExceptions) {
    opk::python::ensureRuntime();
    opk::python::GILGuard gil;
    PyErr_Clear();

    EXPECT_EQ(opk::python::formatPythonError(), "Python operation failed without an exception");
}

TEST(TensorBridge, PythonScriptDecoratorPreservesCallableIdentity) {
    opk::python::ensureRuntime();
    opk::python::GILGuard gil;
    opk::python::PyObjectPtr runtimeModule(PyImport_ImportModule("opk_python_ops"));
    ASSERT_TRUE(runtimeModule) << opk::python::formatPythonError();
    opk::python::PyObjectPtr decorator(
        PyObject_GetAttrString(runtimeModule.get(), "python_script"));
    ASSERT_TRUE(decorator) << opk::python::formatPythonError();
    ASSERT_TRUE(PyCallable_Check(decorator.get()));
    opk::python::PyObjectPtr callbackType(
        PyObject_GetAttrString(runtimeModule.get(), "ProcessCallback"));
    ASSERT_TRUE(callbackType) << opk::python::formatPythonError();
    PyObject *callable = PyDict_GetItemString(PyEval_GetBuiltins(), "len");
    ASSERT_NE(callable, nullptr);

    opk::python::PyObjectPtr decorated(PyObject_CallOneArg(decorator.get(), callable));

    ASSERT_TRUE(decorated) << opk::python::formatPythonError();
    EXPECT_EQ(decorated.get(), callable);
}

TEST(TensorBridge, PythonScriptDecoratorRejectsNoncallables) {
    opk::python::ensureRuntime();
    opk::python::GILGuard gil;
    opk::python::PyObjectPtr runtimeModule(PyImport_ImportModule("opk_python_ops"));
    ASSERT_TRUE(runtimeModule) << opk::python::formatPythonError();
    opk::python::PyObjectPtr decorator(
        PyObject_GetAttrString(runtimeModule.get(), "python_script"));
    ASSERT_TRUE(decorator) << opk::python::formatPythonError();

    opk::python::PyObjectPtr decorated(PyObject_CallOneArg(decorator.get(), Py_None));

    EXPECT_FALSE(decorated);
    EXPECT_NE(opk::python::formatPythonError().find("expects a callable"), std::string::npos);
}

TEST(TensorBridge, WrapsAllSupportedAdditionalTensorTypes) {
    opk::python::ensureRuntime();
    opk::python::GILGuard gil;
    opk::python::PyObjectPtr runtimeModule(PyImport_ImportModule("opk_python_ops"));
    ASSERT_TRUE(runtimeModule) << opk::python::formatPythonError();

    std::array<uint8_t, 1> uint8Values = {1};
    std::array<uint16_t, 1> float16Values = {0};
    std::array<int64_t, 1> int64Values = {2};
    opk::op::OpChainContext context;
    context.inferenceOutputTensorCount = 3;
    context.inferenceOutputTensors[0] = opk::TensorView(
        uint8Values.data(), sizeof(uint8Values), opk::Shape(1), opk::Dtype::Uint8, 0.5F, 1.0F);
    context.inferenceOutputTensors[1] = opk::TensorView(float16Values.data(),
                                                        sizeof(float16Values),
                                                        opk::Shape(1),
                                                        opk::Dtype::Float16,
                                                        1.0F,
                                                        0.0F);
    context.inferenceOutputTensors[2] = opk::TensorView(
        int64Values.data(), sizeof(int64Values), opk::Shape(1), opk::Dtype::Int64, 1.0F, 0.0F);

    opk::python::PyObjectPtr tensors(opk::python::wrapTensors(context, nullptr));

    ASSERT_TRUE(tensors) << opk::python::formatPythonError();
    EXPECT_EQ(PyTuple_Size(tensors.get()), 3);
}

TEST(TensorBridge, RejectsNonpositiveRuntimeDimensions) {
    opk::python::ensureRuntime();
    opk::python::GILGuard gil;
    opk::python::PyObjectPtr runtimeModule(PyImport_ImportModule("opk_python_ops"));
    ASSERT_TRUE(runtimeModule) << opk::python::formatPythonError();

    std::array<uint8_t, 1> values = {1};
    opk::Shape invalidShape;
    invalidShape.setFrom(std::vector<int64_t>{0});
    opk::op::OpChainContext context;
    context.inferenceOutputTensorCount = 1;
    context.inferenceOutputTensors[0] =
        opk::TensorView(values.data(), sizeof(values), invalidShape, opk::Dtype::Uint8, 1.0F, 0.0F);

    opk::python::PyObjectPtr tensors(opk::python::wrapTensors(context, nullptr));

    EXPECT_FALSE(tensors);
    EXPECT_NE(opk::python::formatPythonError().find("invalid runtime shape"), std::string::npos);
}

TEST(TensorBridge, RequiresProducerInfoForContext) {
    opk::python::ensureRuntime();
    opk::python::GILGuard gil;
    opk::python::PyObjectPtr runtimeModule(PyImport_ImportModule("opk_python_ops"));
    ASSERT_TRUE(runtimeModule) << opk::python::formatPythonError();

    opk::python::PyObjectPtr context(opk::python::wrapContext(nullptr));

    EXPECT_FALSE(context);
    EXPECT_NE(opk::python::formatPythonError().find("producer_info is required"),
              std::string::npos);
}

TEST(PythonScriptOp, PreservesStateAndExposesReadOnlyTensors) {
    FakeInferenceOp inference;
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&inference, &script};
    ASSERT_TRUE(script.configure(attributes("stateful_tensors.py")));
    ASSERT_TRUE(script.bind(1, ops));

    open_perception_kit::FrameResults results;
    std::array<float, 4> scores = {1.0F, 2.0F, 3.0F, 4.0F};
    std::array<int8_t, 2> classes = {1, 2};
    opk::op::OpChainContext context;
    context.frameResults = &results;
    context.inferenceOutputTensorCount = 2;
    context.inferenceOutputTensors[0] = opk::TensorView(
        scores.data(), sizeof(scores), opk::Shape(2, 2), opk::Dtype::Float32, 1.0F, 0.0F);
    context.inferenceOutputTensors[1] = opk::TensorView(
        classes.data(), sizeof(classes), opk::Shape(2), opk::Dtype::Int8, 0.5F, 1.0F);

    ASSERT_TRUE(script.process(context));
    scores[0] = 2.0F;
    ASSERT_TRUE(script.process(context));
}

TEST(PythonScriptOp, AllowsPlacementWithoutInferenceOutputs) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;

    EXPECT_TRUE(script.process(context));
}

TEST(PythonScriptOp, RejectsIncompatibleProcessSignaturesDuringConfiguration) {
    const auto initialCount = loadedScriptModuleCount();

    opk::python::PythonScriptOp script;
    const auto missingContext = script.configure(attributes("invalid_signature.py"));
    ASSERT_FALSE(missingContext);
    EXPECT_EQ(missingContext.error().flag, opk::ErrorFlag::InvalidData);
    EXPECT_NE(missingContext.error().info.find("must accept three positional arguments"),
              std::string::npos);
    EXPECT_NE(missingContext.error().info.find("context"), std::string::npos);
    EXPECT_EQ(loadedScriptModuleCount(), initialCount);

    const auto keywordOnlyContext =
        script.configure(attributes("invalid_keyword_only_signature.py"));
    ASSERT_FALSE(keywordOnlyContext);
    EXPECT_EQ(keywordOnlyContext.error().flag, opk::ErrorFlag::InvalidData);
    EXPECT_NE(keywordOnlyContext.error().info.find("must accept three positional arguments"),
              std::string::npos);
    EXPECT_EQ(loadedScriptModuleCount(), initialCount);
}

TEST(PythonScriptOp, RejectsSyntaxErrorsDuringConfiguration) {
    opk::python::PythonScriptOp script;
    const auto result = script.configure(attributes("invalid_syntax.txt"));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::ParseError);
    EXPECT_NE(result.error().info.find("Failed to compile"), std::string::npos);
    EXPECT_NE(result.error().info.find("SyntaxError"), std::string::npos);
    EXPECT_NE(result.error().info.find("invalid_syntax.txt"), std::string::npos);
}

TEST(PythonScriptOp, RejectsModuleLevelExceptionsDuringConfiguration) {
    opk::python::PythonScriptOp script;
    const auto result = script.configure(attributes("module_failure.py"));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::ParseError);
    EXPECT_NE(result.error().info.find("Failed to load"), std::string::npos);
    EXPECT_NE(result.error().info.find("intentional module initialization failure"),
              std::string::npos);
    EXPECT_NE(result.error().info.find("module_failure.py"), std::string::npos);
}

TEST(PythonScriptOp, RejectsMissingProcessDuringConfiguration) {
    opk::python::PythonScriptOp script;
    const auto result = script.configure(attributes("missing_process.py"));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::InvalidData);
    EXPECT_NE(result.error().info.find("must define callable process"), std::string::npos);
}

TEST(PythonScriptOp, RejectsMissingPythonPathDuringConfiguration) {
    auto configuration = attributes("empty_tensors.py");
    configuration.setArray(
        "pythonPaths",
        opk::AttributeValue::Array{opk::AttributeValue("missing-python-import-path")});

    opk::python::PythonScriptOp script;
    const auto result = script.configure(configuration);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::FileNotFound);
    EXPECT_NE(result.error().info.find("Python import path does not exist"), std::string::npos);
}

TEST(PythonScriptOp, AcceptsExistingPythonImportPaths) {
    auto configuration = attributes("empty_tensors.py");
    configuration.setArray(
        "pythonPaths", opk::AttributeValue::Array{opk::AttributeValue(PYTHON_SCRIPT_OP_FIXTURES)});

    opk::python::PythonScriptOp script;
    EXPECT_TRUE(script.configure(configuration));
}

TEST(PythonScriptOp, SupportsLazyImportsFromConfiguredPythonPaths) {
    auto configuration = attributes("lazy_import.py");
    configuration.setArray(
        "pythonPaths",
        opk::AttributeValue::Array{opk::AttributeValue(
            (std::filesystem::path(PYTHON_SCRIPT_OP_FIXTURES) / "lazy_import_path").string())});

    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(configuration));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;

    EXPECT_TRUE(script.process(context));
}

TEST(PythonScriptOp, AcceptsCompatibleVariadicProcessSignature) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("variadic_signature.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;

    EXPECT_TRUE(script.process(context));
}

TEST(PythonScriptOp, FailedReconfigurationKeepsLoadedScript) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;
    ASSERT_TRUE(script.process(context));

    const auto reconfigure = script.configure(attributes("invalid_signature.py"));
    ASSERT_FALSE(reconfigure);
    EXPECT_EQ(reconfigure.error().flag, opk::ErrorFlag::InvalidData);
    EXPECT_TRUE(script.process(context));
}

TEST(PythonScriptOp, RemovesLoadedModuleOnDestruction) {
    const auto initialCount = loadedScriptModuleCount();
    {
        opk::python::PythonScriptOp script;
        ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
        EXPECT_EQ(loadedScriptModuleCount(), initialCount + 1U);
    }
    EXPECT_EQ(loadedScriptModuleCount(), initialCount);
}

TEST(PythonScriptOp, RestoresSysPathAfterScriptMutation) {
    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;

    opk::python::PythonScriptOp mutatingScript;
    std::vector<opk::op::Op *> mutatingOps = {&mutatingScript};
    ASSERT_TRUE(mutatingScript.configure(attributes("mutate_sys_path.py")));
    ASSERT_TRUE(mutatingScript.bind(0, mutatingOps));
    ASSERT_TRUE(mutatingScript.process(context));

    opk::python::PythonScriptOp checkingScript;
    std::vector<opk::op::Op *> checkingOps = {&checkingScript};
    ASSERT_TRUE(checkingScript.configure(attributes("assert_sys_path_restored.py")));
    ASSERT_TRUE(checkingScript.bind(0, checkingOps));
    EXPECT_TRUE(checkingScript.process(context));
}

TEST(PythonScriptOp, UsesFreshSysPathForEveryProcessCall) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("mutate_sys_path_repeatedly.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;

    ASSERT_TRUE(script.process(context));
    EXPECT_TRUE(script.process(context));
}

TEST(PythonScriptOp, UsesConfiguredContainerRuntime) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("runtime_environment.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;

    EXPECT_TRUE(script.process(context));
}

TEST(PythonScriptOp, RecordsWholeOperationTiming) {
    auto &metrics = opk::perf::defaultPerformanceMetrics();
    const auto intervalStart = metrics.aggregateSnapshot();

    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;
    context.inferenceInfo.modelName = "test-model";

    ASSERT_TRUE(script.process(context));
    const auto intervalEnd = metrics.aggregateSnapshot();
    const auto intervalMetrics =
        opk::perf::calculateScopeIntervalMetrics(intervalStart, intervalEnd);
    const auto metric = std::ranges::find_if(intervalMetrics, [](const auto &candidate) {
        return candidate.name == "python/Script/test-model/opk-python-ops-PythonScript-0";
    });
    ASSERT_NE(metric, intervalMetrics.end());
    EXPECT_EQ(metric->completedScopeCount, 1U);
}

TEST(PythonScriptOp, ReturnsPythonTracebackAsRuntimeError) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("failing.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().info.find("intentional Python failure"), std::string::npos);
    EXPECT_NE(result.error().info.find("failing.py"), std::string::npos);
}

TEST(PythonScriptOp, RejectsNonNoneProcessResult) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("returns_value.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::InvalidData);
    EXPECT_NE(result.error().info.find("must return None"), std::string::npos);
}

TEST(PythonScriptOp, RequiresFrameResults) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    opk::op::OpChainContext context;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::InvalidData);
    EXPECT_NE(result.error().info.find("requires FrameResults"), std::string::npos);
}

TEST(PythonScriptOp, RejectsProcessingBeforeConfiguration) {
    opk::python::PythonScriptOp script;
    opk::op::OpChainContext context;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::SystemFailure);
    EXPECT_NE(result.error().info.find("is not configured"), std::string::npos);
}

TEST(PythonScriptOp, RejectsTensorCountBeyondContextCapacity) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;
    context.inferenceOutputTensorCount = context.inferenceOutputTensors.size() + 1;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::TensorError);
    EXPECT_NE(result.error().info.find("tensor count exceeds"), std::string::npos);
}

TEST(PythonScriptOp, RejectsInvalidTensorViews) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;
    context.inferenceOutputTensorCount = 1;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::TensorError);
    EXPECT_NE(result.error().info.find("tensor 0 is invalid"), std::string::npos);
}

TEST(PythonScriptOp, RejectsMissingScript) {
    opk::python::PythonScriptOp script;
    const auto result = script.configure(attributes("missing.py"));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::FileNotFound);
}

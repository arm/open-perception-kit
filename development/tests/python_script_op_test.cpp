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
#include "TensorBridge.h"
#include "op/Op.h"
#include "pek/FrameResults.h"
#include "perf/PerformanceTracer.h"

#ifndef PYTHON_SCRIPT_OP_FIXTURES
#define PYTHON_SCRIPT_OP_FIXTURES ""
#endif

extern "C" void pek_delete_op_instance(void *opInstance);
extern "C" void *pek_create_op_instance(const char *opName);

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

TEST(PythonOpsPlugin, CreatesOnlySupportedOperations) {
    EXPECT_EQ(pek_create_op_instance(nullptr), nullptr);
    EXPECT_EQ(pek_create_op_instance("Unsupported"), nullptr);

    void *instance = pek_create_op_instance("PythonScript");
    ASSERT_NE(instance, nullptr);
    pek_delete_op_instance(instance);
}

TEST(PythonRuntime, ReleasesOwnedPythonObjects) {
    pek::python::ensureRuntime();
    pek::python::GILGuard gil;
    pek::python::PyObjectPtr owned(PyLong_FromLong(42));

    PyObject *released = owned.release();

    ASSERT_NE(released, nullptr);
    EXPECT_EQ(PyLong_AsLong(released), 42);
    EXPECT_FALSE(owned);
    Py_DECREF(released);
}

TEST(PythonRuntime, FormatsMissingPythonExceptions) {
    pek::python::ensureRuntime();
    pek::python::GILGuard gil;
    PyErr_Clear();

    EXPECT_EQ(pek::python::formatPythonError(), "Python operation failed without an exception");
}

TEST(TensorBridge, PythonScriptDecoratorPreservesCallableIdentity) {
    pek::python::ensureRuntime();
    pek::python::GILGuard gil;
    pek::python::PyObjectPtr module(PyImport_ImportModule("pek_python_ops"));
    ASSERT_TRUE(module) << pek::python::formatPythonError();
    pek::python::PyObjectPtr decorator(PyObject_GetAttrString(module.get(), "python_script"));
    ASSERT_TRUE(decorator) << pek::python::formatPythonError();
    ASSERT_TRUE(PyCallable_Check(decorator.get()));
    pek::python::PyObjectPtr callbackType(PyObject_GetAttrString(module.get(), "ProcessCallback"));
    ASSERT_TRUE(callbackType) << pek::python::formatPythonError();
    PyObject *callable = PyDict_GetItemString(PyEval_GetBuiltins(), "len");
    ASSERT_NE(callable, nullptr);

    pek::python::PyObjectPtr decorated(PyObject_CallOneArg(decorator.get(), callable));

    ASSERT_TRUE(decorated) << pek::python::formatPythonError();
    EXPECT_EQ(decorated.get(), callable);
}

TEST(TensorBridge, PythonScriptDecoratorRejectsNoncallables) {
    pek::python::ensureRuntime();
    pek::python::GILGuard gil;
    pek::python::PyObjectPtr module(PyImport_ImportModule("pek_python_ops"));
    ASSERT_TRUE(module) << pek::python::formatPythonError();
    pek::python::PyObjectPtr decorator(PyObject_GetAttrString(module.get(), "python_script"));
    ASSERT_TRUE(decorator) << pek::python::formatPythonError();

    pek::python::PyObjectPtr decorated(PyObject_CallOneArg(decorator.get(), Py_None));

    EXPECT_FALSE(decorated);
    EXPECT_NE(pek::python::formatPythonError().find("expects a callable"), std::string::npos);
}

TEST(TensorBridge, WrapsAllSupportedAdditionalTensorTypes) {
    pek::python::ensureRuntime();
    pek::python::GILGuard gil;
    pek::python::PyObjectPtr module(PyImport_ImportModule("pek_python_ops"));
    ASSERT_TRUE(module) << pek::python::formatPythonError();

    std::array<uint8_t, 1> uint8Values = {1};
    std::array<uint16_t, 1> float16Values = {0};
    std::array<int64_t, 1> int64Values = {2};
    pek::op::OpChainContext context;
    context.inferenceOutputTensorCount = 3;
    context.inferenceOutputTensors[0] = pek::TensorView(
        uint8Values.data(), sizeof(uint8Values), pek::Shape(1), pek::Dtype::Uint8, 0.5F, 1.0F);
    context.inferenceOutputTensors[1] = pek::TensorView(float16Values.data(),
                                                        sizeof(float16Values),
                                                        pek::Shape(1),
                                                        pek::Dtype::Float16,
                                                        1.0F,
                                                        0.0F);
    context.inferenceOutputTensors[2] = pek::TensorView(
        int64Values.data(), sizeof(int64Values), pek::Shape(1), pek::Dtype::Int64, 1.0F, 0.0F);

    pek::python::PyObjectPtr tensors(pek::python::wrapTensors(context, nullptr));

    ASSERT_TRUE(tensors) << pek::python::formatPythonError();
    EXPECT_EQ(PyTuple_Size(tensors.get()), 3);
}

TEST(TensorBridge, RejectsNonpositiveRuntimeDimensions) {
    pek::python::ensureRuntime();
    pek::python::GILGuard gil;
    pek::python::PyObjectPtr module(PyImport_ImportModule("pek_python_ops"));
    ASSERT_TRUE(module) << pek::python::formatPythonError();

    std::array<uint8_t, 1> values = {1};
    pek::Shape invalidShape;
    invalidShape.setFrom(std::vector<int64_t>{0});
    pek::op::OpChainContext context;
    context.inferenceOutputTensorCount = 1;
    context.inferenceOutputTensors[0] =
        pek::TensorView(values.data(), sizeof(values), invalidShape, pek::Dtype::Uint8, 1.0F, 0.0F);

    pek::python::PyObjectPtr tensors(pek::python::wrapTensors(context, nullptr));

    EXPECT_FALSE(tensors);
    EXPECT_NE(pek::python::formatPythonError().find("invalid runtime shape"), std::string::npos);
}

TEST(TensorBridge, RequiresProducerInfoForContext) {
    pek::python::ensureRuntime();
    pek::python::GILGuard gil;
    pek::python::PyObjectPtr module(PyImport_ImportModule("pek_python_ops"));
    ASSERT_TRUE(module) << pek::python::formatPythonError();

    pek::python::PyObjectPtr context(pek::python::wrapContext(nullptr));

    EXPECT_FALSE(context);
    EXPECT_NE(pek::python::formatPythonError().find("producer_info is required"),
              std::string::npos);
}

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

TEST(PythonScriptOp, RejectsIncompatibleProcessSignaturesDuringConfiguration) {
    const auto initialCount = loadedScriptModuleCount();

    pek::python::PythonScriptOp script;
    const auto missingContext = script.configure(attributes("invalid_signature.py"));
    ASSERT_FALSE(missingContext);
    EXPECT_EQ(missingContext.error().flag, pek::ErrorFlag::InvalidData);
    EXPECT_NE(missingContext.error().info.find("must accept three positional arguments"),
              std::string::npos);
    EXPECT_NE(missingContext.error().info.find("context"), std::string::npos);
    EXPECT_EQ(loadedScriptModuleCount(), initialCount);

    const auto keywordOnlyContext =
        script.configure(attributes("invalid_keyword_only_signature.py"));
    ASSERT_FALSE(keywordOnlyContext);
    EXPECT_EQ(keywordOnlyContext.error().flag, pek::ErrorFlag::InvalidData);
    EXPECT_NE(keywordOnlyContext.error().info.find("must accept three positional arguments"),
              std::string::npos);
    EXPECT_EQ(loadedScriptModuleCount(), initialCount);
}

TEST(PythonScriptOp, RejectsSyntaxErrorsDuringConfiguration) {
    pek::python::PythonScriptOp script;
    const auto result = script.configure(attributes("invalid_syntax.txt"));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::ParseError);
    EXPECT_NE(result.error().info.find("Failed to compile"), std::string::npos);
    EXPECT_NE(result.error().info.find("SyntaxError"), std::string::npos);
    EXPECT_NE(result.error().info.find("invalid_syntax.txt"), std::string::npos);
}

TEST(PythonScriptOp, RejectsModuleLevelExceptionsDuringConfiguration) {
    pek::python::PythonScriptOp script;
    const auto result = script.configure(attributes("module_failure.py"));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::ParseError);
    EXPECT_NE(result.error().info.find("Failed to load"), std::string::npos);
    EXPECT_NE(result.error().info.find("intentional module initialization failure"),
              std::string::npos);
    EXPECT_NE(result.error().info.find("module_failure.py"), std::string::npos);
}

TEST(PythonScriptOp, RejectsMissingProcessDuringConfiguration) {
    pek::python::PythonScriptOp script;
    const auto result = script.configure(attributes("missing_process.py"));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::InvalidData);
    EXPECT_NE(result.error().info.find("must define callable process"), std::string::npos);
}

TEST(PythonScriptOp, RejectsMissingPythonPathDuringConfiguration) {
    auto configuration = attributes("empty_tensors.py");
    configuration.setArray(
        "pythonPaths",
        pek::AttributeValue::Array{pek::AttributeValue("missing-python-import-path")});

    pek::python::PythonScriptOp script;
    const auto result = script.configure(configuration);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::FileNotFound);
    EXPECT_NE(result.error().info.find("Python import path does not exist"), std::string::npos);
}

TEST(PythonScriptOp, AcceptsExistingPythonImportPaths) {
    auto configuration = attributes("empty_tensors.py");
    configuration.setArray(
        "pythonPaths", pek::AttributeValue::Array{pek::AttributeValue(PYTHON_SCRIPT_OP_FIXTURES)});

    pek::python::PythonScriptOp script;
    EXPECT_TRUE(script.configure(configuration));
}

TEST(PythonScriptOp, AcceptsCompatibleVariadicProcessSignature) {
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("variadic_signature.py")));
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

    const auto reconfigure = script.configure(attributes("invalid_signature.py"));
    ASSERT_FALSE(reconfigure);
    EXPECT_EQ(reconfigure.error().flag, pek::ErrorFlag::InvalidData);
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

TEST(PythonScriptOp, RejectsNonNoneProcessResult) {
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("returns_value.py")));
    ASSERT_TRUE(script.bind(0, ops));

    perception::FrameResults results;
    pek::op::OpChainContext context;
    context.frameResults = &results;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::InvalidData);
    EXPECT_NE(result.error().info.find("must return None"), std::string::npos);
}

TEST(PythonScriptOp, RequiresFrameResults) {
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    pek::op::OpChainContext context;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::InvalidData);
    EXPECT_NE(result.error().info.find("requires FrameResults"), std::string::npos);
}

TEST(PythonScriptOp, RejectsProcessingBeforeConfiguration) {
    pek::python::PythonScriptOp script;
    pek::op::OpChainContext context;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::SystemFailure);
    EXPECT_NE(result.error().info.find("is not configured"), std::string::npos);
}

TEST(PythonScriptOp, RejectsTensorCountBeyondContextCapacity) {
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    perception::FrameResults results;
    pek::op::OpChainContext context;
    context.frameResults = &results;
    context.inferenceOutputTensorCount = context.inferenceOutputTensors.size() + 1;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::TensorError);
    EXPECT_NE(result.error().info.find("tensor count exceeds"), std::string::npos);
}

TEST(PythonScriptOp, RejectsInvalidTensorViews) {
    pek::python::PythonScriptOp script;
    std::vector<pek::op::Op *> ops = {&script};
    ASSERT_TRUE(script.configure(attributes("empty_tensors.py")));
    ASSERT_TRUE(script.bind(0, ops));

    perception::FrameResults results;
    pek::op::OpChainContext context;
    context.frameResults = &results;
    context.inferenceOutputTensorCount = 1;
    const auto result = script.process(context);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::TensorError);
    EXPECT_NE(result.error().info.find("tensor 0 is invalid"), std::string::npos);
}

TEST(PythonScriptOp, RejectsMissingScript) {
    pek::python::PythonScriptOp script;
    const auto result = script.configure(attributes("missing.py"));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::FileNotFound);
}

/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <utility>
#include <vector>

#include "op/OpRef.h"
#include "pek/Model.h"
#include "pek/ModelDescriptor.h"
#include "pek/Tools.h"

namespace {

pek::Model makeRuntimeModel() {
    pek::Model model;
    model.inputs.emplace_back();
    model.inputs.back().valueType = pek::Dtype::Float32;
    model.inputs.back().shape = pek::Shape(1, 3, 4, 4);
    model.outputs.emplace_back();
    model.outputs.back().valueType = pek::Dtype::Float32;
    model.outputs.back().shape = pek::Shape(1, 2);
    return model;
}

pek::ModelDescriptor makeModelDescriptor() {
    pek::ModelDescriptor descriptor;
    descriptor.inputTensors.emplace_back();
    descriptor.inputTensors.back().shape = pek::Shape(1, 3, 4, 4);
    descriptor.inputTensors.back().dataKind = pek::DataKind::ImageRgbChw;
    descriptor.outputTensors.emplace_back();
    descriptor.outputTensors.back().shape = pek::Shape(1, 2);
    descriptor.outputTensors.back().dataKind = pek::DataKind::RawTensorData;
    return descriptor;
}

void expectDescriptorRejected(pek::Model model, const pek::ModelDescriptor &descriptor) {
    EXPECT_FALSE(model.applyModelFromDescriptor(descriptor).has_value());
}

pek::Shape dynamicShape(std::initializer_list<int64_t> dimensions) {
    pek::Shape shape;
    const std::vector<int64_t> values(dimensions);
    const bool accepted = shape.setFrom(values);
    EXPECT_TRUE(accepted);
    return shape;
}

} // namespace

TEST(ModelDescriptor, ResolvesModelFileRelativeToDescriptor) {
    const auto directory = std::filesystem::path("pek_model_descriptor_test") / "nested";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "model-variant.json";
    std::ofstream(descriptorPath) << R"({
        "version": 1,
        "name": "test",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })";

    auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->modelFile, (directory / "model.onnx").string());
    std::filesystem::remove_all(directory.parent_path());
}

TEST(ModelDescriptor, PreservesAbsoluteModelFileFilesystemMeaning) {
    const auto directory = std::filesystem::path("pek_model_descriptor_test") / "absolute";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto targetDirectory = directory.parent_path() / "absolute-target";
    std::filesystem::create_directories(targetDirectory / "child");
    std::filesystem::create_directory_symlink(std::filesystem::absolute(targetDirectory / "child"),
                                              directory / "linked");
    std::ofstream(targetDirectory / "model.onnx") << "target";
    std::ofstream(directory / "model.onnx") << "lexically-normalized";
    const auto descriptorPath = directory / "model.json";
    const auto modelPath = std::filesystem::absolute(directory) / "linked/../model.onnx";
    std::ofstream(descriptorPath) << nlohmann::json{
        {"version", 1},
        {"name", "test"},
        {"modelFile", modelPath.string()},
        {"dynamicOutput", true},
        {"inputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->modelFile, modelPath.string());
    std::string marker;
    std::ifstream(descriptor->modelFile) >> marker;
    EXPECT_EQ(marker, "target");
    std::filesystem::remove_all(directory.parent_path());
}

TEST(ModelDescriptor, RejectsUriModelFile) {
    const auto descriptor = pek::ModelDescriptor::fromJson(R"({
        "version": 1,
        "name": "test",
        "modelFile": "hf:Arm/example@revision#file=model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })");

    ASSERT_FALSE(descriptor.has_value());
    EXPECT_NE(descriptor.error().info.find("schema.validation"), std::string::npos);
}

TEST(ModelDescriptor, RejectsOpChainFilename) {
    const auto directory = std::filesystem::path("pek_model_descriptor_test") / "wrong-name";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    std::ofstream(descriptorPath) << R"({
        "version": 1,
        "name": "test",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })";

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_FALSE(descriptor.has_value());
    EXPECT_NE(descriptor.error().info.find("dispatch.filename"), std::string::npos);
    std::filesystem::remove_all(directory.parent_path());
}

TEST(ModelDescriptor, ParsesTensorFeedbackMode) {
    const auto feedback =
        nlohmann::json{
            {"mode", "Copy"},
            {"fromOutputTensorIndex", 1},
            {"toInputTensorIndex", 2},
        }
            .get<pek::TensorFeedback>();

    EXPECT_EQ(feedback.mode, pek::TensorFeedback::Mode::Copy);
    EXPECT_THROW((nlohmann::json{
                     {"mode", "Unknown"},
                     {"fromOutputTensorIndex", 1},
                     {"toInputTensorIndex", 2},
                 }
                      .get<pek::TensorFeedback>()),
                 nlohmann::json::type_error);
}

TEST(Model, RejectsIncompatibleDescriptor) {
    {
        auto model = makeRuntimeModel();
        model.inputs.clear();
        expectDescriptorRejected(std::move(model), makeModelDescriptor());
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.inputTensors[0].dataKind = pek::DataKind::Unknown;
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.inputTensors[0].dataKind = pek::DataKind::Value;
        descriptor.inputTensors[0].shape = pek::Shape(1);
        descriptor.inputTensors[0].valueInputs = {1.0f};
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.inputTensors[0].dataKind = pek::DataKind::Value;
        descriptor.inputTensors[0].shape = pek::Shape();
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.inputTensors[0].shape = pek::Shape();
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.inputTensors[0].shape = dynamicShape({1, -1, 4, 4});
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto model = makeRuntimeModel();
        model.inputs[0].shape = dynamicShape({1, -1, 4, 4});
        auto descriptor = makeModelDescriptor();
        descriptor.inputTensors[0].shape = pek::Shape(2, 3, 4, 4);
        expectDescriptorRejected(std::move(model), descriptor);
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.inputTensors[0].shape = pek::Shape(2, 3, 4, 4);
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto model = makeRuntimeModel();
        model.outputs.clear();
        expectDescriptorRejected(std::move(model), makeModelDescriptor());
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.dynamicOutput = true;
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.outputTensors[0].dataKind = pek::DataKind::Unknown;
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.outputTensors[0].shape = dynamicShape({1, -1});
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto model = makeRuntimeModel();
        model.outputs[0].shape = dynamicShape({1, -1});
        auto descriptor = makeModelDescriptor();
        descriptor.outputTensors[0].shape = pek::Shape(2, 2);
        expectDescriptorRejected(std::move(model), descriptor);
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.outputTensors[0].shape = pek::Shape(2, 2);
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
    {
        auto descriptor = makeModelDescriptor();
        descriptor.inputTensors[0].matchShapeOutputIndex = 0;
        expectDescriptorRejected(makeRuntimeModel(), descriptor);
    }
}

TEST(Tools, PropagatesLoadAndSymbolErrors) {
    std::size_t width = 0;
    std::size_t height = 0;
    EXPECT_FALSE(pek::Tools::loadImageFile("missing-image.bmp", width, height).has_value());
    EXPECT_FALSE(pek::Tools::loadImageFile("missing-image.png", width, height).has_value());
    EXPECT_FALSE(pek::Tools::DynamicLibraryOpen("lib-pek-does-not-exist").has_value());
    EXPECT_FALSE(pek::Tools::DynamicLibraryGetSymbolRaw(nullptr, "missing").has_value());

    auto library = pek::Tools::DynamicLibraryOpen("libc.so.6");
    ASSERT_TRUE(library.has_value()) << library.error().toString();
    EXPECT_FALSE(
        pek::Tools::DynamicLibraryGetSymbolRaw(*library, "pek_missing_symbol").has_value());
    pek::Tools::DynamicLibraryClose(*library);

    using MissingFunction = void (*)();
    EXPECT_FALSE(
        pek::Tools::DynamicLibraryGetSymbol<MissingFunction>(nullptr, "missing").has_value());
}

TEST(OpRef, RejectsMissingLibrary) {
    pek::op::OpRef operation;
    EXPECT_FALSE(operation.bind("lib-pek-does-not-exist", "missing").has_value());
}

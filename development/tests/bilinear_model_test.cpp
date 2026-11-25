/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include "Inference.h"
#include "mediaio/PixelBufferVideoFrame.h"
#include "op/OpChain.h"
#include "opk/BilinearImageTensor.h"
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

namespace {
using namespace open_perception_kit::metadata;
namespace fs = std::filesystem;

std::vector<uint8_t> bytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class BilinearModel : public testing::Test {
  protected:
    fs::path assets;
    nlohmann::json oracle;
    void SetUp() override {
        const char *const root = std::getenv("OPK_QUERY_TEST_ASSETS");
        if (!root) {
            GTEST_SKIP() << "Set OPK_QUERY_TEST_ASSETS to qualified real-model oracle fixtures";
        }
        assets = root;
        std::ifstream input(assets / "expected.json");
        ASSERT_TRUE(input.good());
        input >> oracle;
    }
};

} // namespace

TEST_F(BilinearModel, LinearPixelsMatchAuthorAcrossOddAspectRatiosAndChannelLayouts) {
    for (const auto &fixture : oracle["pixel_cases"]) {
        const size_t width = fixture["width"];
        const size_t height = fixture["height"];
        const std::string stem = "pixels-" + std::to_string(fixture["index"].get<int>());
        const auto expected = bytes(assets / (stem + ".f32"));
        for (const bool bgra : {false, true}) {
            const auto pixels = bytes(assets / (stem + (bgra ? ".bgra" : ".rgb")));
            opk::ModelInput input;
            input.shape = opk::Shape(1, 3, 640, 640);
            input.dataKind = opk::DataKind::ImageRgbChw;
            input.keepAspectRatio = true;
            const auto actual = opk::buildBilinearImageTensor(pixels,
                                                              width,
                                                              height,
                                                              width * (bgra ? 4 : 3),
                                                              bgra ? opk::RawImagePixelFormat::Bgra
                                                                   : opk::RawImagePixelFormat::Rgb,
                                                              input);
            ASSERT_TRUE(actual) << actual.error().info;
            ASSERT_EQ(expected.size(), actual->pixels.size() * sizeof(float));
            EXPECT_EQ(std::memcmp(expected.data(), actual->pixels.data(), expected.size()), 0)
                << stem << " bgra=" << bgra;
        }
    }
}

TEST_F(BilinearModel, FixedShapeYolo26ExportReusesGenericBilinearPreprocessing) {
    std::ifstream receiptFile(assets / "second-model.json");
    if (!receiptFile) {
        GTEST_SKIP() << "Generate the optional second-model qualification fixtures";
    }
    const auto receipt = nlohmann::json::parse(receiptFile);
    const auto modelPath =
        fs::path(receipt["artifact"].get<std::string>()).parent_path() / "model.json";
    opk::op::OpRef inference, image;
    ASSERT_TRUE(inference.bind(OPK_QUERY_ONNX_OPS, "Inference"));
    ASSERT_TRUE(image.bind(OPK_QUERY_STD_OPS, "GenericImagePreprocess"));
    inference->instanceId = "second-model";
    opk::AttributeMap inferenceAttributes, imageAttributes;
    inferenceAttributes.set("modelDescriptor", modelPath.string());
    inferenceAttributes.set("intraOpNumThreads", int64_t{2});
    inferenceAttributes.set("interOpNumThreads", int64_t{1});
    ASSERT_TRUE(inference->configure(inferenceAttributes));
    imageAttributes.set("sampling", "bilinear");
    imageAttributes.set("inferenceInstance", "second-model");
    ASSERT_TRUE(image->configure(imageAttributes));
    ASSERT_TRUE(image->bind(0, {image.operator->(), inference.operator->()}));
    const auto *const backend = inference->as<opk::op::OpInterfaceInference>();
    ASSERT_NE(backend, nullptr);
    ASSERT_EQ(backend->getModel().inputs[0].shape, opk::Shape(1, 3, 320, 320));
    const size_t width = receipt["source_width"], height = receipt["source_height"];
    opk::op::OpChainContext context;
    context.videoFrames["pipelineVideoFrame"] = opk::mediaio::PixelBufferVideoFrame::take(
        bytes(assets / "second-model.bgra"), width, height, opk::RawImagePixelFormat::Bgra);
    context.inferenceImageCrops.push_back({0, 0, width, height});
    context.inferenceImageCropIds.push_back(0);
    ASSERT_TRUE(image->process(context));
    const auto expected = bytes(assets / "second-model-input.f32");
    ASSERT_EQ(expected.size(), 3 * 320 * 320 * sizeof(float));
    EXPECT_EQ(std::memcmp(expected.data(), backend->getTensorDataAddress(0), expected.size()), 0);
    EXPECT_EQ(context.inferenceInfo.image.letterboxLeft, receipt["left"]);
    EXPECT_EQ(context.inferenceInfo.image.letterboxTop, receipt["top"]);
    ASSERT_TRUE(inference->process(context));
    const auto output = bytes(assets / "second-model-output.f32");
    const auto &view = context.inferenceOutputTensors[0];
    ASSERT_EQ(output.size(), view.getShape().getFullValueCount() * sizeof(float));
    const auto *const actual = reinterpret_cast<const float *>(view.getData());
    for (size_t i = 0; i < output.size() / sizeof(float); ++i) {
        float expectedValue;
        std::memcpy(&expectedValue, output.data() + i * sizeof(float), sizeof(float));
        ASSERT_NEAR(actual[i], expectedValue, 1e-4);
    }
}

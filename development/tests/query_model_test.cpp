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
#include "YoloePreparedPrompt.h"
#include "YoloePromptPrepareOp.h"
#include "mediaio/PixelBufferVideoFrame.h"
#include "op/OpChain.h"
#include "opk/BilinearImageTensor.h"
#include "opk/QueryPromptInputs.h"
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

// A compatible provider deliberately independent of the production preparer and its counter.
class SubstituteQueryProvider final : public opk::op::Op,
                                      public opk::PreparedPromptProvider,
                                      public opk::op::OpInterfacePromptConsumer {
  public:
    explicit SubstituteQueryProvider(std::shared_ptr<const opk::PreparedPrompt> value)
        : prepared(std::move(value)) {}

    opk::Result<void> configure(const opk::AttributeMap &) override {
        return {};
    }
    opk::Result<void> bind(const size_t, const std::vector<opk::op::Op *> &) override {
        return {};
    }
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &) override {
        return opk::op::OpSignal::Continue;
    }
    std::shared_ptr<const opk::PreparedPrompt>
    preparedFor(const std::shared_ptr<const opk::PromptSnapshot> &snapshot,
                const opk::PreparationContract contract) const override {
        return prepared->snapshot == snapshot && prepared->contract == contract ? prepared
                                                                                : nullptr;
    }
    bool acceptsPrompt(const opk::PromptFormat format,
                       const std::string_view inference,
                       const std::string_view source) const override {
        return accepts && format == prepared->snapshot->payload->format() && inference == "yoloe" &&
               source == "pipelineVideoFrame";
    }

    bool accepts = true;

  private:
    const std::shared_ptr<const opk::PreparedPrompt> prepared;
};

class QueryModel : public testing::Test {
  protected:
    fs::path assets;
    nlohmann::json oracle;
    void runQueries(const bool visual);
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

TEST_F(QueryModel, PreparationRejectsIncompatibleEncoderGraphs) {
    for (const bool visual : {false, true}) {
        opk::AttributeMap attributes;
        attributes.set("mode", visual ? "visual" : "text");
        attributes.set("inferenceInstance", "yoloe");
        attributes.set(
            "modelDescriptor",
            (assets / (visual ? "model-text-encoder.json" : "model-visual-encoder.json")).string());
        opk::python::YoloePromptPrepareOp preparation;
        const auto configured = preparation.configure(attributes);
        ASSERT_FALSE(configured);
        EXPECT_EQ(configured.error().flag, opk::ErrorFlag::InvalidData);
        EXPECT_NE(configured.error().info.find(visual ? "Visual encoder requires"
                                                      : "Text encoder does not expose"),
                  std::string::npos);
    }
}

std::shared_ptr<const opk::PromptSnapshot> textPrompt(const std::vector<std::string> &names,
                                                      const uint64_t revision) {
    TextPromptInputsT wire;
    wire.request = std::make_unique<RequestEnvelopeT>();
    wire.request->request_id = UINT64_MAX;
    wire.request->revision = revision;
    wire.request->destination = std::make_unique<RequestDestinationT>();
    wire.request->destination->inference_instance = "yoloe";
    wire.request->destination->source_name = "pipelineVideoFrame";
    wire.payload = std::make_unique<TextPromptPayloadT>();
    for (const auto &name : names) {
        auto group = std::make_unique<TextPromptGroupT>();
        group->group_id = name.size() <= 64 ? "group-" + name : "group-overflow";
        group->description = name;
        wire.payload->groups.push_back(std::move(group));
    }
    flatbuffers::FlatBufferBuilder builder;
    FinishTextPromptInputsBuffer(builder, TextPromptInputs::Pack(builder, &wire));
    uint64_t id = 0;
    uint64_t decodedRevision = 0;
    auto decoded = opk::decodeTextPrompt({builder.GetBufferPointer(), builder.GetSize()},
                                         "native-session",
                                         "yoloe",
                                         "pipelineVideoFrame",
                                         id,
                                         decodedRevision);
    if (!decoded) {
        return nullptr;
    }
    return std::make_shared<const opk::PromptSnapshot>(std::move(*decoded));
}

std::shared_ptr<const opk::PromptSnapshot> visualPrompt(const fs::path &assets,
                                                        const nlohmann::json &references,
                                                        const nlohmann::json &queries,
                                                        const uint64_t revision) {
    VisualPromptInputsT wire;
    wire.request = std::make_unique<RequestEnvelopeT>();
    wire.request->request_id = UINT64_MAX;
    wire.request->revision = revision;
    wire.request->destination = std::make_unique<RequestDestinationT>();
    wire.request->destination->inference_instance = "yoloe";
    wire.request->destination->source_name = "pipelineVideoFrame";
    wire.payload = std::make_unique<VisualPromptPayloadT>();
    for (const auto &reference : references) {
        auto image = std::make_unique<PromptReferenceImageT>();
        image->width = reference["width"];
        image->height = reference["height"];
        image->encoding = PromptReferenceEncoding::RGB8;
        image->pixels = bytes(assets / reference["file"].get<std::string>());
        wire.payload->references.push_back(std::move(image));
    }
    for (const auto &query : queries) {
        auto group = std::make_unique<VisualPromptGroupT>();
        group->group_id = "group-" + query[0].get<std::string>();
        group->reference_index = query[1];
        group->box = std::make_unique<VisualPromptBoxT>();
        group->box->left = query[2][0];
        group->box->top = query[2][1];
        group->box->right = query[2][2];
        group->box->bottom = query[2][3];
        wire.payload->groups.push_back(std::move(group));
    }
    flatbuffers::FlatBufferBuilder builder;
    FinishVisualPromptInputsBuffer(builder, VisualPromptInputs::Pack(builder, &wire));
    uint64_t id = 0;
    uint64_t decodedRevision = 0;
    auto decoded = opk::decodeVisualPrompt({builder.GetBufferPointer(), builder.GetSize()},
                                           "native-session",
                                           "yoloe",
                                           "pipelineVideoFrame",
                                           id,
                                           decodedRevision);
    if (!decoded) {
        return nullptr;
    }
    // Both builder bytes and authored nested image storage disappear on return.
    return std::make_shared<const opk::PromptSnapshot>(std::move(*decoded));
}

TEST_F(QueryModel, ActualTextEncoderDetectorParserAndRevisionCache) {
    runQueries(false);
}

TEST_F(QueryModel, ActualVisualEncoderCrossImageGroupsAndReferenceOwnership) {
    runQueries(true);
}

void QueryModel::runQueries(const bool visual) {
    if (visual) {
        std::ifstream input(assets / "expected-visual.json");
        ASSERT_TRUE(input.good());
        input >> oracle;
    }
    const auto descriptor = opk::op::OpChainDescriptor::fromFile(
        (assets / (visual ? "opchain-visual.json" : "opchain.json")).string());
    ASSERT_TRUE(descriptor) << descriptor.error().info;
    opk::op::OpChain chain;
    const opk::PreparedPromptProvider *provider = nullptr;
    const opk::python::YoloePromptPrepareOp *encoder = nullptr;
    opk::op::Op *parser = nullptr;
    std::vector<opk::op::Op *> operations;
    for (const auto &entry : descriptor->ops) {
        const auto slash = entry.id.find('/');
        const auto library = entry.id.substr(0, slash);
        const auto operation = entry.id.substr(slash + 1);
        const fs::path path = library == "opk-python-ops" ? OPK_QUERY_PYTHON_OPS
                              : library == "opk-onnx-ops" ? OPK_QUERY_ONNX_OPS
                                                          : OPK_QUERY_STD_OPS;
        opk::op::OpRef reference;
        const auto loaded = reference.bind(path.string(), operation);
        ASSERT_TRUE(loaded) << loaded.error().info;
        reference->instanceId = entry.instanceId;
        reference->libName = library;
        reference->opName = operation;
        const auto configured = reference->configure(entry.attributes);
        ASSERT_TRUE(configured) << configured.error().info;
        EXPECT_TRUE(reference->supportsStageRole(entry.role));
        if (operation == "YoloePromptPrepare") {
            EXPECT_FALSE(reference->supportsStageRole(opk::op::StageRole::ImagePreprocess));
            EXPECT_FALSE(reference->supportsStageRole(opk::op::StageRole::Postprocess));
        }
        if (const auto *const preparation = reference->as<opk::PreparedPromptProvider>()) {
            provider = preparation;
            encoder = reference->as<opk::python::YoloePromptPrepareOp>();
        }
        if (operation == "YoloeInstancePostprocess") {
            parser = reference.operator->();
        }
        operations.push_back(reference.operator->());
        chain.add(reference);
    }
    const auto binding = chain.bind();
    ASSERT_TRUE(binding) << binding.error().info;
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(encoder, nullptr);
    ASSERT_NE(parser, nullptr);
    const auto capabilities = chain.promptCapabilities("yoloe", "pipelineVideoFrame");
    ASSERT_EQ(capabilities.size(), 1);
    EXPECT_EQ(capabilities[0].limits.maxGroups, opk::MaxQueryPromptGroups);
    EXPECT_TRUE(chain.promptCapabilities("other", "pipelineVideoFrame").empty());
    auto conflictingWriters = operations;
    conflictingWriters.insert(conflictingWriters.begin() + 3, operations[3]);
    EXPECT_FALSE(operations[2]->bind(2, conflictingWriters));
    ASSERT_TRUE(chain.bind());
    const uint32_t width = oracle["width"];
    const uint32_t height = oracle["height"];
    std::shared_ptr<opk::mediaio::VideoFrame> frame = opk::mediaio::PixelBufferVideoFrame::take(
        bytes(assets / (visual ? "visual-target.bgra" : "target.bgra")),
        width,
        height,
        opk::RawImagePixelFormat::Bgra);
    ASSERT_TRUE(frame);
    opk::op::OpChainContext context;
    context.videoFrames["pipelineVideoFrame"] = frame;
    context.inferenceInfo.inferElementId = "native";
    std::shared_ptr<const opk::PromptSnapshot> first;
    size_t encodings = 0;
    for (size_t index = 0; index < oracle["cases"].size(); ++index) {
        const auto &fixture = oracle["cases"][index];
        const auto prompt =
            visual ? visualPrompt(assets, oracle["references"], fixture["queries"], index + 1)
                   : textPrompt(fixture["names"].get<std::vector<std::string>>(), index + 1);
        ASSERT_TRUE(prompt);
        if (!first) {
            first = prompt;
        }
        context.prompt = prompt;
        for (int repeat = 0; repeat < 2; ++repeat) {
            open_perception_kit::FrameResults results;
            context.frameResults = &results;
            const auto execution = chain.execute(context);
            ASSERT_TRUE(execution) << execution.error().info;
            if (repeat == 0) {
                encodings += visual ? fixture["encodings"].get<size_t>() : 1;
            }
            EXPECT_EQ(encoder->preparationCount(), encodings);
            ASSERT_EQ(context.inferencePrompt, prompt);
            if (visual) {
                const auto prepared =
                    std::dynamic_pointer_cast<const opk::python::YoloePreparedPrompt>(
                        provider->preparedFor(prompt, opk::python::YoloePreparationContract));
                ASSERT_TRUE(prepared);
                const auto expected =
                    bytes(assets / ("visual-features-" + std::to_string(index) + ".f32"));
                ASSERT_EQ(expected.size(), prepared->conditioning.size() * sizeof(float));
                for (size_t feature = 0; feature < prepared->conditioning.size(); ++feature) {
                    float value;
                    std::memcpy(&value, expected.data() + feature * sizeof(float), sizeof(float));
                    EXPECT_NEAR(prepared->conditioning[feature], value, 2e-4);
                }
            }
            const auto boxes = results.get<BoxDetectionsT>();
            const auto masks = results.get<InstanceSegmentationMasksT>();
            const auto association = results.get<PromptAssociationT>();
            ASSERT_TRUE(boxes);
            ASSERT_TRUE(masks);
            ASSERT_TRUE(association);
            EXPECT_EQ((*association)->request->revision, index + 1);
            EXPECT_EQ((*association)->request->request_id, UINT64_MAX);
            EXPECT_EQ((*association)->session_id, "native-session");
            ASSERT_EQ((*boxes)->detections.size(), fixture["boxes"].size());
            ASSERT_EQ((*masks)->masks.size(), fixture["boxes"].size());
            EXPECT_EQ((*association)->matches.size(), 2 * fixture["boxes"].size());
            std::set<uint64_t> identities;
            for (size_t detection = 0; detection < fixture["boxes"].size(); ++detection) {
                const auto &actual = *(*boxes)->detections[detection];
                const auto &expected = fixture["boxes"][detection];
                const auto projected =
                    open_perception_kit::projectBoundingBox(*actual.box, width, height);
                ASSERT_TRUE(projected);
                EXPECT_NEAR((*projected)[0], expected[0].get<float>(), 0.1);
                EXPECT_NEAR((*projected)[1], expected[1].get<float>(), 0.1);
                EXPECT_NEAR((*projected)[0] + (*projected)[2], expected[2].get<float>(), 0.1);
                EXPECT_NEAR((*projected)[1] + (*projected)[3], expected[3].get<float>(), 0.1);
                EXPECT_NEAR(actual.confidence, expected[4].get<float>(), 1e-5);
                EXPECT_FALSE(actual.class_id.has_value());
                const auto expectedMask =
                    bytes(assets / (fixture["mask_prefix"].get<std::string>() +
                                    std::to_string(detection) + ".bin"));
                const auto &mask = *(*masks)->masks[detection];
                ASSERT_TRUE(mask.bitmap);
                EXPECT_FALSE(mask.class_id.has_value());
                ASSERT_EQ(mask.bitmap->pixels.size(), expectedMask.size());
                size_t intersection = 0;
                size_t unionCount = 0;
                for (size_t pixel = 0; pixel < expectedMask.size(); ++pixel) {
                    intersection += expectedMask[pixel] != 0 && mask.bitmap->pixels[pixel] != 0;
                    unionCount += expectedMask[pixel] != 0 || mask.bitmap->pixels[pixel] != 0;
                }
                EXPECT_GE(unionCount ? static_cast<double>(intersection) / unionCount : 1.0, 0.995);
                for (const uint64_t object : {actual.object->id, mask.object->id}) {
                    EXPECT_TRUE(identities.insert(object).second);
                    const auto found =
                        std::find_if((*association)->matches.begin(),
                                     (*association)->matches.end(),
                                     [&](const auto &match) { return match->object_id == object; });
                    ASSERT_NE(found, (*association)->matches.end());
                    EXPECT_EQ((*found)->group_id,
                              "group-" +
                                  fixture["names"][expected[5].get<int>()].get<std::string>());
                }
            }
        }
    }
    // One-entry cache: a still-owned older revision is correctly re-encoded after eviction.
    open_perception_kit::FrameResults lateResults;
    context.frameResults = &lateResults;
    context.prompt = first;
    ASSERT_TRUE(chain.execute(context));
    EXPECT_EQ(encoder->preparationCount(), ++encodings);
    const auto late = lateResults.get<PromptAssociationT>();
    ASSERT_TRUE(late);
    EXPECT_EQ((*late)->request->revision, 1);
    {
        const auto prepared = provider->preparedFor(first, opk::python::YoloePreparationContract);
        ASSERT_TRUE(prepared);
        SubstituteQueryProvider substitute(prepared);
        substitute.instanceId = operations[0]->instanceId;
        auto substituted = operations;
        substituted[0] = &substitute;
        substitute.accepts = false;
        EXPECT_FALSE(operations[3]->bind(3, substituted));
        substitute.accepts = true;
        ASSERT_TRUE(operations[3]->bind(3, substituted));
        ASSERT_TRUE(parser->bind(5, substituted));
        ASSERT_TRUE(operations[3]->process(context));
        EXPECT_EQ(context.preparedPrompt, first);
        context.preparedPrompt.reset();
        open_perception_kit::FrameResults substitutedResults;
        context.frameResults = &substitutedResults;
        ASSERT_TRUE(parser->process(context));
        const auto association = substitutedResults.get<PromptAssociationT>();
        ASSERT_TRUE(association);
        EXPECT_EQ((*association)->request->revision, first->revision());
        EXPECT_EQ((*association)->matches.size(), (*late)->matches.size());
        ASSERT_TRUE(chain.bind());
    }
    const auto originalPrediction = context.inferenceOutputTensors[0];
    const auto originalPrototype = context.inferenceOutputTensors[1];
    std::vector<float> malformed(37 * 8400);
    for (size_t value = 0; value < malformed.size(); ++value) {
        malformed[value] = originalPrediction.get(value);
    }
    for (int failure = 0; failure < 3; ++failure) {
        open_perception_kit::FrameResults failedResults;
        context.frameResults = &failedResults;
        if (failure == 0) {
            context.inferenceOutputTensors[0] = opk::TensorView(malformed.data(),
                                                                malformed.size() * sizeof(float),
                                                                opk::Shape(1, 1, 1),
                                                                opk::Dtype::Float32,
                                                                1,
                                                                0);
        } else {
            malformed[4 * 8400] = failure == 1 ? std::numeric_limits<float>::quiet_NaN() : 1.0F;
            if (failure == 2) {
                malformed[0] = std::numeric_limits<float>::max();
                malformed[2 * 8400] = std::numeric_limits<float>::max();
            }
            context.inferenceOutputTensors[0] = opk::TensorView(malformed.data(),
                                                                malformed.size() * sizeof(float),
                                                                opk::Shape(1, 37, 8400),
                                                                opk::Dtype::Float32,
                                                                1,
                                                                0);
        }
        EXPECT_FALSE(parser->process(context));
        EXPECT_EQ(failedResults.count<BoxDetectionsT>(), 0);
        EXPECT_EQ(failedResults.count<InstanceSegmentationMasksT>(), 0);
        EXPECT_EQ(failedResults.count<PromptAssociationT>(), 0);
    }
    context.inferenceOutputTensors[0] = originalPrediction;
    std::vector<float> badPrototype(32 * 160 * 160, std::numeric_limits<float>::quiet_NaN());
    context.inferenceOutputTensors[1] = opk::TensorView(badPrototype.data(),
                                                        badPrototype.size() * sizeof(float),
                                                        opk::Shape(1, 32, 160, 160),
                                                        opk::Dtype::Float32,
                                                        1,
                                                        0);
    open_perception_kit::FrameResults failedMasks;
    context.frameResults = &failedMasks;
    EXPECT_FALSE(parser->process(context));
    EXPECT_EQ(failedMasks.count<BoxDetectionsT>(), 0);
    EXPECT_EQ(failedMasks.count<InstanceSegmentationMasksT>(), 0);
    EXPECT_EQ(failedMasks.count<PromptAssociationT>(), 0);
    context.inferenceOutputTensors[1] = originalPrototype;
    if (!visual) {
        std::string excessive;
        for (int token = 0; token < 76; ++token) {
            excessive += "a ";
        }
        context.prompt = textPrompt({excessive}, 10);
        ASSERT_TRUE(context.prompt);
        open_perception_kit::FrameResults rejectedResults;
        context.frameResults = &rejectedResults;
        EXPECT_FALSE(chain.execute(context));
        EXPECT_FALSE(context.inferencePrompt);
        EXPECT_FALSE(rejectedResults.get<PromptAssociationT>());
        EXPECT_EQ(encoder->preparationCount(), encodings);
    } else {
        const auto retainedPrepared =
            provider->preparedFor(first, opk::python::YoloePreparationContract);
        ASSERT_TRUE(retainedPrepared);
        // Fail the second reference after a successful first encoder call: publish no partial
        // state.
        const nlohmann::json invalidQueries = {{"person", 0, {0.05, 0.35, 0.30, 0.85}},
                                               {"tiny", 1, {0.001, 0.001, 0.0011, 0.0011}}};
        context.prompt = visualPrompt(assets, oracle["references"], invalidQueries, 10);
        ASSERT_TRUE(context.prompt);
        open_perception_kit::FrameResults rejectedResults;
        context.frameResults = &rejectedResults;
        EXPECT_FALSE(chain.execute(context));
        EXPECT_FALSE(context.inferencePrompt);
        EXPECT_FALSE(rejectedResults.get<PromptAssociationT>());
        EXPECT_FALSE(provider->preparedFor(context.prompt, opk::python::YoloePreparationContract));
        EXPECT_FALSE(provider->preparedFor(first, opk::python::YoloePreparationContract));
        EXPECT_EQ(encoder->preparationCount(), ++encodings);
        EXPECT_EQ(retainedPrepared->snapshot, first);
        EXPECT_EQ(
            std::dynamic_pointer_cast<const opk::python::YoloePreparedPrompt>(retainedPrepared)
                ->groupIds.front(),
            "group-person");
        context.prompt = textPrompt({"person"}, 11);
        EXPECT_FALSE(chain.execute(context));
        EXPECT_EQ(encoder->preparationCount(), encodings);
        EXPECT_FALSE(rejectedResults.get<PromptAssociationT>());
    }
    open_perception_kit::FrameResults emptyResults;
    context.frameResults = &emptyResults;
    const std::weak_ptr<const opk::PromptPayload> released = first->payload;
    first.reset();
    context.prompt.reset();
    ASSERT_TRUE(chain.execute(context));
    EXPECT_FALSE(context.inferencePrompt);
    EXPECT_FALSE(emptyResults.get<PromptAssociationT>());
    EXPECT_TRUE(released.expired());
}
} // namespace

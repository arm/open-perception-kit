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

#include "YoloePromptPrepareOp.h"
#include "Inference.h"
#include "PythonRuntime.h"
#include "YoloePreparedPrompt.h"
#include "opk/BilinearImageTensor.h"
#include "opk/QueryPromptInputs.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace opk::python {
YoloePreparedPrompt::YoloePreparedPrompt(std::shared_ptr<const PromptSnapshot> snapshot_,
                                         std::vector<std::string> groupIds_,
                                         std::vector<float> conditioning_)
    : PreparedPrompt(std::move(snapshot_), YoloePreparationContract),
      groupIds(std::move(groupIds_)), conditioning(std::move(conditioning_)) {}

size_t YoloePreparedPrompt::ownedBytes() const {
    size_t bytes = sizeof(*this) + conditioning.capacity() * sizeof(float) +
                   groupIds.capacity() * sizeof(std::string);
    for (const auto &group : groupIds) {
        bytes += group.capacity() + 1;
    }
    return bytes;
}

namespace {
constexpr int FeatureWidth = 512;
class YoloePreparationAdapter final : public op::PromptPreparationAdapter {
  public:
    ~YoloePreparationAdapter() override;
    Result<void> configure(const AttributeMap &attributes) override;
    Result<void>
    bind(const op::Op &owner, const size_t index, const std::vector<op::Op *> &ops) override;
    bool acceptsPrompt(const PromptFormat format,
                       const std::string_view inferenceId,
                       const std::string_view source) const override;
    std::span<const op::PromptConsumerCapability> promptCapabilities() const override;
    PreparationContract preparationContract() const override {
        return YoloePreparationContract;
    }
    size_t maxPreparedBytes() const override {
        return 64 * 1024;
    }
    Result<std::shared_ptr<const PreparedPrompt>>
    prepare(const std::shared_ptr<const PromptSnapshot> &snapshot) override;
    size_t preparationCount() const {
        return preparations;
    }

  private:
    Result<std::shared_ptr<const PreparedPrompt>>
    prepareText(const std::shared_ptr<const PromptSnapshot> &snapshot);
    Result<std::shared_ptr<const PreparedPrompt>>
    prepareVisual(const std::shared_ptr<const PromptSnapshot> &snapshot);
    std::string mode;
    std::string inferenceInstance;
    std::string sourceName;
    std::unique_ptr<onnx::Inference> encoder;
    PyObjectPtr tokenizer;
    size_t preparations = 0;
};

class YoloePromptInputAdapter final : public op::PromptInputAdapter {
  public:
    Result<void> configure(const AttributeMap &attributes) override;
    Result<void>
    bind(const op::Op &owner, const size_t index, const std::vector<op::Op *> &ops) override;
    Result<void> writeInputs(op::OpChainContext &context) override;
    op::InferenceInputBinding getInputBinding() const override;
    bool acceptsPrompt(const PromptFormat format,
                       const std::string_view inferenceId,
                       const std::string_view source) const override;
    std::span<const op::PromptConsumerCapability> promptCapabilities() const override {
        return promptConsumer ? promptConsumer->promptCapabilities()
                              : std::span<const op::PromptConsumerCapability>{};
    }

  private:
    std::string preparationInstance;
    std::string inferenceInstance;
    std::string sourceName;
    size_t tensorIndex = 1;
    const PreparedPromptProvider *preparer = nullptr;
    const op::OpInterfacePromptConsumer *promptConsumer = nullptr;
    const op::OpInterfaceInference *inference = nullptr;
    op::OpInterfaceResizableInput *resizable = nullptr;
    op::OpInterfaceSharedInput *shared = nullptr;
};

constexpr size_t TokenCount = 77;
constexpr size_t MaxConditioningBytes = MaxQueryPromptGroups * FeatureWidth * sizeof(float);

Result<PyObjectPtr> loadTokenizer(const std::filesystem::path &script,
                                  const std::filesystem::path &vocabulary) {
    PyObjectPtr utility(PyImport_ImportModule("importlib.util"));
    PyObjectPtr spec(utility ? PyObject_CallMethod(utility.get(),
                                                   "spec_from_file_location",
                                                   "ss",
                                                   "opk_query_tokenizer",
                                                   script.c_str())
                             : nullptr);
    PyObjectPtr module(
        spec ? PyObject_CallMethod(utility.get(), "module_from_spec", "O", spec.get()) : nullptr);
    PyObjectPtr loader(spec ? PyObject_GetAttrString(spec.get(), "loader") : nullptr);
    PyObjectPtr loaded(module && loader
                           ? PyObject_CallMethod(loader.get(), "exec_module", "O", module.get())
                           : nullptr);
    PyObjectPtr tokenizer(
        loaded ? PyObject_CallMethod(module.get(), "create_tokenizer", "s", vocabulary.c_str())
               : nullptr);
    if (!tokenizer || !PyCallable_Check(tokenizer.get())) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                        "Unable to load query tokenizer: " + formatPythonError()));
    }
    return tokenizer;
}

YoloePreparationAdapter::~YoloePreparationAdapter() {
    if (tokenizer) {
        const GILGuard gil;
        tokenizer = PyObjectPtr{};
    }
}

Result<void> YoloePreparationAdapter::configure(const AttributeMap &attributes) {
    preparations = 0;
    if (tokenizer) {
        const GILGuard gil;
        tokenizer = PyObjectPtr{};
    }
    encoder.reset();
    try {
        mode = attributes.getStringOrDefault("mode", "text");
        inferenceInstance = attributes.getString("inferenceInstance");
        sourceName = attributes.getStringOrDefault("inputImageSourceName", "pipelineVideoFrame");
        if ((mode != "text" && mode != "visual") || inferenceInstance.empty() ||
            sourceName.empty()) {
            return tl::unexpected(OPK_ERROR(
                ErrorFlag::InvalidOpChain,
                "Query preparation requires text or visual mode and an explicit destination"));
        }
        const std::filesystem::path descriptorPath = attributes.getString("modelDescriptor");
        const auto descriptor = ModelDescriptor::fromFile(descriptorPath.string());
        if (!descriptor) {
            return tl::unexpected(descriptor.error());
        }
        const auto threads = attributes.getIntOrDefault("intraOpNumThreads", 2);
        if (threads < 1 || threads > 256) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidOpChain, "Encoder thread count must be in [1,256]"));
        }
        encoder = std::make_unique<onnx::Inference>();
        const auto setup = encoder->setup(*descriptor, threads, 1);
        if (!setup) {
            return setup;
        }
        const auto &graph = encoder->getDeclaredModel();
        if (mode == "text" &&
            (graph.inputs.size() != 1 || graph.outputs.size() != 1 ||
             graph.inputs[0].name != "tokens" || graph.outputs[0].name != "conditioning" ||
             graph.inputs[0].valueType != Dtype::Int64 ||
             !(graph.inputs[0].shape == Shape(-1, static_cast<int>(TokenCount))))) {
            return tl::unexpected(OPK_ERROR(
                ErrorFlag::InvalidData, "Text encoder does not expose dynamic int64 tokens[Q,77]"));
        }
        if (mode == "visual" &&
            (graph.inputs.size() != 2 || graph.outputs.size() != 1 ||
             graph.inputs[0].name != "reference" || graph.inputs[1].name != "regions" ||
             graph.outputs[0].name != "conditioning" ||
             graph.inputs[0].valueType != Dtype::Float32 ||
             graph.inputs[1].valueType != Dtype::Float32 ||
             !(graph.inputs[0].shape == Shape(1, 3, 640, 640)) ||
             !(graph.inputs[1].shape == Shape(1, -1, 80, 80)))) {
            return tl::unexpected(OPK_ERROR(
                ErrorFlag::InvalidData,
                "Visual encoder requires RGB reference[1,3,640,640] and regions[1,Q,80,80]"));
        }
        const auto &outputShape = graph.outputs[0].shape;
        if (graph.outputs[0].valueType != Dtype::Float32 || outputShape.rank != 3 ||
            (outputShape.dims[0] != 1 && outputShape.dims[0] != -1) || outputShape.dims[1] != -1 ||
            (outputShape.dims[2] != FeatureWidth && outputShape.dims[2] != -1)) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidData,
                          "Query encoder must produce final Float32 conditioning[1,Q,512]"));
        }
        if (mode == "visual") {
            return {};
        }
        ensureRuntime();
        const GILGuard gil;
        auto loaded = loadTokenizer(
            descriptorPath.parent_path() /
                attributes.getStringOrDefault("tokenizerScript", "scripts/tokenizer.py"),
            descriptorPath.parent_path() /
                attributes.getStringOrDefault("tokenizerVocabulary",
                                              "bpe_simple_vocab_16e6.txt.gz"));
        if (!loaded) {
            return tl::unexpected(loaded.error());
        }
        tokenizer = std::move(*loaded);
        return {};
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidOpChain, error.what()));
    }
}

Result<void> YoloePreparationAdapter::bind(const op::Op &owner,
                                           const size_t index,
                                           const std::vector<op::Op *> &ops) {
    if (owner.loopId != 0) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidOpChain, "Query preparation requires an unlooped chain"));
    }
    for (size_t target = index + 1; target < ops.size(); ++target) {
        if (ops[target]->instanceId == inferenceInstance && ops[target]->loopId == 0 &&
            ops[target]->as<op::OpInterfaceInference>()) {
            return {};
        }
    }
    return tl::unexpected(
        OPK_ERROR(ErrorFlag::InvalidOpChain, "Query preparation destination inference is missing"));
}

bool YoloePreparationAdapter::acceptsPrompt(const PromptFormat format,
                                            const std::string_view inferenceId,
                                            const std::string_view source) const {
    return ((mode == "text" && format == TextPromptFormat) ||
            (mode == "visual" && format == VisualPromptFormat)) &&
           inferenceId == inferenceInstance && source == sourceName;
}

std::span<const op::PromptConsumerCapability> YoloePreparationAdapter::promptCapabilities() const {
    static constexpr std::array text{
        op::PromptConsumerCapability{TextPromptFormat,
                                     "yoloe-reprta-savpe-conditioning-v1",
                                     {MaxQueryPromptGroups, 0, 0, 0, MaxPromptDescriptionBytes},
                                     77}};
    static constexpr std::array visual{
        op::PromptConsumerCapability{VisualPromptFormat,
                                     "yoloe-reprta-savpe-conditioning-v1",
                                     {MaxQueryPromptGroups,
                                      MaxPromptReferences,
                                      MaxPromptReferenceDimension,
                                      MaxPromptReferenceBytes}}};
    return mode == "text" ? std::span<const op::PromptConsumerCapability>{text}
                          : std::span<const op::PromptConsumerCapability>{visual};
}

Result<std::shared_ptr<const PreparedPrompt>>
YoloePreparationAdapter::prepareText(const std::shared_ptr<const PromptSnapshot> &snapshot) {
    const auto validation = validateTextPrompt(*snapshot, inferenceInstance, sourceName);
    if (!validation) {
        return tl::unexpected(validation.error());
    }
    const auto &payload = static_cast<const TextPromptPayload &>(*snapshot->payload).value();
    const size_t count = payload.groups()->size();
    std::vector<std::string> groups;
    std::vector<int64_t> tokens(count * TokenCount);
    {
        const GILGuard gil;
        PyObjectPtr descriptions(PyList_New(static_cast<Py_ssize_t>(count)));
        if (!descriptions) {
            return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, formatPythonError()));
        }
        for (size_t index = 0; index < count; ++index) {
            const auto *const group = payload.groups()->Get(index);
            groups.push_back(group->group_id()->str());
            PyObject *const text = PyUnicode_DecodeUTF8(
                group->description()->c_str(), group->description()->size(), "strict");
            if (!text) {
                return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, formatPythonError()));
            }
            PyList_SET_ITEM(descriptions.get(), static_cast<Py_ssize_t>(index), text);
        }
        PyObjectPtr encoded(PyObject_CallOneArg(tokenizer.get(), descriptions.get()));
        if (!encoded || !PyList_Check(encoded.get()) ||
            PyList_Size(encoded.get()) != static_cast<Py_ssize_t>(count)) {
            return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                            "Query tokenization failed: " + formatPythonError()));
        }
        for (size_t group = 0; group < count; ++group) {
            PyObject *const row = PyList_GetItem(encoded.get(), static_cast<Py_ssize_t>(group));
            if (!PyList_Check(row) || PyList_Size(row) != TokenCount) {
                return tl::unexpected(OPK_ERROR(
                    ErrorFlag::InvalidData, "Tokenizer must return exactly 77 tokens per group"));
            }
            for (size_t index = 0; index < TokenCount; ++index) {
                const int64_t token =
                    PyLong_AsLongLong(PyList_GetItem(row, static_cast<Py_ssize_t>(index)));
                if (PyErr_Occurred() || token < 0 || token >= 49408) {
                    PyErr_Clear();
                    return tl::unexpected(OPK_ERROR(
                        ErrorFlag::InvalidData, "Tokenizer returned an invalid vocabulary index"));
                }
                tokens[group * TokenCount + index] = token;
            }
        }
    }
    const auto resized =
        encoder->resizeInputTensor(0,
                                   Shape(static_cast<int>(count), static_cast<int>(TokenCount)),
                                   Dtype::Int64,
                                   MaxQueryPromptGroups * TokenCount * sizeof(int64_t));
    if (!resized) {
        return tl::unexpected(resized.error());
    }
    std::memcpy(
        encoder->getInputTensorDataAddress(0), tokens.data(), tokens.size() * sizeof(int64_t));
    ++preparations;
    const auto inferred = encoder->inference();
    if (!inferred) {
        return tl::unexpected(inferred.error());
    }
    if (!(encoder->getOutputTensorFinalShape(0) ==
          Shape(1, static_cast<int>(count), FeatureWidth))) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::TensorError,
                                        "Encoder returned unexpected conditioning dimensions"));
    }
    const auto *const data =
        reinterpret_cast<const float *>(encoder->getOutputTensorDataAddress(0));
    std::vector<float> features(data, data + count * FeatureWidth);
    if (!std::all_of(features.begin(), features.end(), [](const float value) {
            return std::isfinite(value);
        })) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::TensorError, "Encoder returned non-finite conditioning"));
    }
    return std::make_shared<const YoloePreparedPrompt>(
        snapshot, std::move(groups), std::move(features));
}

Result<std::shared_ptr<const PreparedPrompt>>
YoloePreparationAdapter::prepareVisual(const std::shared_ptr<const PromptSnapshot> &snapshot) {
    const auto validation = validateVisualPrompt(*snapshot, inferenceInstance, sourceName);
    if (!validation) {
        return tl::unexpected(validation.error());
    }
    const auto &payload = static_cast<const VisualPromptPayload &>(*snapshot->payload).value();
    const size_t count = payload.groups()->size();
    std::vector<std::string> groups;
    std::vector<float> features(count * FeatureWidth);
    for (const auto *const group : *payload.groups()) {
        groups.push_back(group->group_id()->str());
    }
    for (size_t referenceIndex = 0; referenceIndex < payload.references()->size();
         ++referenceIndex) {
        std::vector<size_t> indices;
        for (size_t group = 0; group < count; ++group) {
            if (payload.groups()->Get(group)->reference_index() == referenceIndex) {
                indices.push_back(group);
            }
        }
        if (indices.empty()) {
            continue;
        }
        const auto &reference = *payload.references()->Get(referenceIndex);
        auto image =
            buildBilinearImageTensor({reference.pixels()->data(), reference.pixels()->size()},
                                     reference.width(),
                                     reference.height(),
                                     reference.width() * 3,
                                     RawImagePixelFormat::Rgb,
                                     encoder->getModel().inputs[0]);
        if (!image) {
            return tl::unexpected(image.error());
        }
        std::vector<float> regions(indices.size() * 80 * 80);
        const float gain = std::min(640.0 / reference.width(), 640.0 / reference.height());
        for (size_t local = 0; local < indices.size(); ++local) {
            const auto &box = *payload.groups()->Get(indices[local])->box();
            // Match author float32 xyxy -> normalized xywh -> xyxy rasterization.
            const float left =
                (static_cast<float>(box.left() * reference.width()) * gain + image->left) / 640;
            const float top =
                (static_cast<float>(box.top() * reference.height()) * gain + image->top) / 640;
            const float right =
                (static_cast<float>(box.right() * reference.width()) * gain + image->left) / 640;
            const float bottom =
                (static_cast<float>(box.bottom() * reference.height()) * gain + image->top) / 640;
            const float centerX = (left + right) / 2;
            const float centerY = (top + bottom) / 2;
            const float halfWidth = (right - left) / 2;
            const float halfHeight = (bottom - top) / 2;
            const float x1 = (centerX - halfWidth) * 80;
            const float y1 = (centerY - halfHeight) * 80;
            const float x2 = (centerX + halfWidth) * 80;
            const float y2 = (centerY + halfHeight) * 80;
            size_t selected = 0;
            for (size_t y = 0; y < 80; ++y) {
                for (size_t x = 0; x < 80; ++x) {
                    const bool inside = x >= x1 && x < x2 && y >= y1 && y < y2;
                    regions[local * 6400 + y * 80 + x] = inside ? 1.0F : 0.0F;
                    selected += inside;
                }
            }
            if (selected == 0) {
                return tl::unexpected(
                    OPK_ERROR(ErrorFlag::InvalidData,
                              "Visual prompt region is empty at the encoder's 80x80 grid"));
            }
        }
        const auto imageResized = encoder->resizeInputTensor(
            0, Shape(1, 3, 640, 640), Dtype::Float32, 3 * 640 * 640 * sizeof(float));
        if (!imageResized) {
            return tl::unexpected(imageResized.error());
        }
        const auto regionsResized =
            encoder->resizeInputTensor(1,
                                       Shape(1, static_cast<int>(indices.size()), 80, 80),
                                       Dtype::Float32,
                                       MaxQueryPromptGroups * 80 * 80 * sizeof(float));
        if (!regionsResized) {
            return tl::unexpected(regionsResized.error());
        }
        std::memcpy(encoder->getInputTensorDataAddress(0),
                    image->pixels.data(),
                    image->pixels.size() * sizeof(float));
        std::memcpy(
            encoder->getInputTensorDataAddress(1), regions.data(), regions.size() * sizeof(float));
        ++preparations;
        const auto inferred = encoder->inference();
        if (!inferred) {
            return tl::unexpected(inferred.error());
        }
        if (!(encoder->getOutputTensorFinalShape(0) ==
              Shape(1, static_cast<int>(indices.size()), FeatureWidth))) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::TensorError, "Visual encoder returned unexpected dimensions"));
        }
        const auto *const data =
            reinterpret_cast<const float *>(encoder->getOutputTensorDataAddress(0));
        if (!std::all_of(data, data + indices.size() * FeatureWidth, [](const float value) {
                return std::isfinite(value);
            })) {
            return tl::unexpected(OPK_ERROR(ErrorFlag::TensorError,
                                            "Visual encoder returned non-finite conditioning"));
        }
        for (size_t local = 0; local < indices.size(); ++local) {
            std::copy_n(data + local * FeatureWidth,
                        FeatureWidth,
                        features.data() + indices[local] * FeatureWidth);
        }
    }
    return std::make_shared<const YoloePreparedPrompt>(
        snapshot, std::move(groups), std::move(features));
}

Result<std::shared_ptr<const PreparedPrompt>>
YoloePreparationAdapter::prepare(const std::shared_ptr<const PromptSnapshot> &snapshot) {
    return mode == "text" ? prepareText(snapshot) : prepareVisual(snapshot);
}

Result<void> YoloePromptInputAdapter::configure(const AttributeMap &attributes) {
    try {
        preparationInstance = attributes.getString("preparationInstance");
        inferenceInstance = attributes.getString("inferenceInstance");
        sourceName = attributes.getStringOrDefault("inputImageSourceName", "pipelineVideoFrame");
        if (preparationInstance.empty() || inferenceInstance.empty() || sourceName.empty()) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidOpChain, "Query packing requires explicit bindings"));
        }
        return {};
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidOpChain, error.what()));
    }
}

op::InferenceInputBinding YoloePromptInputAdapter::getInputBinding() const {
    return {inferenceInstance, sourceName, {&tensorIndex, 1}};
}

bool YoloePromptInputAdapter::acceptsPrompt(const PromptFormat format,
                                            const std::string_view inferenceId,
                                            const std::string_view source) const {
    return promptConsumer && promptConsumer->acceptsPrompt(format, inferenceId, source);
}

Result<void> YoloePromptInputAdapter::bind(const op::Op &owner,
                                           const size_t index,
                                           const std::vector<op::Op *> &ops) {
    if (index == 0 || index + 1 >= ops.size() || owner.loopId != 0 || ops[index + 1]->loopId != 0) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidOpChain,
                      "Query packing requires an unlooped image/pack/inference sequence"));
    }
    const auto *const image = ops[index - 1]->as<op::OpInterfaceImageInputPreprocessor>();
    inference = ops[index + 1]->as<op::OpInterfaceInference>();
    resizable = ops[index + 1]->as<op::OpInterfaceResizableInput>();
    shared = ops[index + 1]->as<op::OpInterfaceSharedInput>();
    preparer = nullptr;
    promptConsumer = nullptr;
    for (size_t previous = 0; previous < index - 1; ++previous) {
        if (ops[previous]->instanceId == preparationInstance) {
            preparer = ops[previous]->as<PreparedPromptProvider>();
            promptConsumer = ops[previous]->as<op::OpInterfacePromptConsumer>();
        }
    }
    const auto imageBinding = image ? image->getImageInputBinding() : op::InferenceInputBinding{};
    if (!preparer || !promptConsumer ||
        (!promptConsumer->acceptsPrompt(TextPromptFormat, inferenceInstance, sourceName) &&
         !promptConsumer->acceptsPrompt(VisualPromptFormat, inferenceInstance, sourceName)) ||
        !image || imageBinding.tensorIndices.size() != 1 || !inference || !resizable ||
        ops[index + 1]->instanceId != inferenceInstance ||
        imageBinding.inferenceInstance != inferenceInstance ||
        imageBinding.sourceName != sourceName || imageBinding.tensorIndices[0] != 0 ||
        inference->getModel().inputs.size() != 2) {
        return tl::unexpected(OPK_ERROR(
            ErrorFlag::InvalidOpChain,
            "Query packing requires matching preparation/image/dynamic inference bindings"));
    }
    const auto &imageInput = inference->getModel().inputs[0];
    const auto &input = inference->getModel().inputs[1];
    if (!(imageInput.shape == Shape(1, 3, 640, 640)) || imageInput.valueType != Dtype::Float32 ||
        imageInput.dataKind != DataKind::ImageRgbChw || !imageInput.keepAspectRatio ||
        !MeanStd::isDefaultMean(imageInput.mean) || !MeanStd::isDefaultStd(imageInput.std) ||
        imageInput.letterboxRed != DefaultLetterboxColor ||
        imageInput.letterboxGreen != DefaultLetterboxColor ||
        imageInput.letterboxBlue != DefaultLetterboxColor || !imageInput.valueInputs.empty() ||
        imageInput.matchShapeOutputIndex != InvalidTensorIndex ||
        input.valueType != Dtype::Float32 || !(input.shape == Shape(1, 1, FeatureWidth)) ||
        !input.valueInputs.empty() || input.matchShapeOutputIndex != InvalidTensorIndex) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidOpChain,
                      "Query packing requires the qualified RGB640 / conditioning512 contract"));
    }
    return {};
}

Result<void> YoloePromptInputAdapter::writeInputs(op::OpChainContext &context) {
    const auto state = preparer->preparedFor(context.prompt, YoloePreparationContract);
    const auto prepared = std::dynamic_pointer_cast<const YoloePreparedPrompt>(state);
    const auto &geometry = context.inferenceInfo.image;
    if (!prepared || prepared->snapshot != context.prompt ||
        prepared->contract != YoloePreparationContract || prepared->groupIds.empty() ||
        prepared->groupIds.size() > MaxQueryPromptGroups ||
        prepared->conditioning.size() != prepared->groupIds.size() * FeatureWidth ||
        geometry.modelWidth != 640 || geometry.modelHeight != 640 || geometry.cropX != 0 ||
        geometry.cropY != 0 || geometry.frameId == 0 ||
        context.inferenceInfo.parentId != geometry.frameId ||
        geometry.width != geometry.frameWidth || geometry.height != geometry.frameHeight) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      "Query packing lacks matching prepared state or full target geometry"));
    }
    const auto resized = resizable->resizeInputTensor(
        tensorIndex,
        Shape(1, static_cast<int>(prepared->groupIds.size()), FeatureWidth),
        Dtype::Float32,
        MaxConditioningBytes);
    if (!resized) {
        return tl::unexpected(resized.error());
    }
    if (shared) {
        const auto owned = shared->setOwnedInputStorage(tensorIndex);
        if (!owned) {
            return tl::unexpected(owned.error());
        }
    }
    auto *const target = inference->getTensorDataAddress(tensorIndex);
    if (!target) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::TensorError, "Conditioning input has no storage"));
    }
    std::memcpy(
        target, prepared->conditioning.data(), prepared->conditioning.size() * sizeof(float));
    return {};
}
} // namespace

YoloePromptPrepareOp::YoloePromptPrepareOp()
    : op::PromptPrepareOp(std::make_unique<YoloePreparationAdapter>()) {}

size_t YoloePromptPrepareOp::preparationCount() const {
    return static_cast<const YoloePreparationAdapter &>(preparationAdapter()).preparationCount();
}

YoloePromptPackOp::YoloePromptPackOp()
    : op::PromptInputOp(std::make_unique<YoloePromptInputAdapter>()) {}

} // namespace opk::python

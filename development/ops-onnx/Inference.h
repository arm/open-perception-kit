/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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
#pragma once

#include <cstdint>
#include <fmt/core.h>
#include <onnxruntime_cxx_api.h>

#include "Log.h"
#include "opk/ModelDescriptor.h"

#include "opk/Model.h"
#include "opk/Result.h"
#include "opk/Types.h"

#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opk::onnx {

// ONNX level tensor
struct Tensor {

    Tensor(const opk::Shape &shape, opk::Dtype type) {
        this->shape = shape;
        this->type = type;
        this->typeByteSize = opk::getValueTypeByteSize(type);
        for (size_t i = 0; i < shape.rank; i++)
            onnxShape[i] = shape.dims[i];

        if (shape.hasDynamicDimension()) {
            opk::log::info("Creating dynamic tensor with shape: {}\n", shape.toString());
            // do nothing
        } else {
            // ONNX rank-zero tensors contain one scalar. OPK's empty Shape otherwise
            // remains an unspecified shape; keep this convention local to ONNX.
            this->data.resize((shape.rank == 0 ? 1 : shape.getFullValueCount()) * typeByteSize);
            std::fill(this->data.begin(), this->data.end(), 0);
        }
    }

    uint8_t *getData() {
        return sharedData.empty() ? data.data() : sharedData.data();
    }

    bool hasSharedStorage() const {
        return !sharedData.empty();
    }

    opk::Result<Ort::Value> setSharedStorage(const Ort::MemoryInfo &memInfo,
                                             const std::span<uint8_t> storage,
                                             std::shared_ptr<void> owner) {
        if (!owner || storage.empty() || !storage.data() || typeByteSize == 0 ||
            storage.size() != getByteCount() ||
            reinterpret_cast<uintptr_t>(storage.data()) % typeByteSize != 0) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::TensorError,
                          "Shared ONNX input has invalid size, alignment or owner"));
        }
        auto value{createOnnxTensor(memInfo, storage.data(), storage.size() / typeByteSize)};
        if (!value) {
            return value;
        }
        sharedOwner = std::move(owner);
        sharedData = storage;
        std::vector<uint8_t>{}.swap(data);
        return value;
    }

    /// Reuses or allocates owned storage and returns its ONNX view; borrowed contents are
    /// untouched.
    opk::Result<Ort::Value> setOwnedStorage(const Ort::MemoryInfo &memInfo) {
        if (!hasSharedStorage()) {
            return createOnnxTensor(memInfo);
        }
        std::vector<uint8_t> ownedData(getByteCount());
        auto value{createOnnxTensor(memInfo, ownedData.data(), getElementCount())};
        if (!value) {
            return value;
        }
        data = std::move(ownedData);
        sharedData = {};
        sharedOwner.reset();
        return value;
    }

    size_t getElementCount() const {
        return getByteCount() / typeByteSize;
    }

    size_t getByteCount() const {
        return sharedData.empty() ? data.size() : sharedData.size();
    }

    bool checkShape(const opk::Shape &shape) const {
        return this->shape == shape;
    }

    opk::Result<Ort::Value> createOnnxTensor(const Ort::MemoryInfo &memInfo) {
        return createOnnxTensor(memInfo, getData(), getElementCount());
    }

  private:
    opk::Result<Ort::Value> createOnnxTensor(const Ort::MemoryInfo &memInfo,
                                             uint8_t *const tensorData,
                                             const size_t elementCount) {
        try {
            if (this->type == opk::Dtype::Float32) {
                return Ort::Value::CreateTensor<float>(memInfo,
                                                       reinterpret_cast<float *>(tensorData),
                                                       elementCount,
                                                       this->onnxShape,
                                                       this->shape.rank);
            }
            if (this->type == opk::Dtype::Int64) {
                return Ort::Value::CreateTensor<int64_t>(memInfo,
                                                         reinterpret_cast<int64_t *>(tensorData),
                                                         elementCount,
                                                         onnxShape,
                                                         this->shape.rank);
            }
            if (this->type == opk::Dtype::Uint8) {
                return Ort::Value::CreateTensor<uint8_t>(
                    memInfo, tensorData, elementCount, onnxShape, this->shape.rank);
            }
        } catch (const Ort::Exception &error) {
            const std::string message =
                "Failed to create ONNX tensor: " + std::string(error.what());
            opk::log::error("{}\n", message);
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::TensorError, message));
        }

        const std::string message =
            "Unsupported ONNX tensor dtype " + std::to_string(static_cast<int>(this->type));
        opk::log::error("{}\n", message);
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::TensorError, message));
    }

    opk::Dtype type;
    size_t typeByteSize;
    opk::Shape shape;
    int64_t onnxShape[8];

    std::vector<uint8_t> data;
    std::shared_ptr<void> sharedOwner;
    std::span<uint8_t> sharedData;
};

struct Inference {

    Inference();
    virtual ~Inference();

    opk::Result<void> setupFromJson(const std::string &filePath,
                                    const std::optional<int64_t> intraOpNumThreads = std::nullopt,
                                    const std::optional<int64_t> interOpNumThreads = std::nullopt);
    opk::Result<void> setup(const opk::ModelDescriptor &modelDesc,
                            const std::optional<int64_t> intraOpNumThreads = std::nullopt,
                            const std::optional<int64_t> interOpNumThreads = std::nullopt);
    bool isReady() {
        return setupReady;
    }

    opk::Result<void> preprocessImageData(size_t tensorIndex,
                                          const uint8_t *data,
                                          opk::DataKind dataKind,
                                          opk::Dtype valueType,
                                          size_t imageWidth,
                                          size_t imageHeight);

    // void prepareForPostprocess(opk::TensorParser::Input &input);
    opk::Result<void> inference();

    /// Restore zero initial state for inputs populated by tensor feedback.
    opk::Result<void> resetFeedbackState();

    opk::Result<void> setSharedInputStorage(const size_t index,
                                            const std::span<uint8_t> data,
                                            std::shared_ptr<void> owner);
    opk::Result<void> setOwnedInputStorage(const size_t index);

    const opk::Model &getModel() const {
        return this->model;
    }

    uint8_t *getInputTensorDataAddress(size_t index) {
        return api.inputTensors[index]->getData();
    }

    const uint8_t *getOutputTensorDataAddress(size_t index) const {
        assert(outputTensorPointers[index]);
        return outputTensorPointers[index];
    }

    opk::Shape getOutputTensorFinalShape(size_t index) const {
        return outputTensorFinalShapes[index];
    }

  private:
    opk::InferenceInfo inferenceInfo;
    bool setupReady = false;

    static bool onnxTypeToUniflowType(ONNXTensorElementDataType onnxType, opk::Dtype &outType);
    static std::vector<int64_t>
    getTensorShape(const Ort::Session &session, opk::TensorInOut tensorInOut, int tensorIndex);
    static opk::Result<opk::Model> inspectModel(const Ort::Session &session);

    std::unique_ptr<Ort::Env> environment;
    std::unique_ptr<Ort::SessionOptions> sessionOptions;
    std::unique_ptr<Ort::MemoryInfo> memoryInfo;
    std::unique_ptr<Ort::Session> session;

    Result<void> setupTensorsForModel();
    Result<void> recreateInputTensor(size_t index, const opk::Shape &shape, opk::Dtype valueType);

    opk::ModelDescriptor modelDescriptor;
    opk::Model model;

    std::vector<Ort::Value> dynamicOutputData;

    // Output tensor buffer pointers and shapes populated after inference().
    // Valid only until the next inference() call.
    // Backend internal: use TensorView interface (via getOutputTensorDataAddress) for external
    // access.
    const uint8_t *outputTensorPointers[opk::MaxTensorCount] = {nullptr};
    opk::Shape outputTensorFinalShapes[opk::MaxTensorCount];

    // ---

    struct ApiTensorGlue {
        std::unique_ptr<onnx::Tensor> inputTensors[opk::MaxTensorCount];
        std::unique_ptr<onnx::Tensor> outputTensors[opk::MaxTensorCount];
        std::vector<const char *> inputNames;
        std::vector<const char *> outputNames;
        std::vector<Ort::Value> inputTensorVector;
        std::vector<Ort::Value> outputTensorVector;
    } api;
};
} // namespace opk::onnx

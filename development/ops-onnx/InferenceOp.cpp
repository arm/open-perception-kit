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
#include "InferenceOp.h"

#include <fmt/core.h>
#include <memory>
#include <onnxruntime_cxx_api.h>

#include "Inference.h"
#include "glib-object.h"
#include "glib.h"
#include "gst/gstpad.h"
#include "op/OpChainContext.h"
#include "opk/AttributeMap.h"
#include "opk/ModelDescriptor.h"
#include "opk/TensorView.h"
#include "opk/Tools.h"

#include <perf/PerformanceMetrics.h>

using namespace opk::onnx;

InferenceOp::InferenceOp() = default;

InferenceOp::~InferenceOp() = default;

opk::Result<void> InferenceOp::configure(const opk::AttributeMap &attributes) {
    std::string modelDescPath;
    std::optional<int64_t> intraOpNumThreads;

    try {
        modelDescPath = attributes.getString("modelDescriptor");
    } catch (const opk::AttributeError &error) {

        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      fmt::format("Missing required attribute in InferenceOp: {}", error.what())));
    }

    if (attributes.contains("intraOpNumThreads")) {
        const auto &value = attributes.at("intraOpNumThreads");
        if (!value.isInt()) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                          fmt::format("intraOpNumThreads must be an integer, got {}",
                                      nlohmann::json(value).dump())));
        }
        intraOpNumThreads = value.asInt();
    }

    try {
        inference = std::make_unique<onnx::Inference>();

        const auto setupResult = inference->setupFromJson(modelDescPath, intraOpNumThreads);
        if (!setupResult) {
            return setupResult;
        }
    } catch (const std::exception &e) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                        fmt::format("OnnxRT startup error: {}", e.what())));
    }

    return {};
}

opk::Result<opk::op::OpSignal> InferenceOp::process(opk::op::OpChainContext &opChainContext) {
    OPK_PERF_SCOPE(fmt::format("onnx/Infer/{}", opChainContext.inferenceInfo.modelName));

    // inference
    auto inferenceResult = inference->inference();
    if (!inferenceResult) {
        return tl::unexpected(inferenceResult.error());
    }

    // populate the context with views to the output tensors
    size_t outputTensorCount = inference->getModel().outputs.size();
    opChainContext.inferenceOutputTensorCount = outputTensorCount;
    for (size_t i = 0; i < outputTensorCount; i++) {
        opChainContext.inferenceOutputTensors[i] = inference->getModel().createOutputTensorView(
            i, inference->getOutputTensorDataAddress(i), inference->getOutputTensorFinalShape(i));
    }

    return opk::op::OpSignal::Continue;
}

opk::Result<void> InferenceOp::bind(size_t index, const std::vector<opk::op::Op *> &ops) {
    return {};
}

const opk::Model &InferenceOp::getModel() const {
    assert(inference);
    return inference->getModel();
}

uint8_t *InferenceOp::getTensorDataAddress(size_t index) const {
    assert(inference);
    return inference->getInputTensorDataAddress(index);
}

opk::Result<void> InferenceOp::bindInputTensor(const size_t index,
                                               const std::span<uint8_t> data,
                                               std::shared_ptr<void> owner) {
    return inference->bindInputTensor(index, data, std::move(owner));
}

opk::Result<void> InferenceOp::resetInputTensor(const size_t index) {
    return inference->resetInputTensor(index);
}

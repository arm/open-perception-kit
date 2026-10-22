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
#include "InferenceOp.h"

#include "op/OpChainContext.h"
#include "perf/PerformanceMetrics.h"

#include <stdexcept>

namespace opk::mlwl {

InferenceOp::InferenceOp() = default;
InferenceOp::~InferenceOp() = default;

opk::Result<void> InferenceOp::configure(const opk::AttributeMap &attributes) {
    inference.reset();
    std::string descriptorPath;
    try {
        descriptorPath = attributes.getString("modelDescriptor");
    } catch (const opk::AttributeError &error) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain, error.what()));
    }

    try {
        auto candidate = std::make_unique<Inference>();
        const auto result = candidate->setupFromJson(descriptorPath);
        if (!result)
            return result;
        inference = std::move(candidate);
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                        "MLWL startup: " + std::string(error.what())));
    }
    return {};
}

opk::Result<void> InferenceOp::bind(size_t, const std::vector<opk::op::Op *> &) {
    return {};
}

const opk::Model &InferenceOp::getModel() const {
    if (!inference)
        throw std::logic_error("MLWL inference has not been configured");
    return inference->getModel();
}

uint8_t *InferenceOp::getTensorDataAddress(size_t index) const {
    if (!inference)
        throw std::logic_error("MLWL inference has not been configured");
    return inference->getInputTensorDataAddress(index);
}

opk::Result<opk::op::OpSignal> InferenceOp::process(opk::op::OpChainContext &context) {
    context.inferenceOutputTensorCount = 0;
    if (!inference)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtStartupError,
                                        "MLWL inference has not been configured"));
    OPK_PERF_SCOPE(fmt::format("mlwl/Infer/{}", inference->getModel().name));
    const auto result = inference->inference();
    if (!result)
        return tl::unexpected(result.error());
    try {
        const auto &model = inference->getModel();
        for (size_t i = 0; i < model.outputs.size(); ++i)
            context.inferenceOutputTensors[i] =
                model.createOutputTensorView(i,
                                             inference->getOutputTensorDataAddress(i),
                                             inference->getOutputTensorFinalShape(i));
        context.inferenceOutputTensorCount = model.outputs.size();
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InferenceRtInferenceError,
                                        "MLWL execution: " + std::string(error.what())));
    }
    return opk::op::OpSignal::Continue;
}

} // namespace opk::mlwl

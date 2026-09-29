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

#include "ValidatorInternal.h"

#include <format>
#include <limits>
#include <unordered_map>

namespace opk::config::detail {
namespace {

bool shapeByteSizeOverflows(const opk::TensorDescriptor &tensor) {
    std::size_t count = 1;
    for (std::size_t index = 0; index < tensor.shape.rank; ++index) {
        const auto dimension = static_cast<std::size_t>(tensor.shape.dims[index]);
        if (dimension == 0 || count > std::numeric_limits<std::size_t>::max() / dimension)
            return true;
        count *= dimension;
    }

    const std::size_t width = opk::getValueTypeByteSize(tensor.dtype);
    return width == 0 || count > std::numeric_limits<std::size_t>::max() / width;
}

void validateTensorSizes(ValidationReport &report,
                         const std::vector<opk::TensorDescriptor> &tensors,
                         std::string_view arrayName,
                         std::string_view source) {
    for (std::size_t index = 0; index < tensors.size(); ++index) {
        if (!shapeByteSizeOverflows(tensors[index]))
            continue;
        report.issues.push_back(makeIssue("model.v1.tensor-size",
                                          ValidationPhase::Descriptor,
                                          source,
                                          std::format("/{}/{}/shape", arrayName, index),
                                          "tensor element or byte count overflows size_t"));
    }
}

} // namespace

ValidationReport validateModelV1(const opk::ModelDescriptor &descriptor, std::string_view source) {
    ValidationReport report;
    validateControlFreeString(report, descriptor.name, source, "/name", "common.v1.name-control");
    validateControlFreeString(
        report, descriptor.modelFile, source, "/modelFile", "model.v1.model-file-control");
    validateTensorSizes(report, descriptor.inputTensors, "inputTensors", source);
    validateTensorSizes(report, descriptor.outputTensors, "outputTensors", source);

    std::unordered_map<std::size_t, std::size_t> firstDestination;
    for (std::size_t index = 0; index < descriptor.tensorFeedbacks.size(); ++index) {
        const auto &feedback = descriptor.tensorFeedbacks[index];
        const std::string base = std::format("/tensorFeedbacks/{}", index);

        if (feedback.toInputTensorIndex >= descriptor.inputTensors.size()) {
            report.issues.push_back(makeIssue("model.v1.feedback-destination",
                                              ValidationPhase::Descriptor,
                                              source,
                                              base + "/toInputTensorIndex",
                                              "feedback destination input does not exist"));
            continue;
        }

        const auto [first, inserted] =
            firstDestination.try_emplace(feedback.toInputTensorIndex, index);
        if (!inserted) {
            report.issues.push_back(
                makeIssue("model.v1.feedback-destination",
                          ValidationPhase::Descriptor,
                          source,
                          base + "/toInputTensorIndex",
                          std::format("feedback destination is already used by tensorFeedbacks/{}",
                                      first->second),
                          std::format("/tensorFeedbacks/{}/toInputTensorIndex", first->second)));
        }

        const auto &destination = descriptor.inputTensors[feedback.toInputTensorIndex];
        if (destination.dataKind != opk::DataKind::RawTensorData) {
            report.issues.push_back(
                makeIssue("model.v1.feedback-destination",
                          ValidationPhase::Descriptor,
                          source,
                          base + "/toInputTensorIndex",
                          "feedback destination must target a RawTensorData input"));
        }

        if (descriptor.dynamicOutput)
            continue;

        if (feedback.fromOutputTensorIndex >= descriptor.outputTensors.size()) {
            report.issues.push_back(makeIssue("model.v1.feedback-source",
                                              ValidationPhase::Descriptor,
                                              source,
                                              base + "/fromOutputTensorIndex",
                                              "feedback source output does not exist"));
            continue;
        }

        const auto &sourceTensor = descriptor.outputTensors[feedback.fromOutputTensorIndex];
        if (sourceTensor.dtype != destination.dtype || !(sourceTensor.shape == destination.shape)) {
            report.issues.push_back(
                makeIssue("model.v1.feedback-compatible",
                          ValidationPhase::Descriptor,
                          source,
                          base,
                          "feedback source and destination dtype and shape must match",
                          std::format("/inputTensors/{}", feedback.toInputTensorIndex)));
        }
    }

    report.sort();
    return report;
}

} // namespace opk::config::detail

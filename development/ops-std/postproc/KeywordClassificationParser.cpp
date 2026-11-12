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

#include "postproc/KeywordClassificationParser.h"

#include <cmath>
#include <span>

namespace opk::stdop::postproc {
namespace {

struct Configuration {
    size_t tensorIndex;
    bool softmax;
    float threshold;
    std::span<const opk::AttributeValue> labels;
};

struct Candidate {
    size_t index;
    float probability;
};

auto invalidInput(const char *const message) {
    return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, message));
}

opk::Result<Configuration> readConfiguration(const opk::AttributeMap &attributes) {
    if (!attributes.contains("outputTensorIndex") || !attributes.at("outputTensorIndex").isInt() ||
        !attributes.contains("activation") || !attributes.at("activation").isString() ||
        !attributes.contains("confidenceThreshold") ||
        (!attributes.at("confidenceThreshold").isInt() &&
         !attributes.at("confidenceThreshold").isDouble()) ||
        !attributes.contains("labels") || !attributes.at("labels").isArray())
        return invalidInput("KeywordClassificationParser requires outputTensorIndex, activation, "
                            "confidenceThreshold and labels");
    const auto tensorIndex = attributes.getInt("outputTensorIndex");
    if (tensorIndex < 0 || static_cast<uint64_t>(tensorIndex) >= opk::MaxTensorCount)
        return invalidInput(
            "KeywordClassificationParser outputTensorIndex is outside the output array");
    const auto &activation = attributes.getString("activation");
    const bool softmax = activation == "softmax";
    if (!softmax && activation != "none")
        return invalidInput("KeywordClassificationParser activation must be softmax or none");
    const auto threshold = attributes.getDouble("confidenceThreshold");
    if (!std::isfinite(threshold) || threshold < 0 || threshold > 1)
        return invalidInput("KeywordClassificationParser confidenceThreshold must be in [0,1]");
    const auto &labels = attributes.at("labels").asArray();
    bool hasLabel = false;
    for (const auto &label : labels) {
        if (label.isNull())
            continue;
        if (!label.isString() || label.asString().empty())
            return invalidInput(
                "KeywordClassificationParser labels must be nonempty strings or null");
        hasLabel = true;
    }
    if (!hasLabel)
        return invalidInput("KeywordClassificationParser requires at least one nonempty label");
    return Configuration{.tensorIndex = static_cast<size_t>(tensorIndex),
                         .softmax = softmax,
                         .threshold = static_cast<float>(threshold),
                         .labels = labels};
}

opk::Result<Candidate> decodeCandidate(const opk::TensorParser::Input &input,
                                       const Configuration &configuration) {
    const auto *const tensor = input.tensors[configuration.tensorIndex];
    if (!tensor || !tensor->isValid() || tensor->getValueType() != opk::Dtype::Float32)
        return invalidInput("KeywordClassificationParser requires a valid Float32 class vector");
    const auto &shape = tensor->getShape();
    const auto labels = configuration.labels;
    if (shape.rank == 0 || shape.rank > 8 || shape.dims[shape.rank - 1] <= 0 ||
        static_cast<size_t>(shape.dims[shape.rank - 1]) != labels.size())
        return invalidInput("KeywordClassificationParser class axis must match its labels");
    for (size_t dimension = 0; dimension + 1 < shape.rank; ++dimension) {
        if (shape.dims[dimension] != 1)
            return invalidInput(
                "KeywordClassificationParser requires singleton leading dimensions");
    }
    size_t winner = 0;
    float maximum = 0;
    for (size_t index = 0; index < labels.size(); ++index) {
        const float score = tensor->get(index);
        if (!std::isfinite(score) || (!configuration.softmax && (score < 0 || score > 1)))
            return invalidInput(
                "KeywordClassificationParser requires finite logits or scores in [0,1]");
        if (index == 0 || score > maximum) {
            maximum = score;
            winner = index;
        }
    }
    // Ignored classes still compete for the maximum and remain in the softmax denominator.
    if (labels[winner].isNull() || !configuration.softmax)
        return Candidate{winner, maximum};
    double sum = 0;
    for (size_t index = 0; index < labels.size(); ++index)
        sum += std::exp(static_cast<double>(tensor->get(index)) - maximum);
    return Candidate{winner, static_cast<float>(1.0 / sum)};
}

open_perception_kit::metadata::ClassificationsT makeResult(const opk::TensorParser::Input &input,
                                                           const Candidate &decoded,
                                                           const std::string &label) {
    open_perception_kit::metadata::ClassificationsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = "keyword",
                                            .producer = &input.producerInfo});
    auto classification = std::make_unique<open_perception_kit::metadata::ClassificationT>();
    classification->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    auto candidate = std::make_unique<open_perception_kit::metadata::ClassificationCandidateT>();
    candidate->class_id = static_cast<int>(decoded.index);
    candidate->confidence = decoded.probability;
    candidate->text = label;
    classification->candidates.push_back(std::move(candidate));
    payload.classifications.push_back(std::move(classification));
    return payload;
}

} // namespace

opk::Result<void> KeywordClassificationParser::parse(const opk::TensorParser::Input &input,
                                                     open_perception_kit::FrameResults &results) {
    const auto configuration = readConfiguration(input.attributes);
    if (!configuration)
        return tl::unexpected(configuration.error());
    const auto candidate = decodeCandidate(input, *configuration);
    if (!candidate)
        return tl::unexpected(candidate.error());
    const auto &label = configuration->labels[candidate->index];
    // Compare in the output score's Float32 domain, including exact threshold equality.
    if (label.isNull() || candidate->probability < configuration->threshold)
        return {};
    results.add(makeResult(input, *candidate, label.asString()));
    return {};
}

} // namespace opk::stdop::postproc

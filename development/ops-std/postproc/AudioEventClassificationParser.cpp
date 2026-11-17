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

#include "postproc/AudioEventClassificationParser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fmt/core.h>
#include <span>

namespace opk::stdop::postproc {
namespace {

struct Configuration {
    size_t tensorIndex;
    bool sigmoid;
    size_t topK;
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
        return invalidInput(
            "AudioEventClassificationParser requires outputTensorIndex, activation, "
            "confidenceThreshold and labels");
    const auto tensorIndex = attributes.getInt("outputTensorIndex");
    if (tensorIndex < 0 || static_cast<uint64_t>(tensorIndex) >= opk::MaxTensorCount)
        return invalidInput(
            "AudioEventClassificationParser outputTensorIndex is outside the output array");
    const auto &activation = attributes.getString("activation");
    const bool sigmoid = activation == "sigmoid";
    if (!sigmoid && activation != "none")
        return invalidInput("AudioEventClassificationParser activation must be sigmoid or none");
    if (!attributes.contains("topK") || !attributes.at("topK").isInt() ||
        attributes.getInt("topK") < 1 || attributes.getInt("topK") > 2147483647)
        return invalidInput("AudioEventClassificationParser topK must be a positive int");
    const auto threshold = attributes.getDouble("confidenceThreshold");
    if (!std::isfinite(threshold) || threshold < 0 || threshold > 1)
        return invalidInput("AudioEventClassificationParser confidenceThreshold must be in [0,1]");
    const auto &labels = attributes.at("labels").asArray();
    bool hasLabel = false;
    for (const auto &label : labels) {
        if (label.isNull())
            continue;
        if (!label.isString() || label.asString().empty())
            return invalidInput(
                "AudioEventClassificationParser labels must be nonempty strings or null");
        hasLabel = true;
    }
    if (!hasLabel)
        return invalidInput("AudioEventClassificationParser requires at least one nonempty label");
    return Configuration{.tensorIndex = static_cast<size_t>(tensorIndex),
                         .sigmoid = sigmoid,
                         .topK = static_cast<size_t>(attributes.getInt("topK")),
                         .threshold = static_cast<float>(threshold),
                         .labels = labels};
}

opk::Result<std::vector<Candidate>> decodeCandidates(const opk::TensorParser::Input &input,
                                                     const Configuration &configuration) {
    const auto *const tensor = input.tensors[configuration.tensorIndex];
    if (!tensor || !tensor->isValid() || tensor->getValueType() != opk::Dtype::Float32)
        return invalidInput("AudioEventClassificationParser requires a valid Float32 class vector");
    const auto &shape = tensor->getShape();
    const auto labels = configuration.labels;
    if (shape.rank == 0 || shape.rank > 8 || shape.dims[shape.rank - 1] <= 0 ||
        static_cast<size_t>(shape.dims[shape.rank - 1]) != labels.size())
        return invalidInput("AudioEventClassificationParser class axis must match its labels");
    for (size_t dimension = 0; dimension + 1 < shape.rank; ++dimension) {
        if (shape.dims[dimension] != 1)
            return invalidInput(
                "AudioEventClassificationParser requires singleton leading dimensions");
    }
    std::vector<Candidate> candidates;
    for (size_t index = 0; index < labels.size(); ++index) {
        const float score = tensor->get(index);
        if (!std::isfinite(score) || (!configuration.sigmoid && (score < 0 || score > 1)))
            return invalidInput(
                "AudioEventClassificationParser requires finite logits or scores in [0,1]");
        // Stable sigmoid keeps independent event probabilities, including extreme logits.
        const float exponential = std::exp(-std::abs(score));
        const float probability =
            configuration.sigmoid
                ? (score >= 0 ? 1.0f / (1.0f + exponential) : exponential / (1.0f + exponential))
                : score;
        if (!labels[index].isNull() && probability >= configuration.threshold)
            candidates.push_back({index, probability});
    }
    const size_t count = std::min(configuration.topK, candidates.size());
    std::partial_sort(candidates.begin(),
                      candidates.begin() + count,
                      candidates.end(),
                      [](const Candidate &left, const Candidate &right) {
                          return left.probability != right.probability
                                     ? left.probability > right.probability
                                     : left.index < right.index;
                      });
    candidates.resize(count);
    return candidates;
}

open_perception_kit::metadata::ClassificationsT makeResult(const opk::TensorParser::Input &input,
                                                           const std::span<const Candidate> decoded,
                                                           const Configuration &configuration) {
    open_perception_kit::metadata::ClassificationsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = "esc",
                                            .producer = &input.producerInfo});
    auto classification = std::make_unique<open_perception_kit::metadata::ClassificationT>();
    classification->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    for (const auto &entry : decoded) {
        auto candidate =
            std::make_unique<open_perception_kit::metadata::ClassificationCandidateT>();
        candidate->class_id = static_cast<int>(entry.index);
        candidate->confidence = entry.probability;
        candidate->text = configuration.labels[entry.index].asString();
        classification->candidates.push_back(std::move(candidate));
    }
    payload.classifications.push_back(std::move(classification));
    return payload;
}

} // namespace

opk::Result<void>
AudioEventClassificationParser::parse(const opk::TensorParser::Input &input,
                                      open_perception_kit::FrameResults &results) {
    const auto configuration = readConfiguration(input.attributes);
    if (!configuration)
        return tl::unexpected(configuration.error());
    const auto candidates = decodeCandidates(input, *configuration);
    if (!candidates)
        return tl::unexpected(candidates.error());
    if (!candidates->empty())
        results.add(makeResult(input, *candidates, *configuration));
    if (input.attributes.getBoolOrDefault("log", false)) {
        for (const auto &candidate : *candidates)
            fmt::print(stderr,
                       "Audio event [{}]: {} (class={}, confidence={:.3f})\n",
                       input.inferenceInfo.modelName,
                       configuration->labels[candidate.index].asString(),
                       candidate.index,
                       candidate.probability);
    }
    return {};
}

} // namespace opk::stdop::postproc

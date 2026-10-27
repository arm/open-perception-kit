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

#include "postproc/PaddleOcrRecognitionParser.h"

#include "opk/File.h"
#include "opk/String.h"

#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace opk::stdop::postproc {
namespace {

opk::Result<std::vector<std::string>> loadVocabulary(const opk::AttributeMap &attributes) {
    for (const auto &[key, value] : attributes.raw()) {
        if (key == "parser" && value.isString() && value.asString() == "PaddleOcrRecognitionParser")
            continue;
        if (key == "dictionaryFile" && value.isString() && !value.asString().empty())
            continue;
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrRecognitionParser: invalid attribute: " + key));
    }
    if (!attributes.contains("parser") || !attributes.contains("dictionaryFile"))
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "PaddleOcrRecognitionParser: parser and dictionaryFile are required"));

    const auto &path = attributes.getString("dictionaryFile");
    auto text = opk::fs::loadText(path);
    if (!text)
        return tl::unexpected(
            OPK_ERROR(text.error().flag,
                      "PaddleOcrRecognitionParser: cannot load dictionary: " + text.error().info));
    // UTF-8 helpers use NUL-terminated strings; reject embedded NUL explicitly.
    if (text->find('\0') != std::string::npos)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrRecognitionParser: dictionary contains NUL"));

    std::istringstream lines(*text);
    std::unordered_set<std::string> seen;
    std::vector<std::string> vocabulary(1); // CTC blank at index zero.
    std::string character;
    size_t line = 0;
    while (std::getline(lines, character)) {
        ++line;
        if (!character.empty() && character.back() == '\r')
            character.pop_back();
        if (opk::utf8::length(character) != 1 ||
            opk::codepoint::decode(character.c_str()) <= U' ' || character == "\x7f" ||
            !seen.insert(character).second)
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidData,
                fmt::format("PaddleOcrRecognitionParser: invalid or duplicate dictionary "
                            "character at {}:{} (expected one UTF-8 character, excluding space)",
                            path,
                            line)));
        vocabulary.push_back(std::move(character));
    }
    if (vocabulary.size() == 1)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "PaddleOcrRecognitionParser: dictionary is empty: " + path));
    vocabulary.emplace_back(" ");
    return vocabulary;
}

} // namespace

opk::Result<void> PaddleOcrRecognitionParser::parse(const opk::TensorParser::Input &input,
                                                    open_perception_kit::FrameResults &results) {
    if (vocabulary.empty()) {
        auto loaded = loadVocabulary(input.attributes);
        if (!loaded)
            return tl::unexpected(loaded.error());
        vocabulary = std::move(*loaded);
    }

    const auto *tensor = input.tensors[0];
    if (!tensor || tensor->getValueType() != opk::Dtype::Float32)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrRecognitionParser: expected Float32 tensor 0"));
    const auto shape = tensor->getShape();
    if (shape.rank != 3 || shape.dims[0] != 1 || shape.dims[1] <= 0 || shape.dims[2] <= 0)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrRecognitionParser: expected resolved [1,T,C]"));
    const auto steps = static_cast<size_t>(shape.dims[1]);
    const auto classes = static_cast<size_t>(shape.dims[2]);
    if (classes != vocabulary.size())
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            fmt::format("PaddleOcrRecognitionParser: expected {} classes (dictionary + blank + "
                        "space), got {}",
                        vocabulary.size(),
                        classes)));
    const auto maximum = std::numeric_limits<size_t>::max();
    if (steps > maximum / classes || steps * classes > maximum / sizeof(float) ||
        !tensor->isValid() || tensor->getCount() != steps * classes ||
        tensor->getByteCount() < steps * classes * sizeof(float))
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "PaddleOcrRecognitionParser: invalid tensor storage"));

    std::string text;
    double confidenceSum = 0.0;
    size_t retained = 0;
    size_t previous = 0;
    for (size_t step = 0; step < steps; ++step) {
        size_t best = 0;
        float bestProbability = -1.0f;
        for (size_t id = 0; id < classes; ++id) {
            // The exported ONNX model already applies softmax.
            const float probability = tensor->get(step * classes + id);
            if (!std::isfinite(probability) || probability < 0.0f || probability > 1.0f)
                return tl::unexpected(OPK_ERROR(
                    opk::ErrorFlag::InvalidData,
                    fmt::format("PaddleOcrRecognitionParser: probability outside [0,1] at [{},{}]",
                                step,
                                id)));
            if (probability > bestProbability) {
                bestProbability = probability;
                best = id;
            }
        }
        // Collapse adjacent repeats before removing blanks, preserving A,blank,A.
        if (best != previous && best != 0) {
            text += vocabulary[best];
            confidenceSum += bestProbability;
            ++retained;
        }
        previous = best;
    }

    auto candidate = std::make_unique<open_perception_kit::metadata::ClassificationCandidateT>();
    candidate->text = std::move(text);
    candidate->confidence = retained == 0 ? 0.0f : static_cast<float>(confidenceSum / retained);
    candidate->class_id = -1;

    auto classification = std::make_unique<open_perception_kit::metadata::ClassificationT>();
    classification->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    classification->candidates.push_back(std::move(candidate));

    open_perception_kit::metadata::ClassificationsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .producer = &input.producerInfo});
    payload.classifications.push_back(std::move(classification));
    results.add(std::move(payload));
    return {};
}

} // namespace opk::stdop::postproc

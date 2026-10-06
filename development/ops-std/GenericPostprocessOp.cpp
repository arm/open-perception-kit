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

#include "GenericPostprocessOp.h"

#include <fmt/core.h>
#include <functional>
#include <map>
#include <memory>

#include "Log.h"
#include "opk/FrameResults.h"
#include "opk/Types.h"
#include <perf/PerformanceMetrics.h>

// parser class headers
#include "postproc/CameraContactParser.h"
#include "postproc/DummyParser.h"
#include "postproc/GazeDetectionParser.h"
#include "postproc/ImageNetClassificationParser.h"
#include "postproc/ModNetSegmentationParser.h"
#include "postproc/ObjectEmbeddingParser.h"
#include "postproc/PaddleocrParser.h"
#include "postproc/PersonClassificationParser.h"
#include "postproc/RvmParser.h"
#include "postproc/ScrfdParser.h"
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"
#include "postproc/YoloXParser.h"
// ... add new parser headers here

using namespace opk::stdop;

namespace {

using ParserCreator = std::function<std::unique_ptr<opk::TensorParser>()>;

template <class T> ParserCreator make() {
    return []() { return std::make_unique<T>(); };
}

// parser registry
const std::map<std::string, ParserCreator> &getParserRegistry() {
    static const std::map<std::string, ParserCreator> registry = {
        {"CameraContactParser", make<opk::stdop::postproc::CameraContactParser>()},
        {"DummyParser", make<opk::stdop::postproc::DummyParser>()},
        {"GazeDetectionParser", make<opk::stdop::postproc::GazeDetectionParser>()},
        {"ImageNetClassificationParser",
         make<opk::stdop::postproc::ImageNetClassificationParser>()},
        {"ModNetSegmentationParser", make<opk::stdop::postproc::ModNetSegmentationParser>()},
        {"ObjectEmbeddingParser", make<opk::stdop::postproc::ObjectEmbeddingParser>()},
        {"PaddleOcrDetectionParser", make<opk::stdop::postproc::PaddleOcrDetectionParser>()},
        {"PersonClassificationParser", make<opk::stdop::postproc::PersonClassificationParser>()},
        {"RvmParser", make<opk::stdop::postproc::RvmParser>()},
        {"ScrfdParser", make<opk::stdop::postproc::ScrfdParser>()},
        {"UltrafaceParser", make<opk::stdop::postproc::UltraFaceParser>()},
        {"YoloXParser", make<opk::stdop::postproc::YoloXParser>()},
        {"YoloParser", make<opk::stdop::postproc::YoloParser>()},
        // ... add new parsers here
    };
    return registry;
}

} // namespace

GenericPostprocessOp::GenericPostprocessOp() = default;
GenericPostprocessOp::~GenericPostprocessOp() = default;

opk::Result<void> GenericPostprocessOp::bind(size_t index, const std::vector<opk::op::Op *> &ops) {
    return {};
}

opk::Result<void> GenericPostprocessOp::configure(const opk::AttributeMap &attributes) {

    this->attributes = attributes.cloneDeep();

    parserName = attributes.getStringOrDefault("parser", "");
    if (instanceId.empty())
        instanceId = fmt::format("GenericPostprocess-{}", index);

    if (parserName.empty()) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        fmt::format("No 'parser' attribute in postprocessor op")));
    }

    const auto &registry = getParserRegistry();
    auto it = registry.find(parserName);
    if (it == registry.end()) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      fmt::format("No tensor parser with name: [{}]", parserName)));
    }

    this->parser = it->second();

    return {};
}

std::vector<std::string_view> GenericPostprocessOp::getProvidedContentTypes() const {
    return parser ? parser->getProvidedContentTypes() : std::vector<std::string_view>{};
}

opk::Result<opk::op::OpSignal>
GenericPostprocessOp::process(opk::op::OpChainContext &opChainContext) {
    OPK_PERF_SCOPE(fmt::format("std/Post/{}", opChainContext.inferenceInfo.modelName));

    opk::TensorParser::Input tensorParserInput(attributes);

    // populate tensors
    for (size_t i = 0; i < opk::MaxTensorCount; i++) {
        if (i < opChainContext.inferenceOutputTensorCount) {
            if (!opChainContext.inferenceOutputTensors[i].isValid()) {
                opk::log::error("Postprocessor input tensor {} is invalid\n", i);
                return tl::unexpected(
                    OPK_ERROR(opk::ErrorFlag::InvalidData,
                              "Postprocessor input tensor " + std::to_string(i) + " is invalid"));
            }
            tensorParserInput.tensors[i] = &opChainContext.inferenceOutputTensors[i];
        } else {
            tensorParserInput.tensors[i] = nullptr;
        }
    }

    // copy active inference info
    tensorParserInput.inferenceInfo = opChainContext.inferenceInfo;
    tensorParserInput.producerInfo = producerInfo(
        opChainContext.inferenceInfo.inferElementId, parserName, "opk-std-ops/GenericPostprocess");

    if (auto parseResult = parser->parse(tensorParserInput, *opChainContext.frameResults);
        !parseResult) {
        return tl::unexpected(parseResult.error());
    }

    return opk::op::OpSignal::Continue;
}

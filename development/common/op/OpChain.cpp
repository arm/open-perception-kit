/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "op/OpChain.h"

#include "Log.h"
#include "Validator.h"
#include "opk/String.h"
#include "tools.h"

#include "op/Op.h"
#include "op/OpChainDescriptor.h"
#include "perf/PerformanceMetrics.h"

#include <algorithm>
#include <span>
#include <string>
#include <unordered_map>
using namespace opk::op;

const std::string &OpChain::getName() const {
    return this->name;
}

const std::string &OpChain::getDisplayName() const {
    return this->displayName;
}

const std::string &OpChain::getTask() const {
    return this->task;
}

const std::string &OpChain::getRuntime() const {
    return this->runtime;
}

namespace {

template <typename Interface, typename Getter>
std::vector<std::string_view> collectContentTypes(std::span<opk::op::Op *const> ops,
                                                  Getter getter) {
    std::vector<std::string_view> result;
    for (const auto *op : ops) {
        const auto *contentOp = op->as<Interface>();
        if (contentOp == nullptr)
            continue;
        for (const auto contentType : getter(*contentOp)) {
            if (!contentType.empty() && std::ranges::find(result, contentType) == result.end())
                result.push_back(contentType);
        }
    }
    return result;
}

} // namespace

std::vector<std::string_view> OpChain::getProvidedContentTypes() const {
    return collectContentTypes<OpInterfacePostprocessor>(
        opPtrs, [](const auto &op) { return op.getProvidedContentTypes(); });
}

std::vector<std::string_view> OpChain::getRequiredContentTypes() const {
    return collectContentTypes<OpInterfaceContentConsumer>(
        opPtrs, [](const auto &op) { return op.getRequiredContentTypes(); });
}

opk::Result<void> OpChain::setupFromDescriptor(const opk::op::OpChainDescriptor &descriptor) {
    if (const auto validation = opk::config::validateOpChainSemantics(descriptor); !validation.ok())
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain, validation.toText()));

    name = descriptor.name;
    displayName = descriptor.displayName;
    task = descriptor.task;
    runtime = descriptor.runtime;
    std::unordered_map<std::string, size_t> occurrences;

    for (size_t opIndex = 0; opIndex < descriptor.ops.size(); ++opIndex) {
        const auto &op = descriptor.ops[opIndex];

        if (opk::utf8::count(op.id, '/') != 1) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          fmt::format("Op id must be library/op, but found: [{}]", op.id)));
        }

        std::string libName = opk::utf8::split(op.id, "/")[0];
        std::string opName = opk::utf8::split(op.id, "/")[1];

        opk::op::OpRef opRef;
        auto bindResult = opRef.bind(libName, opName);
        if (!bindResult) {
            return bindResult;
        }

        opRef->libName = libName;
        opRef->opName = opName;
        opRef->index = opIndex;
        auto &occurrenceCount = occurrences[op.id];
        const size_t occurrence = occurrenceCount;
        ++occurrenceCount;
        opRef->instanceId =
            op.instanceId.empty() ? makeDefaultInstanceId(op.id, occurrence) : op.instanceId;

        opRef->loopId = op.loopId.value_or(0);

        auto configureResult = opRef->configure(op.attributes);
        if (!configureResult) {
            return configureResult;
        }

        add(opRef);
    }

    auto chainBindResult = bind();
    if (!chainBindResult) {
        return tl::unexpected(std::move(chainBindResult.error()));
    }

    opk::log::info("{}", opk::log::tools::enframe(this->toString(), "OpChain"));
    opk::log::notice("OpChain is valid\n");

    return {};
}

opk::Result<void> OpChain::setupFromFile(const std::string &filePath) {
    opk::log::info("Loading OpChain from file: [{}]\n", filePath);
    auto descResult = opk::op::OpChainDescriptor::fromFile(filePath);
    if (!descResult) {
        return tl::unexpected(std::move(descResult.error()));
    }

    return setupFromDescriptor(*descResult);
}

void OpChain::add(opk::op::OpRef &opRef) {
    opRefs.push_back(std::move(opRef));
}

opk::Result<void> OpChain::bind() {
    opPtrs.resize(opRefs.size());
    for (size_t i = 0; i < opRefs.size(); i++) {
        opPtrs[i] = opRefs[i].get();
    }

    for (size_t i = 0; i < opPtrs.size(); i++) {
        auto opBindResult = opPtrs[i]->bind(i, opPtrs);
        if (!opBindResult) {
            return opBindResult;
        }
    }

    return {};
}

opk::Result<void> OpChain::execute(opk::op::OpChainContext &opChainContext) {
    const auto metricName = name.empty() ? std::string("opchain") : fmt::format("opchain/{}", name);
    OPK_PERF_SCOPE(metricName);

    size_t currentIndex = 0;

    auto firstWorkerIndexForLoop = [this](size_t index, size_t loopId) {
        size_t firstGroupIndex = index;
        while (firstGroupIndex > 0 && opPtrs[firstGroupIndex - 1]->loopId == loopId) {
            --firstGroupIndex;
        }

        // Skip the controller op. Loop groups are validated to have at least two ops.
        return firstGroupIndex + 1;
    };

    auto nextIndexAfterLoop = [this](size_t index, size_t loopId) {
        while (index < opPtrs.size() && opPtrs[index]->loopId == loopId) {
            ++index;
        }
        return index;
    };

    while (currentIndex < opPtrs.size()) {
        Op *op = opPtrs[currentIndex];
        const size_t loopId = op->loopId;

        auto result = op->process(opChainContext);
        if (!result) {
            return tl::unexpected(std::move(result.error()));
        }

        switch (*result) {
        case OpSignal::AbortChain:
            return {};
        case OpSignal::BreakLoop:
            currentIndex =
                (loopId == 0) ? currentIndex + 1 : nextIndexAfterLoop(currentIndex, loopId);
            continue;
        case OpSignal::Continue:
            break;
        }

        const bool lastInChain = currentIndex + 1 == opPtrs.size();
        const size_t nextLoopId = lastInChain ? 0 : opPtrs[currentIndex + 1]->loopId;

        if (loopId != 0 && (lastInChain || nextLoopId != loopId)) {
            currentIndex = firstWorkerIndexForLoop(currentIndex, loopId);
        } else {
            ++currentIndex;
        }
    }

    return {};
}

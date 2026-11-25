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

#include "op/PromptAdapter.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace opk {
PreparedPrompt::PreparedPrompt(std::shared_ptr<const PromptSnapshot> snapshot_,
                               const PreparationContract contract_)
    : snapshot(std::move(snapshot_)), contract(contract_) {}

namespace op {
namespace {
Result<void> validateInput(const PromptAdapter &adapter, const PromptSnapshot &snapshot) {
    const auto valid = validatePromptSnapshot(snapshot);
    if (!valid) {
        return valid;
    }
    if (!snapshot.payload) {
        return {};
    }
    const auto *const destination = snapshot.destination();
    if (!destination || !adapter.acceptsPrompt(snapshot.payload->format(),
                                               destination->inference_instance,
                                               destination->source_name)) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Prompt adapter rejects format or destination"));
    }
    return {};
}
} // namespace

PromptPrepareOp::PromptPrepareOp(std::unique_ptr<PromptPreparationAdapter> adapter_)
    : adapter(std::move(adapter_)) {
    if (!adapter) {
        throw std::invalid_argument("Prompt preparation requires an adapter");
    }
}

Result<void> PromptPrepareOp::configure(const AttributeMap &attributes) {
    cached.reset();
    const auto configured = adapter->configure(attributes);
    if (!configured) {
        return configured;
    }
    const auto contract = adapter->preparationContract();
    if (contract.id.empty() || contract.version == 0 || adapter->maxPreparedBytes() == 0 ||
        adapter->maxPreparedBytes() >
            std::numeric_limits<size_t>::max() / PromptController::MaxRetainedRevisions) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      "Prompt preparation needs a stable contract and bounded storage"));
    }
    return {};
}

Result<void> PromptPrepareOp::bind(const size_t index, const std::vector<Op *> &ops) {
    const auto bound = adapter->bind(*this, index, ops);
    if (!bound) {
        cached.reset();
    }
    return bound;
}

bool PromptPrepareOp::acceptsPrompt(const PromptFormat format,
                                    const std::string_view inference,
                                    const std::string_view source) const {
    return adapter->acceptsPrompt(format, inference, source);
}

std::span<const PromptConsumerCapability> PromptPrepareOp::promptCapabilities() const {
    return adapter->promptCapabilities();
}

const PromptPreparationAdapter &PromptPrepareOp::preparationAdapter() const {
    return *adapter;
}

std::shared_ptr<const PreparedPrompt>
PromptPrepareOp::preparedFor(const std::shared_ptr<const PromptSnapshot> &snapshot,
                             const PreparationContract contract) const {
    return cached && cached->snapshot == snapshot && cached->contract == contract ? cached
                                                                                  : nullptr;
}

Result<OpSignal> PromptPrepareOp::process(OpChainContext &context) {
    context.preparedPrompt.reset();
    if (!context.prompt) {
        cached.reset();
        return OpSignal::AbortChain;
    }
    const auto valid = validateInput(*adapter, *context.prompt);
    if (!valid) {
        cached.reset();
        return tl::unexpected(valid.error());
    }
    if (!context.prompt->payload) {
        cached.reset();
        return OpSignal::AbortChain;
    }
    const auto contract = adapter->preparationContract();
    if (preparedFor(context.prompt, contract)) {
        return OpSignal::Continue;
    }
    cached.reset();
    try {
        const auto prepared = adapter->prepare(context.prompt);
        if (!prepared) {
            return tl::unexpected(prepared.error());
        }
        if (!*prepared || (*prepared)->snapshot != context.prompt ||
            (*prepared)->contract != contract || contract.id.empty() || contract.version == 0 ||
            (*prepared)->ownedBytes() > adapter->maxPreparedBytes()) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidData,
                          "Prepared prompt violates snapshot, contract or byte bound"));
        }
        std::erase_if(retained, [](const auto &entry) { return entry.expired(); });
        size_t bytes = (*prepared)->ownedBytes();
        const size_t limit = adapter->maxPreparedBytes() * PromptController::MaxRetainedRevisions;
        for (const auto &entry : retained) {
            const auto owner = entry.lock();
            if (owner && (owner->ownedBytes() > limit - bytes)) {
                return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                                "Retained prepared prompt byte limit exceeded"));
            }
            if (owner) {
                bytes += owner->ownedBytes();
            }
        }
        if (retained.size() >= PromptController::MaxRetainedRevisions) {
            return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                            "Retained prepared prompt revision limit exceeded"));
        }
        retained.emplace_back(*prepared);
        cached = *prepared;
        return OpSignal::Continue;
    } catch (const std::exception &error) {
        context.preparedPrompt.reset();
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, error.what()));
    }
}

PromptInputOp::PromptInputOp(std::unique_ptr<PromptInputAdapter> adapter_)
    : adapter(std::move(adapter_)) {
    if (!adapter) {
        throw std::invalid_argument("Prompt input requires an adapter");
    }
}

Result<void> PromptInputOp::configure(const AttributeMap &attributes) {
    return adapter->configure(attributes);
}

Result<void> PromptInputOp::bind(const size_t index, const std::vector<Op *> &ops) {
    return adapter->bind(*this, index, ops);
}

bool PromptInputOp::acceptsPrompt(const PromptFormat format,
                                  const std::string_view inference,
                                  const std::string_view source) const {
    return adapter->acceptsPrompt(format, inference, source);
}

std::span<const PromptConsumerCapability> PromptInputOp::promptCapabilities() const {
    return adapter->promptCapabilities();
}

InferenceInputBinding PromptInputOp::getInputBinding() const {
    return adapter->getInputBinding();
}

Result<OpSignal> PromptInputOp::process(OpChainContext &context) {
    context.preparedPrompt.reset();
    if (!context.prompt) {
        return OpSignal::AbortChain;
    }
    const auto valid = validateInput(*adapter, *context.prompt);
    if (!valid) {
        return tl::unexpected(valid.error());
    }
    if (!context.prompt->payload) {
        return OpSignal::AbortChain;
    }
    const auto snapshot = context.prompt;
    try {
        const auto written = adapter->writeInputs(context);
        context.preparedPrompt.reset();
        if (!written) {
            return tl::unexpected(written.error());
        }
        if (context.prompt != snapshot) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidData, "Prompt adapter changed invocation identity"));
        }
        context.preparedPrompt = snapshot;
        return OpSignal::Continue;
    } catch (const std::exception &error) {
        context.preparedPrompt.reset();
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, error.what()));
    }
}
} // namespace op
} // namespace opk

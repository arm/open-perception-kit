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

#pragma once

#include "op/Op.h"

namespace opk {

/// Internal prepared-data contract, independent of wire format and tensor shape.
/// IDs have static storage duration, including while old prepared owners survive reconfiguration.
struct PreparationContract {
    std::string_view id;
    uint32_t version = 0;
    bool operator==(const PreparationContract &) const = default;
};

/// Concrete adapters own immutable data; the shared runtime only manages identity/lifetime.
struct PreparedPrompt {
    PreparedPrompt(std::shared_ptr<const PromptSnapshot> snapshot,
                   const PreparationContract contract);
    virtual ~PreparedPrompt() = default;
    /// Additional owned allocation bytes, excluding the separately accounted semantic snapshot.
    virtual size_t ownedBytes() const = 0;
    const std::shared_ptr<const PromptSnapshot> snapshot;
    const PreparationContract contract;
};

struct PreparedPromptProvider {
    virtual ~PreparedPromptProvider() = default;
    virtual std::shared_ptr<const PreparedPrompt>
    preparedFor(const std::shared_ptr<const PromptSnapshot> &snapshot,
                const PreparationContract contract) const = 0;
};

namespace op {

/// Model/export-specific semantics. No transport, cache or inference-consumption policy.
struct PromptAdapter : OpInterfacePromptConsumer {
    virtual Result<void> configure(const AttributeMap &attributes) = 0;
    virtual Result<void>
    bind(const Op &owner, const size_t index, const std::vector<Op *> &ops) = 0;
};

/// Optional revision work; direct geometric adapters do not implement this interface.
struct PromptPreparationAdapter : PromptAdapter {
    virtual PreparationContract preparationContract() const = 0;
    virtual size_t maxPreparedBytes() const = 0;
    virtual Result<std::shared_ptr<const PreparedPrompt>>
    prepare(const std::shared_ptr<const PromptSnapshot> &snapshot) = 0;
};

/// Invocation conversion writes only the inputs declared by getInputBinding().
struct PromptInputAdapter : PromptAdapter, OpInterfaceInferenceInputPreprocessor {
    virtual Result<void> writeInputs(OpChainContext &context) = 0;
};

class PromptPrepareOp : public Op, public OpInterfacePromptConsumer, public PreparedPromptProvider {
  public:
    explicit PromptPrepareOp(std::unique_ptr<PromptPreparationAdapter> adapter);
    Result<void> configure(const AttributeMap &attributes) final;
    Result<void> bind(const size_t index, const std::vector<Op *> &ops) final;
    Result<OpSignal> process(OpChainContext &context) final;
    bool acceptsPrompt(const PromptFormat format,
                       const std::string_view inference,
                       const std::string_view source) const final;
    std::span<const PromptConsumerCapability> promptCapabilities() const final;
    std::shared_ptr<const PreparedPrompt>
    preparedFor(const std::shared_ptr<const PromptSnapshot> &snapshot,
                const PreparationContract contract) const final;

  protected:
    const PromptPreparationAdapter &preparationAdapter() const;

  private:
    std::unique_ptr<PromptPreparationAdapter> adapter;
    std::shared_ptr<const PreparedPrompt> cached;
    std::vector<std::weak_ptr<const PreparedPrompt>> retained;
};

/// Current visual adapters skip the chain when no payload is present.
class PromptInputOp : public Op,
                      public OpInterfaceCpuPreprocessor,
                      public OpInterfacePromptConsumer,
                      public OpInterfaceInferenceInputPreprocessor {
  public:
    explicit PromptInputOp(std::unique_ptr<PromptInputAdapter> adapter);
    Result<void> configure(const AttributeMap &attributes) final;
    Result<void> bind(const size_t index, const std::vector<Op *> &ops) final;
    Result<OpSignal> process(OpChainContext &context) final;
    bool acceptsPrompt(const PromptFormat format,
                       const std::string_view inference,
                       const std::string_view source) const final;
    std::span<const PromptConsumerCapability> promptCapabilities() const final;
    InferenceInputBinding getInputBinding() const final;

  private:
    std::unique_ptr<PromptInputAdapter> adapter;
};
} // namespace op
} // namespace opk

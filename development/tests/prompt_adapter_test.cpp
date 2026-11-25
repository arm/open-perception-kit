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
#include "opk/TensorView.h"
#include <array>
#include <gtest/gtest.h>
#include <stdexcept>

namespace {
using namespace opk;
using namespace opk::op;
inline constexpr PromptFormat TokenFormat{"TOKN", 1, 0};
inline constexpr PreparationContract TokenContract{"test-int64-tokens-and-mask", 1};

struct TokenPayload final : PromptPayload {
    explicit TokenPayload(const PromptFormat format_ = TokenFormat) : kind(format_) {}
    PromptFormat format() const override {
        return kind;
    }
    size_t ownedBytes() const override {
        return sizeof(*this);
    }
    const PromptFormat kind;
};

std::shared_ptr<const PromptSnapshot> snapshot(const uint64_t revision = 1,
                                               const PromptFormat format = TokenFormat) {
    open_perception_kit::metadata::RequestEnvelopeT request;
    request.request_id = 1;
    request.revision = revision;
    request.destination = std::make_unique<open_perception_kit::metadata::RequestDestinationT>();
    request.destination->inference_instance = "detector";
    request.destination->source_name = "pipelineVideoFrame";
    return std::make_shared<const PromptSnapshot>(
        "session", std::move(request), std::make_shared<const TokenPayload>(format));
}

struct TokenState final : PreparedPrompt {
    TokenState(std::shared_ptr<const PromptSnapshot> snapshot_,
               const PreparationContract contract_ = TokenContract,
               const size_t bytes_ = 64)
        : PreparedPrompt(std::move(snapshot_), contract_), bytes(bytes_) {}
    size_t ownedBytes() const override {
        return bytes;
    }
    const std::array<int64_t, 3> tokens{17, 23, 0};
    const std::array<uint8_t, 3> mask{1, 1, 0};
    const size_t bytes;
};

class TokenPreparation final : public PromptPreparationAdapter {
  public:
    enum class Fault { None, Identity, Contract, Size, Error, Exception };
    Result<void> configure(const AttributeMap &) override {
        return {};
    }
    Result<void> bind(const Op &, const size_t, const std::vector<Op *> &) override {
        if (failBinding) {
            return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, "binding failed"));
        }
        return {};
    }

    bool acceptsPrompt(const PromptFormat format,
                       const std::string_view inference,
                       const std::string_view source) const override {
        return format == TokenFormat && inference == "detector" && source == "pipelineVideoFrame";
    }
    std::span<const PromptConsumerCapability> promptCapabilities() const override {
        static constexpr std::array capabilities{
            PromptConsumerCapability{TokenFormat, "test-int64-tokens-and-mask", {1}, 3}};
        return capabilities;
    }
    PreparationContract preparationContract() const override {
        return TokenContract;
    }
    size_t maxPreparedBytes() const override {
        return limit;
    }
    Result<std::shared_ptr<const PreparedPrompt>>
    prepare(const std::shared_ptr<const PromptSnapshot> &prompt) override {
        ++calls;
        if (fault == Fault::Error) {
            return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, "test preparation failure"));
        }
        if (fault == Fault::Exception) {
            throw std::runtime_error("test preparation exception");
        }
        return std::make_shared<const TokenState>(
            fault == Fault::Identity ? snapshot(99) : prompt,
            fault == Fault::Contract ? PreparationContract{"other", 1} : TokenContract,
            fault == Fault::Size ? limit + 1 : bytes);
    }
    size_t calls = 0;
    size_t limit = 64;
    size_t bytes = 64;
    Fault fault = Fault::None;
    bool failBinding = false;
};

class TokenWriter final : public PromptInputAdapter {
  public:
    explicit TokenWriter(const PreparedPromptProvider *const provider_) : provider(provider_) {}
    Result<void> configure(const AttributeMap &) override {
        return {};
    }
    Result<void> bind(const Op &, const size_t, const std::vector<Op *> &) override {
        return {};
    }
    bool acceptsPrompt(const PromptFormat format,
                       const std::string_view inference,
                       const std::string_view source) const override {
        return format == TokenFormat && inference == "detector" && source == "pipelineVideoFrame";
    }
    InferenceInputBinding getInputBinding() const override {
        static constexpr std::array<size_t, 2> indices{1, 2};
        return {"detector", "pipelineVideoFrame", indices};
    }
    Result<void> writeInputs(OpChainContext &context) override {
        ++calls;
        if (changeSnapshot) {
            context.prompt = snapshot(99);
            return {};
        }
        sawClearMarker = !context.preparedPrompt;
        if (fail) {
            context.preparedPrompt = context.prompt; // Deliberately misbehaving adapter.
            return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, "write failed"));
        }
        const auto state = provider->preparedFor(context.prompt, TokenContract);
        const auto tokens = std::dynamic_pointer_cast<const TokenState>(state);
        if (!tokens) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidData, "wrong prepared representation"));
        }
        outputTokens = tokens->tokens;
        outputMask = tokens->mask;
        return {};
    }
    const PreparedPromptProvider *const provider;
    std::array<int64_t, 3> outputTokens{};
    std::array<uint8_t, 3> outputMask{};
    size_t calls = 0;
    bool sawClearMarker = false;
    bool fail = false;
    bool changeSnapshot = false;
};

struct Fixture {
    Fixture()
        : adapter(std::make_unique<TokenPreparation>()), native(adapter.get()),
          prepare(std::move(adapter)), writerAdapter(std::make_unique<TokenWriter>(&prepare)),
          writer(writerAdapter.get()), input(std::move(writerAdapter)) {
        EXPECT_TRUE(prepare.configure({}));
        context.prompt = snapshot();
    }
    std::unique_ptr<TokenPreparation> adapter;
    TokenPreparation *const native;
    PromptPrepareOp prepare;
    std::unique_ptr<TokenWriter> writerAdapter;
    TokenWriter *const writer;
    PromptInputOp input;
    OpChainContext context;
};

TEST(PromptAdapter, SubstitutesInt64TokensAndMaskThroughSharedPreparationAndInputLifecycle) {
    Fixture f;
    ASSERT_TRUE(f.prepare.process(f.context));
    EXPECT_FALSE(f.context.preparedPrompt);
    ASSERT_TRUE(f.input.process(f.context));
    EXPECT_TRUE(f.writer->sawClearMarker);
    EXPECT_EQ(f.context.preparedPrompt, f.context.prompt);
    EXPECT_EQ(f.writer->outputTokens, (std::array<int64_t, 3>{17, 23, 0}));
    EXPECT_EQ(f.writer->outputMask, (std::array<uint8_t, 3>{1, 1, 0}));
    const TensorView tokens(f.writer->outputTokens.data(),
                            sizeof(f.writer->outputTokens),
                            Shape(1, 3),
                            Dtype::Int64,
                            1,
                            0);
    EXPECT_TRUE(tokens.isValid());
    EXPECT_EQ(tokens.getValueType(), Dtype::Int64);
    ASSERT_TRUE(f.prepare.process(f.context));
    EXPECT_EQ(f.native->calls, 1);
    EXPECT_FALSE(f.context.preparedPrompt);
    EXPECT_FALSE(f.prepare.preparedFor(f.context.prompt, {"wrong", 1}));
    EXPECT_FALSE(f.prepare.preparedFor(snapshot(), TokenContract));
}

TEST(PromptAdapter, SuccessfulRebindPreservesCacheButBindingFailureInvalidatesIt) {
    Fixture f;
    ASSERT_TRUE(f.prepare.process(f.context));
    ASSERT_TRUE(f.prepare.bind(0, {}));
    EXPECT_TRUE(f.prepare.preparedFor(f.context.prompt, TokenContract));
    f.native->failBinding = true;
    EXPECT_FALSE(f.prepare.bind(0, {}));
    EXPECT_FALSE(f.prepare.preparedFor(f.context.prompt, TokenContract));
}

TEST(PromptAdapter, RejectsBadPreparationWithoutPublishingOrReusingOldState) {
    for (const auto fault : {TokenPreparation::Fault::Identity,
                             TokenPreparation::Fault::Contract,
                             TokenPreparation::Fault::Size,
                             TokenPreparation::Fault::Error,
                             TokenPreparation::Fault::Exception}) {
        Fixture f;
        ASSERT_TRUE(f.prepare.process(f.context));
        const auto previous = f.context.prompt;
        const auto retained = f.prepare.preparedFor(previous, TokenContract);
        f.context.prompt = snapshot(2);
        f.context.preparedPrompt = previous;
        f.native->fault = fault;
        EXPECT_FALSE(f.prepare.process(f.context));
        EXPECT_FALSE(f.context.preparedPrompt);
        EXPECT_FALSE(f.prepare.preparedFor(previous, TokenContract));
        EXPECT_FALSE(f.prepare.preparedFor(f.context.prompt, TokenContract));
        ASSERT_TRUE(retained);
        EXPECT_EQ(retained->snapshot, previous);
    }
}

TEST(PromptAdapter, AbsenceAndReconfigureInvalidateCacheWithoutInvalidatingBorrowers) {
    Fixture f;
    ASSERT_TRUE(f.prepare.process(f.context));
    const auto first = f.context.prompt;
    const auto retained = f.prepare.preparedFor(first, TokenContract);
    f.context.prompt.reset();
    ASSERT_EQ(*f.prepare.process(f.context), OpSignal::AbortChain);
    EXPECT_FALSE(f.prepare.preparedFor(first, TokenContract));
    f.context.prompt = first;
    ASSERT_TRUE(f.prepare.process(f.context));
    ASSERT_TRUE(f.prepare.configure({}));
    EXPECT_FALSE(f.prepare.preparedFor(first, TokenContract));
    EXPECT_EQ(std::dynamic_pointer_cast<const TokenState>(retained)->tokens[0], 17);
    ASSERT_TRUE(f.prepare.process(f.context));
    EXPECT_EQ(f.native->calls, 3);
}

TEST(PromptAdapter, RejectsUnsupportedSemanticFormatBeforeCallingAdapterDespiteCompatibleData) {
    Fixture f;
    ASSERT_TRUE(f.prepare.process(f.context));
    f.context.prompt = snapshot(2, {"OTHER", 1, 0});
    EXPECT_FALSE(f.prepare.process(f.context));
    EXPECT_EQ(f.native->calls, 1);
    EXPECT_FALSE(f.input.process(f.context));
    EXPECT_EQ(f.writer->calls, 0);
    EXPECT_FALSE(f.context.preparedPrompt);
}

TEST(PromptAdapter, InputFailureClearsEvenAnIncorrectlyWrittenMarker) {
    Fixture f;
    ASSERT_TRUE(f.prepare.process(f.context));
    f.writer->fail = true;
    EXPECT_FALSE(f.input.process(f.context));
    EXPECT_FALSE(f.context.preparedPrompt);
    f.context.prompt.reset();
    ASSERT_EQ(*f.input.process(f.context), OpSignal::AbortChain);
    EXPECT_EQ(f.writer->calls, 1);
}

TEST(PromptAdapter, RejectsAdapterThatChangesInvocationSnapshot) {
    Fixture f;
    ASSERT_TRUE(f.prepare.process(f.context));
    f.writer->changeSnapshot = true;
    EXPECT_FALSE(f.input.process(f.context));
    EXPECT_FALSE(f.context.preparedPrompt);
}

struct ForeignState final : PreparedPrompt {
    explicit ForeignState(std::shared_ptr<const PromptSnapshot> snapshot_)
        : PreparedPrompt(std::move(snapshot_), TokenContract) {}
    size_t ownedBytes() const override {
        return sizeof(*this);
    }
};

struct ForeignProvider final : PreparedPromptProvider {
    std::shared_ptr<const PreparedPrompt>
    preparedFor(const std::shared_ptr<const PromptSnapshot> &snapshot_,
                const PreparationContract) const override {
        return std::make_shared<const ForeignState>(snapshot_);
    }
};

TEST(PromptAdapter, ConcreteAdapterRejectsWrongRepresentationWithMatchingContractIdentity) {
    const ForeignProvider provider;
    PromptInputOp input(std::make_unique<TokenWriter>(&provider));
    OpChainContext context;
    context.prompt = snapshot();
    EXPECT_FALSE(input.process(context));
    EXPECT_FALSE(context.preparedPrompt);
}

TEST(PromptAdapter, RetainedRevisionLimitDoesNotInvalidateBorrowedData) {
    Fixture f;
    std::vector<std::shared_ptr<const PreparedPrompt>> borrowers;
    for (size_t i = 0; i < PromptController::MaxRetainedRevisions; ++i) {
        f.context.prompt = snapshot(i + 1);
        ASSERT_TRUE(f.prepare.process(f.context));
        borrowers.push_back(f.prepare.preparedFor(f.context.prompt, TokenContract));
    }
    f.context.prompt = snapshot(99);
    EXPECT_FALSE(f.prepare.process(f.context));
    EXPECT_EQ(std::dynamic_pointer_cast<const TokenState>(borrowers.front())->tokens[1], 23);
    borrowers.clear();
    EXPECT_TRUE(f.prepare.process(f.context));
}

TEST(PromptAdapter, RetainedByteLimitIncludesOwnersFromBeforeReconfigure) {
    Fixture f;
    ASSERT_TRUE(f.prepare.process(f.context));
    const auto retained = f.prepare.preparedFor(f.context.prompt, TokenContract);
    f.native->limit = 1;
    f.native->bytes = 1;
    ASSERT_TRUE(f.prepare.configure({}));
    f.context.prompt = snapshot(2);
    EXPECT_FALSE(f.prepare.process(f.context));
    EXPECT_FALSE(f.prepare.preparedFor(f.context.prompt, TokenContract));
    EXPECT_EQ(retained->ownedBytes(), 64);
}

TEST(PromptAdapter, DetailedCapabilitiesAreOptionalAndSeparateFromCodecSupport) {
    Fixture f;
    const auto capabilities = f.prepare.promptCapabilities();
    ASSERT_EQ(capabilities.size(), 1);
    EXPECT_EQ(capabilities[0].format, TokenFormat);
    EXPECT_EQ(capabilities[0].maxTokens, 3);
    EXPECT_TRUE(f.input.promptCapabilities().empty());
    EXPECT_TRUE(f.input.acceptsPrompt(TokenFormat, "detector", "pipelineVideoFrame"));
}
} // namespace

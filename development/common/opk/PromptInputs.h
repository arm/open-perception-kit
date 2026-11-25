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

#include "opk/FrameResults.h"
#include "opk/Result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace opk {

/// Stable payload contract; identifiers must have static storage duration.
struct PromptFormat {
    std::string_view id;
    uint16_t major = 0;
    uint16_t minor = 0;

    bool operator==(const PromptFormat &) const = default;
};

/// Advertised semantic limits; zero means unspecified, not unlimited.
/// Codecs and model consumers own independent values and validate their own boundaries.
struct PromptLimits {
    size_t maxGroups = 0;
    size_t maxReferences = 0;
    size_t maxReferenceDimension = 0;
    size_t maxReferenceBytes = 0;
    size_t maxDescriptionBytes = 0;

    bool operator==(const PromptLimits &) const = default;
};

/// Immutable semantic input. Concrete implementations own any referenced media.
struct PromptPayload {
    virtual ~PromptPayload();
    virtual PromptFormat format() const = 0;
    /// Bytes owned by this payload, including transitively retained semantic storage.
    virtual size_t ownedBytes() const = 0;
};

class PromptSnapshot {
  public:
    PromptSnapshot(std::string sessionId,
                   open_perception_kit::metadata::RequestEnvelopeT request,
                   std::shared_ptr<const PromptPayload> payload);

    uint64_t requestId() const;
    uint64_t revision() const;
    open_perception_kit::metadata::RequestApplication application() const;
    const open_perception_kit::metadata::RequestDestinationT *destination() const;
    /// Detached deep copy for serialization; the owned generated envelope stays private.
    open_perception_kit::metadata::RequestEnvelopeT copyRequest() const;

    std::string sessionId;
    std::shared_ptr<const PromptPayload> payload;

  private:
    open_perception_kit::metadata::RequestEnvelopeT request;
};

Result<void> validatePromptRequest(const open_perception_kit::metadata::RequestEnvelopeT &request);
Result<void> validatePromptSnapshot(const PromptSnapshot &snapshot);

/// Called by the result producer only for outputs that used this prompt.
void appendPromptAssociation(open_perception_kit::FrameResults &results,
                             const PromptSnapshot &snapshot,
                             const open_perception_kit::metadata::LayerInfoT &layer,
                             const uint64_t parentId);

/// One controller per destination; immutable snapshots outlive updates and session end.
class PromptController {
  public:
    static constexpr size_t MaxNameBytes = 128;
    // Defensive ingress limits bound history and queued revisions; they are not measured device
    // budgets.
    static constexpr size_t MaxRequestsPerSession = 4096;
    static constexpr size_t MaxEndedSessions = 1024;
    static constexpr size_t MaxRetainedRevisions = 8;
    static constexpr size_t MaxRetainedBytes = 64 * 1024 * 1024;

    Result<void> submit(PromptSnapshot prompt);
    Result<void>
    cancel(const std::string &session, const uint64_t requestId, const uint64_t revision);
    Result<void> endSession(const std::string &session);
    std::shared_ptr<const PromptSnapshot> snapshot() const;

  private:
    mutable std::mutex mutex;
    std::string controller;
    std::set<std::string> endedSessions;
    std::set<uint64_t> usedRequestIds;
    std::shared_ptr<const PromptSnapshot> active;
    std::vector<std::weak_ptr<const PromptSnapshot>> retained;
    std::vector<std::weak_ptr<const PromptPayload>> retainedPayloads;
};

} // namespace opk

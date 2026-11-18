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

#include "opk/PromptInputs.h"

#include <algorithm>
#include <utility>

namespace opk {
namespace {

Result<void> invalid(const std::string &reason) {
    return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, reason));
}

bool validName(const std::string_view name) {
    return !name.empty() && name.size() <= PromptController::MaxNameBytes &&
           name.find('\0') == std::string_view::npos;
}

Result<void>
validateIdentity(const uint64_t requestId,
                 const uint64_t revision,
                 const open_perception_kit::metadata::RequestApplication application,
                 const open_perception_kit::metadata::RequestDestinationT *const destination) {
    if (!destination || requestId == 0 || revision == 0 ||
        application != open_perception_kit::metadata::RequestApplication::UntilReplaced) {
        return invalid(
            "Prompt requires a destination, nonzero uint64 IDs/revisions and UntilReplaced");
    }
    if (!validName(destination->inference_instance) || !validName(destination->source_name)) {
        return invalid("Invalid prompt inference/source binding");
    }
    return {};
}

} // namespace

PromptPayload::~PromptPayload() = default;

PromptSnapshot::PromptSnapshot(std::string sessionId,
                               open_perception_kit::metadata::RequestEnvelopeT request,
                               std::shared_ptr<const PromptPayload> payload)
    : sessionId(std::move(sessionId)), payload(std::move(payload)), request(std::move(request)) {}

uint64_t PromptSnapshot::requestId() const {
    return request.request_id;
}

uint64_t PromptSnapshot::revision() const {
    return request.revision;
}

open_perception_kit::metadata::RequestApplication PromptSnapshot::application() const {
    return request.application;
}

const open_perception_kit::metadata::RequestDestinationT *PromptSnapshot::destination() const {
    return request.destination.get();
}

open_perception_kit::metadata::RequestEnvelopeT PromptSnapshot::copyRequest() const {
    return request;
}

Result<void> validatePromptRequest(const open_perception_kit::metadata::RequestEnvelopeT &request) {
    return validateIdentity(
        request.request_id, request.revision, request.application, request.destination.get());
}

Result<void> validatePromptSnapshot(const PromptSnapshot &snapshot) {
    if (!validName(snapshot.sessionId)) {
        return invalid("Invalid controlling session ID");
    }
    if (snapshot.payload) {
        const auto format = snapshot.payload->format();
        if (!validName(format.id) || format.major == 0) {
            return invalid("Prompt requires a named, versioned payload format");
        }
    }
    return validateIdentity(
        snapshot.requestId(), snapshot.revision(), snapshot.application(), snapshot.destination());
}

void appendPromptAssociation(open_perception_kit::FrameResults &results,
                             const PromptSnapshot &snapshot,
                             const open_perception_kit::metadata::LayerInfoT &layer,
                             const uint64_t parentId) {
    open_perception_kit::metadata::PromptAssociationT association;
    association.layer = std::make_unique<open_perception_kit::metadata::LayerInfoT>(layer);
    auto request = snapshot.copyRequest();
    association.request =
        std::make_unique<open_perception_kit::metadata::RequestEnvelopeT>(std::move(request));
    association.session_id = snapshot.sessionId;
    association.parent_id = parentId;
    results.add(std::move(association));
}

Result<void> PromptController::submit(PromptSnapshot prompt) {
    const auto validation = validatePromptSnapshot(prompt);
    if (!validation) {
        return validation;
    }
    const auto &session = prompt.sessionId;
    const std::lock_guard lock(mutex);
    if (endedSessions.contains(session) || (!controller.empty() && controller != session)) {
        return invalid("Session ended or destination belongs to another controller");
    }
    if (controller.empty() && endedSessions.size() >= MaxEndedSessions) {
        return invalid("Session history limit reached; recreate opkprompt");
    }
    const auto *const destination = prompt.destination();
    if (active && active->requestId() == prompt.requestId()) {
        const auto &previousDestination = *active->destination();
        if (previousDestination.inference_instance != destination->inference_instance ||
            previousDestination.source_name != destination->source_name) {
            return invalid("Changing a request destination requires a new request ID");
        }
        if (prompt.revision() <= active->revision()) {
            return invalid("Replacement revision must increase strictly");
        }
    } else {
        if (prompt.revision() != 1 || usedRequestIds.contains(prompt.requestId())) {
            return invalid("New request IDs require revision 1 and cannot be reused in a session");
        }
        if (usedRequestIds.size() >= MaxRequestsPerSession) {
            return invalid("Request history limit reached; end the controlling session");
        }
    }
    std::erase_if(retained, [](const auto &revision) { return revision.expired(); });
    if (retained.size() >= MaxRetainedRevisions) {
        return invalid("Too many prompt revisions retained by downstream work");
    }
    controller = session;
    auto next = std::make_shared<const PromptSnapshot>(std::move(prompt));
    usedRequestIds.insert(next->requestId());
    active = std::move(next);
    retained.emplace_back(active);
    return {};
}

Result<void> PromptController::cancel(const std::string &session,
                                      const uint64_t requestId,
                                      const uint64_t revision) {
    const std::lock_guard lock(mutex);
    if (session != controller || !active || active->requestId() != requestId ||
        active->revision() != revision) {
        return invalid("Cancel must identify the controlling session's current request/revision");
    }
    active.reset();
    return {};
}

Result<void> PromptController::endSession(const std::string &session) {
    const std::lock_guard lock(mutex);
    if (!validName(session) || session != controller) {
        return invalid("Session end must identify the destination's controlling session");
    }
    endedSessions.insert(controller);
    active.reset();
    controller.clear();
    usedRequestIds.clear();
    return {};
}

std::shared_ptr<const PromptSnapshot> PromptController::snapshot() const {
    const std::lock_guard lock(mutex);
    return active && active->payload ? active : nullptr;
}

} // namespace opk

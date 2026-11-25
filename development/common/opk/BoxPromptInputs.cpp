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

#include "opk/BoxPromptInputs.h"

#include <cmath>
#include <utility>

namespace opk {
namespace {

Result<void> invalid(const std::string &reason) {
    return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, reason));
}

Result<void> validateBox(const open_perception_kit::metadata::PromptBoxT &box) {
    if (!std::isfinite(box.left) || !std::isfinite(box.top) || !std::isfinite(box.right) ||
        !std::isfinite(box.bottom) || box.left < 0.0 || box.top < 0.0 || box.right > 1.0 ||
        box.bottom > 1.0 || box.left >= box.right || box.top >= box.bottom) {
        return invalid("Box must have finite ordered normalized coordinates in [0,1]");
    }
    return {};
}

} // namespace

BoxPromptPayload::BoxPromptPayload(const open_perception_kit::metadata::PromptBoxT &box)
    : box(box) {}

BoxPromptPayload::~BoxPromptPayload() = default;

PromptFormat BoxPromptPayload::format() const {
    return BoxPromptFormat;
}

size_t BoxPromptPayload::ownedBytes() const {
    return sizeof(*this);
}

Result<open_perception_kit::metadata::BoxPromptInputsT>
decodeBoxPrompt(const std::span<const uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > MaxBoxPromptRequestBytes) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Prompt request exceeds size limit"));
    }
    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!open_perception_kit::metadata::VerifyBoxPromptInputsBuffer(verifier)) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Malformed BoxPromptInputs packet"));
    }
    auto request = std::unique_ptr<open_perception_kit::metadata::BoxPromptInputsT>(
        open_perception_kit::metadata::GetBoxPromptInputs(bytes.data())->UnPack());
    return std::move(*request);
}

Result<void> validateBoxPrompt(const open_perception_kit::metadata::BoxPromptInputsT &request,
                               const std::string &inferenceInstance,
                               const std::string &sourceName) {
    using namespace open_perception_kit::metadata;
    if (request.schema_major != 1 || request.schema_minor != 0 || !request.request ||
        !request.request->destination) {
        return invalid("Prompt requires schema 1.0 and a request destination");
    }
    const auto envelopeValidation = validatePromptRequest(*request.request);
    if (!envelopeValidation) {
        return envelopeValidation;
    }
    const auto &destination = *request.request->destination;
    if (destination.inference_instance != inferenceInstance ||
        destination.source_name != sourceName) {
        return invalid("Prompt destination does not match this inference/source binding");
    }
    if (request.payload) {
        if (!request.payload->box) {
            return invalid("Present box payload requires box geometry");
        }
        return validateBox(*request.payload->box);
    }
    return {};
}

Result<void> validateBoxPrompt(const PromptSnapshot &snapshot,
                               const std::string &inferenceInstance,
                               const std::string &sourceName) {
    const auto validation = validatePromptSnapshot(snapshot);
    if (!validation) {
        return validation;
    }
    const auto &destination = *snapshot.destination();
    if (destination.inference_instance != inferenceInstance ||
        destination.source_name != sourceName) {
        return invalid("Prompt destination does not match this inference/source binding");
    }
    if (!snapshot.payload) {
        return {};
    }
    const auto *const payload = dynamic_cast<const BoxPromptPayload *>(snapshot.payload.get());
    if (snapshot.payload->format() != BoxPromptFormat || !payload) {
        return invalid("BoxPromptPreprocess requires a supported box payload type/version");
    }
    return validateBox(payload->box);
}

Result<PromptSnapshot>
makeBoxPromptSnapshot(const std::string &session,
                      open_perception_kit::metadata::BoxPromptInputsT request,
                      const std::string &inferenceInstance,
                      const std::string &sourceName) {
    const auto validation = validateBoxPrompt(request, inferenceInstance, sourceName);
    if (!validation) {
        return tl::unexpected(validation.error());
    }
    std::shared_ptr<const PromptPayload> payload;
    if (request.payload) {
        payload = std::make_shared<const BoxPromptPayload>(*request.payload->box);
    }
    return PromptSnapshot{session, std::move(*request.request), std::move(payload)};
}

} // namespace opk

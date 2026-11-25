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

#include "opk/QueryPromptInputs.h"

#include <cmath>
#include <glib.h>
#include <set>
#include <string_view>

namespace opk {
namespace {
using namespace open_perception_kit::metadata;

Result<void> invalid(const std::string &reason) {
    return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, reason));
}

bool validString(const flatbuffers::String *const value,
                 const size_t maximum,
                 const bool optional = false) {
    if (!value) {
        return optional;
    }
    const std::string_view text(value->c_str(), value->size());
    return (optional || !text.empty()) && text.size() <= maximum &&
           text.find('\0') == std::string_view::npos &&
           g_utf8_validate(text.data(), static_cast<gssize>(text.size()), nullptr);
}

Result<void> validateEnvelope(const RequestEnvelope *const request,
                              const std::string &inferenceInstance,
                              const std::string &sourceName) {
    if (!request || !request->destination() || request->request_id() == 0 ||
        request->revision() == 0 || request->application() != RequestApplication::UntilReplaced) {
        return invalid("Prompt requires nonzero IDs, UntilReplaced and a destination");
    }
    const auto *const destination = request->destination();
    if (!validString(destination->inference_instance(), PromptController::MaxNameBytes) ||
        !validString(destination->source_name(), PromptController::MaxNameBytes) ||
        destination->inference_instance()->string_view() != inferenceInstance ||
        destination->source_name()->string_view() != sourceName) {
        return invalid("Prompt destination does not match the inference/source binding");
    }
    return {};
}

template <typename Groups> Result<void> validateGroups(const Groups *const groups) {
    if (!groups || groups->size() == 0 || groups->size() > MaxQueryPromptGroups) {
        return invalid("Present query payload requires a bounded nonempty group list");
    }
    std::set<std::string_view> identities;
    for (const auto *const group : *groups) {
        if (!group || !validString(group->group_id(), MaxPromptGroupNameBytes) ||
            !validString(group->display_name(), MaxPromptGroupNameBytes, true) ||
            !identities.insert(group->group_id()->string_view()).second) {
            return invalid(
                "Prompt groups require unique nonempty UTF-8 IDs and valid display names");
        }
    }
    return {};
}

Result<void> validatePayload(const open_perception_kit::metadata::TextPromptPayload &payload) {
    const auto validation = validateGroups(payload.groups());
    if (!validation) {
        return validation;
    }
    for (const auto *const group : *payload.groups()) {
        if (!validString(group->description(), MaxPromptDescriptionBytes)) {
            return invalid("Text descriptions must be nonempty bounded UTF-8 without NUL");
        }
        // Whitespace-only descriptions do not specify a category.
        const char *cursor = group->description()->c_str();
        bool hasContent = false;
        while (*cursor != '\0') {
            if (!g_unichar_isspace(g_utf8_get_char(cursor))) {
                hasContent = true;
            }
            cursor = g_utf8_next_char(cursor);
        }
        if (!hasContent) {
            return invalid("Text description must contain a category");
        }
    }
    return {};
}

Result<void> validatePayload(const open_perception_kit::metadata::VisualPromptPayload &payload) {
    const auto validation = validateGroups(payload.groups());
    if (!validation) {
        return validation;
    }
    const auto *const references = payload.references();
    if (!references || references->size() == 0 || references->size() > MaxPromptReferences) {
        return invalid("Visual prompts require a bounded nonempty reference list");
    }
    size_t totalBytes = 0;
    for (const auto *const reference : *references) {
        if (!reference || reference->encoding() != PromptReferenceEncoding::RGB8 ||
            reference->width() == 0 || reference->height() == 0 ||
            reference->width() > MaxPromptReferenceDimension ||
            reference->height() > MaxPromptReferenceDimension || !reference->pixels()) {
            return invalid("Reference must be bounded upright packed RGB8");
        }
        // Dimension checks precede multiplication; even a 32-bit size_t can represent this.
        const size_t bytes = size_t{reference->width()} * reference->height() * 3;
        if (reference->pixels()->size() != bytes || bytes > MaxPromptReferenceBytes - totalBytes) {
            return invalid(
                "Reference RGB byte count is inconsistent or exceeds the aggregate budget");
        }
        // Count aliases too: a repeated table must not amplify future preparation allocations.
        totalBytes += bytes;
    }
    for (const auto *const group : *payload.groups()) {
        const auto *const box = group->box();
        if (group->reference_index() >= references->size() || !box || !std::isfinite(box->left()) ||
            !std::isfinite(box->top()) || !std::isfinite(box->right()) ||
            !std::isfinite(box->bottom()) || box->left() < 0 || box->top() < 0 ||
            box->right() > 1 || box->bottom() > 1 || box->left() >= box->right() ||
            box->top() >= box->bottom()) {
            return invalid(
                "Visual group requires an included reference and a finite normalized box");
        }
    }
    return {};
}

template <typename Payload>
Result<void> validateSnapshot(const PromptSnapshot &snapshot,
                              const PromptFormat format,
                              const std::string &inferenceInstance,
                              const std::string &sourceName) {
    const auto validation = validatePromptSnapshot(snapshot);
    if (!validation) {
        return validation;
    }
    if (snapshot.destination()->inference_instance != inferenceInstance ||
        snapshot.destination()->source_name != sourceName) {
        return invalid("Prompt destination does not match the inference/source binding");
    }
    if (!snapshot.payload) {
        return {};
    }
    const auto *const payload = dynamic_cast<const Payload *>(snapshot.payload.get());
    if (!payload || payload->format() != format) {
        return invalid("Unsupported semantic prompt type/version");
    }
    return validatePayload(payload->value());
}
} // namespace

TextPromptPayload::TextPromptPayload(const std::span<const uint8_t> bytes)
    : storage(bytes.begin(), bytes.end()) {}

PromptFormat TextPromptPayload::format() const {
    return TextPromptFormat;
}

const open_perception_kit::metadata::TextPromptPayload &TextPromptPayload::value() const {
    return *open_perception_kit::metadata::GetTextPromptInputs(storage.data())->payload();
}

size_t TextPromptPayload::ownedBytes() const {
    return sizeof(*this) + storage.capacity();
}

Result<PromptSnapshot> decodeTextPrompt(const std::span<const uint8_t> bytes,
                                        const std::string &session,
                                        const std::string &inferenceInstance,
                                        const std::string &sourceName,
                                        uint64_t &requestId,
                                        uint64_t &revision) {
    using namespace open_perception_kit::metadata;
    if (bytes.empty() || bytes.size() > MaxTextPromptRequestBytes) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Prompt packet exceeds format limit"));
    }
    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!VerifyTextPromptInputsBuffer(verifier)) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Malformed TextPromptInputs packet"));
    }
    const auto *const root = GetTextPromptInputs(bytes.data());
    if (root->request()) {
        requestId = root->request()->request_id();
        revision = root->request()->revision();
    }
    if (root->schema_major() != 1 || root->schema_minor() != 0) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, "Prompt requires schema 1.0"));
    }
    const auto validation = validateEnvelope(root->request(), inferenceInstance, sourceName);
    if (!validation) {
        return tl::unexpected(validation.error());
    }
    std::shared_ptr<const PromptPayload> payload;
    if (root->payload()) {
        const auto payloadValidation = validatePayload(*root->payload());
        if (!payloadValidation) {
            return tl::unexpected(payloadValidation.error());
        }
        payload = std::shared_ptr<const opk::TextPromptPayload>(new opk::TextPromptPayload(bytes));
    }
    const std::unique_ptr<RequestEnvelopeT> request(root->request()->UnPack());
    PromptSnapshot snapshot(session, *request, std::move(payload));
    const auto snapshotValidation = validatePromptSnapshot(snapshot);
    if (!snapshotValidation) {
        return tl::unexpected(snapshotValidation.error());
    }
    return snapshot;
}

Result<void> validateTextPrompt(const PromptSnapshot &snapshot,
                                const std::string &inferenceInstance,
                                const std::string &sourceName) {
    return validateSnapshot<TextPromptPayload>(
        snapshot, TextPromptFormat, inferenceInstance, sourceName);
}

VisualPromptPayload::VisualPromptPayload(const std::span<const uint8_t> bytes)
    : storage(bytes.begin(), bytes.end()) {}

PromptFormat VisualPromptPayload::format() const {
    return VisualPromptFormat;
}

const open_perception_kit::metadata::VisualPromptPayload &VisualPromptPayload::value() const {
    return *open_perception_kit::metadata::GetVisualPromptInputs(storage.data())->payload();
}

size_t VisualPromptPayload::ownedBytes() const {
    return sizeof(*this) + storage.capacity();
}

Result<PromptSnapshot> decodeVisualPrompt(const std::span<const uint8_t> bytes,
                                          const std::string &session,
                                          const std::string &inferenceInstance,
                                          const std::string &sourceName,
                                          uint64_t &requestId,
                                          uint64_t &revision) {
    using namespace open_perception_kit::metadata;
    if (bytes.empty() || bytes.size() > MaxVisualPromptRequestBytes) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Prompt packet exceeds format limit"));
    }
    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!VerifyVisualPromptInputsBuffer(verifier)) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Malformed VisualPromptInputs packet"));
    }
    const auto *const root = GetVisualPromptInputs(bytes.data());
    if (root->request()) {
        requestId = root->request()->request_id();
        revision = root->request()->revision();
    }
    if (root->schema_major() != 1 || root->schema_minor() != 0) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, "Prompt requires schema 1.0"));
    }
    const auto validation = validateEnvelope(root->request(), inferenceInstance, sourceName);
    if (!validation) {
        return tl::unexpected(validation.error());
    }
    std::shared_ptr<const PromptPayload> payload;
    if (root->payload()) {
        const auto payloadValidation = validatePayload(*root->payload());
        if (!payloadValidation) {
            return tl::unexpected(payloadValidation.error());
        }
        payload =
            std::shared_ptr<const opk::VisualPromptPayload>(new opk::VisualPromptPayload(bytes));
    }
    const std::unique_ptr<RequestEnvelopeT> request(root->request()->UnPack());
    PromptSnapshot snapshot(session, *request, std::move(payload));
    const auto snapshotValidation = validatePromptSnapshot(snapshot);
    if (!snapshotValidation) {
        return tl::unexpected(snapshotValidation.error());
    }
    return snapshot;
}

Result<void> validateVisualPrompt(const PromptSnapshot &snapshot,
                                  const std::string &inferenceInstance,
                                  const std::string &sourceName) {
    return validateSnapshot<VisualPromptPayload>(
        snapshot, VisualPromptFormat, inferenceInstance, sourceName);
}

} // namespace opk

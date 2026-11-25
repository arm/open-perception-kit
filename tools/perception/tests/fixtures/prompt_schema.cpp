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

#include "open_perception_kit.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>

int main() {
    namespace metadata = open_perception_kit::metadata;
    constexpr std::uint64_t requestId = 0xfedcba9876543210ULL;
    constexpr std::uint64_t revision = 0xfffffffffffffffeULL;
    metadata::BoxPromptInputsT prompted;
    prompted.request = std::make_unique<metadata::RequestEnvelopeT>();
    prompted.request->request_id = requestId;
    prompted.request->revision = revision;
    prompted.request->destination = std::make_unique<metadata::RequestDestinationT>();
    prompted.request->destination->inference_instance = "sam";
    prompted.request->destination->source_name = "pipelineVideoFrame";
    prompted.payload = std::make_unique<metadata::BoxPromptPayloadT>();
    prompted.payload->box = std::make_unique<metadata::PromptBoxT>();
    prompted.payload->box->top = 0.25;
    prompted.payload->box->right = 1.0;
    prompted.payload->box->bottom = 0.75;

    metadata::PromptAssociationT association;
    association.request = std::make_unique<metadata::RequestEnvelopeT>(*prompted.request);
    association.session_id = "session-a";
    association.parent_id = 0x8000000000000001ULL;
    open_perception_kit::container::envelope envelope;
    envelope.add(std::move(prompted));
    envelope.add(metadata::BoxPromptInputsT{});
    metadata::BoxPromptInputsT absentWithRequest;
    absentWithRequest.request = std::make_unique<metadata::RequestEnvelopeT>(*association.request);
    envelope.add(std::move(absentWithRequest));
    envelope.add(std::move(association));
    const open_perception_kit::container::envelope result(envelope.serialize());
    assert(result.valid());
    const auto decoded = result.get<metadata::BoxPromptInputsT>();
    assert(decoded && (*decoded)->request->request_id == requestId);
    assert((*decoded)->request->revision == revision);
    assert((*decoded)->payload->box->top == 0.25);
    const auto absent = result.get<metadata::BoxPromptInputsT>(1U);
    assert(absent && !(*absent)->request && !(*absent)->payload);
    const auto absentWithIdentity = result.get<metadata::BoxPromptInputsT>(2U);
    assert(absentWithIdentity && (*absentWithIdentity)->request->request_id == requestId);
    assert(!(*absentWithIdentity)->payload);
    const auto decodedAssociation = result.get<metadata::PromptAssociationT>();
    assert(decodedAssociation && (*decodedAssociation)->request->request_id == requestId);
    assert((*decodedAssociation)->session_id == "session-a");
    assert((*decodedAssociation)->parent_id == 0x8000000000000001ULL);
    metadata::TextPromptInputsT text;
    text.request = std::make_unique<metadata::RequestEnvelopeT>(*(*decoded)->request);
    text.request->request_id = UINT64_MAX;
    text.request->revision = UINT64_MAX;
    text.payload = std::make_unique<metadata::TextPromptPayloadT>();
    for (const auto *const name : {"people", "vehicles"}) {
        auto group = std::make_unique<metadata::TextPromptGroupT>();
        group->group_id = name;
        group->description = name;
        text.payload->groups.push_back(std::move(group));
    }
    metadata::VisualPromptInputsT visual;
    visual.request = std::make_unique<metadata::RequestEnvelopeT>(*text.request);
    visual.payload = std::make_unique<metadata::VisualPromptPayloadT>();
    auto reference = std::make_unique<metadata::PromptReferenceImageT>();
    reference->width = 2;
    reference->height = 1;
    reference->encoding = metadata::PromptReferenceEncoding::RGB8;
    reference->pixels = {1, 2, 3, 4, 5, 6};
    visual.payload->references.push_back(std::move(reference));
    auto group = std::make_unique<metadata::VisualPromptGroupT>();
    group->group_id = "sample";
    group->box = std::make_unique<metadata::VisualPromptBoxT>();
    group->box->right = 1;
    group->box->bottom = 1;
    visual.payload->groups.push_back(std::move(group));
    metadata::PromptAssociationT matched;
    auto match = std::make_unique<metadata::PromptObjectMatchT>();
    match->object_id = UINT64_MAX;
    match->group_id = "people";
    matched.matches.push_back(std::move(match));
    open_perception_kit::container::envelope queries;
    queries.add(std::move(text));
    queries.add(std::move(visual));
    queries.add(std::move(matched));
    const open_perception_kit::container::envelope roundtrip(queries.serialize());
    const auto textResult = roundtrip.get<metadata::TextPromptInputsT>();
    assert((*textResult)->request->request_id == UINT64_MAX);
    assert((*textResult)->request->revision == UINT64_MAX);
    assert((*textResult)->payload->groups.size() == 2);
    const auto visualResult = roundtrip.get<metadata::VisualPromptInputsT>();
    assert((*visualResult)->payload->references[0]->pixels[5] == 6);
    assert((*visualResult)->payload->groups[0]->box->right == 1);
    const auto matches = roundtrip.get<metadata::PromptAssociationT>();
    assert((*matches)->matches[0]->object_id == UINT64_MAX);
    assert((*matches)->matches[0]->group_id == "people");
}

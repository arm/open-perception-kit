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

import assert from 'node:assert/strict';
import { Envelope } from './dist/open_perception_kit/index.js';
import { BoxPromptInputsT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/box-prompt-inputs.js';
import { BoxPromptPayloadT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/box-prompt-payload.js';
import { PromptAssociationT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/prompt-association.js';
import { PromptBoxT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/prompt-box.js';
import { RequestDestinationT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/request-destination.js';
import { RequestEnvelopeT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/request-envelope.js';

const requestId = 0xfedcba9876543210n;
const revision = 0xfffffffffffffffen;
const request = new RequestEnvelopeT(
  requestId, revision, new RequestDestinationT('sam', 'pipelineVideoFrame'),
);
const prompted = new BoxPromptInputsT(
  1, 0, request, new BoxPromptPayloadT(new PromptBoxT(0, 0.25, 1, 0.75)),
);
const association = new PromptAssociationT(1, 0, null, request, 'session-a', 0x8000000000000001n);
const envelope = new Envelope();
envelope.add(prompted);
envelope.add(new BoxPromptInputsT());
envelope.add(new BoxPromptInputsT(1, 0, request));
envelope.add(association);
const result = new Envelope(envelope.serialize());
const decoded = result.get(BoxPromptInputsT);
assert.equal(decoded.request.requestId, requestId);
assert.equal(decoded.request.revision, revision);
assert.equal(decoded.payload.box.top, 0.25);
assert.equal(result.get(BoxPromptInputsT, 1).request, null);
assert.equal(result.get(BoxPromptInputsT, 1).payload, null);
assert.equal(result.get(BoxPromptInputsT, 2).request.requestId, requestId);
assert.equal(result.get(BoxPromptInputsT, 2).payload, null);
const associated = result.get(PromptAssociationT);
assert.equal(associated.request.requestId, requestId);
assert.equal(associated.sessionId, 'session-a');
assert.equal(associated.parentId, 0x8000000000000001n);

import { TextPromptInputsT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/text-prompt-inputs.js';
import { TextPromptPayloadT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/text-prompt-payload.js';
import { TextPromptGroupT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/text-prompt-group.js';
import { VisualPromptInputsT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/visual-prompt-inputs.js';
import { VisualPromptPayloadT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/visual-prompt-payload.js';
import { VisualPromptGroupT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/visual-prompt-group.js';
import { VisualPromptBoxT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/visual-prompt-box.js';
import { PromptReferenceImageT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/prompt-reference-image.js';
import { PromptObjectMatchT } from './dist/open_perception_kit/fb/open-perception-kit/metadata/prompt-object-match.js';
const maximum = 0xffffffffffffffffn;
const queryRequest = new RequestEnvelopeT(maximum, maximum, new RequestDestinationT('query', 'pipelineVideoFrame'));
const queries = new Envelope();
queries.add(new TextPromptInputsT(1, 0, queryRequest, new TextPromptPayloadT([
  new TextPromptGroupT('people', 'person'), new TextPromptGroupT('vehicles', 'bus'),
])));
queries.add(new VisualPromptInputsT(1, 0, queryRequest, new VisualPromptPayloadT([
  new PromptReferenceImageT(2, 1, 1, [1, 2, 3, 4, 5, 6]),
], [new VisualPromptGroupT('sample', '', 0, new VisualPromptBoxT(0.25, 0, 1, 1))])));
queries.add(new PromptAssociationT(1, 0, null, queryRequest, 'session', 1n, [new PromptObjectMatchT(maximum, 'people')]));
const queryResult = new Envelope(queries.serialize());
assert.equal(queryResult.get(TextPromptInputsT).request.requestId, maximum);
assert.equal(queryResult.get(TextPromptInputsT).request.revision, maximum);
assert.equal(queryResult.get(TextPromptInputsT).payload.groups.length, 2);
assert.deepEqual(Array.from(queryResult.get(VisualPromptInputsT).payload.references[0].pixels), [1, 2, 3, 4, 5, 6]);
assert.equal(queryResult.get(VisualPromptInputsT).payload.groups[0].box.left, 0.25);
assert.equal(queryResult.get(PromptAssociationT).matches[0].objectId, maximum);
assert.equal(queryResult.get(PromptAssociationT).matches[0].groupId, 'people');

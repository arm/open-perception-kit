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
import test from 'node:test';
import {ByteBuffer} from 'flatbuffers';
import {BoxPromptInputs} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-prompt-inputs.js';
import {Envelope} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/index.js';
import {BitmapDataT, BoundingBoxT, LayerInfoT, ObjectMetaT, ProducerInfoT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata.js';
import {PromptAssociationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/prompt-association.js';
import {RequestEnvelopeT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/request-envelope.js';
import {RequestDestinationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/request-destination.js';
import {InstanceSegmentationMaskT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/instance-segmentation-mask.js';
import {InstanceSegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/instance-segmentation-masks.js';
import {decodeFrameResultsMessage, FRAME_RESULTS_ENCODING} from '../src/frame-results.js';
import {BoxPromptSession, encodeBoxPrompt, matchingPromptResult, promptPoint, validPromptBox} from '../src/prompt-inputs.js';
import {sendConnected} from '../src/control-messages.js';

const target = {target: 'prompts', destination_element: 'mobileSam', inference_instance: 'sam',
    source_name: 'pipelineVideoFrame', format: 'BPRM', format_major: 1, format_minor: 0, drawing_supported: true};
const box = {left: 0.1, top: 0.2, right: 0.7, bottom: 0.8};
const maximum = (1n << 64n) - 1n;
const capabilities = session => ({version: 1, session_id: session, targets: [null, target]});

function ack(session, wire, accepted = true, identity = {}) {
    const pending = session.state(wire.target).pending;
    return {type: 'prompt_ack', version: 1, command_id: wire.command_id, command: wire.command,
        target: wire.target, session_id: session.sessionId, accepted, reason: accepted ? '' : 'Rejected', basis: 'bus',
        request_id: String(pending.request.requestId), revision: String(pending.request.revision), ...identity};
}

function packet(wire) {
    const buffer = new ByteBuffer(Uint8Array.from(atob(wire), ch => ch.charCodeAt(0)));
    assert.equal(BoxPromptInputs.bufferHasIdentifier(buffer), true);
    return BoxPromptInputs.getRootAsBoxPromptInputs(buffer).unpack();
}

test('SDK box packet preserves uint64 maxima, destination, policy and exact geometry', () => {
    const decoded = packet(encodeBoxPrompt(target, maximum, maximum, box));
    assert.equal(decoded.request.requestId, maximum);
    assert.equal(decoded.request.revision, maximum);
    assert.equal(decoded.request.application, 0);
    assert.deepEqual({...decoded.request.destination}, {inferenceInstance: 'sam', sourceName: 'pipelineVideoFrame'});
    assert.deepEqual({...decoded.payload.box}, box);
    for (const value of [0n, -1n, maximum + 1n, 1]) {
        assert.throws(() => encodeBoxPrompt(target, value, 1n, box));
    }
    for (const invalid of [{...box, left: NaN}, {...box, right: Infinity}, {...box, left: box.right}, {...box, bottom: 1.1}]) {
        assert.equal(validPromptBox(invalid), false);
        assert.throws(() => encodeBoxPrompt(target, 1n, 1n, invalid));
    }
});

test('pointer mapping rejects letterbox starts, clamps ends and handles portrait, invalid and CSS-pixel geometry', () => {
    assert.equal(promptPoint(400, 50, 800, 800, 640, 480), null);
    assert.deepEqual(promptPoint(400, 100, 800, 800, 640, 480), {x: 0.5, y: 0});
    assert.deepEqual(promptPoint(900, -50, 800, 800, 640, 480, true), {x: 1, y: 0});
    assert.deepEqual(promptPoint(400, 400, 800, 800, 480, 640), {x: 0.5, y: 0.5});
    assert.equal(promptPoint(0, 0, 0, 800, 640, 480), null);
    assert.equal(promptPoint(NaN, 0, 800, 800, 640, 480), null);
});

test('accepted update, rejected replacement, cancellation and fresh request preserve lifecycle identities', () => {
    const sent = [];
    const session = new BoxPromptSession(wire => { sent.push(wire); return true; });
    session.capabilities(capabilities('session-a'));
    session.nextRequestId = (1n << 63n) + 7n;
    assert.equal(session.command('prompts', 'submit', box), true);
    assert.equal(session.command('prompts', 'submit', box), false);
    const first = packet(sent[0].payload_b64);
    assert.equal(first.request.requestId, (1n << 63n) + 7n);
    assert.equal(session.receive(ack(session, sent[0])), true);
    const state = session.state('prompts');
    assert.equal(state.active.revision, 1n);
    session.command('prompts', 'submit', {...box, left: 0.2});
    session.receive(ack(session, sent[1], false));
    assert.equal(state.active.revision, 1n);
    assert.match(state.message, /Ingress rejected/);
    session.command('prompts', 'submit', box);
    session.receive(ack(session, sent[2]));
    assert.equal(state.active.revision, 2n);
    session.command('prompts', 'cancel');
    assert.equal(sent[3].request_id, String(first.request.requestId));
    assert.equal(sent[3].revision, '2');
    session.receive(ack(session, sent[3]));
    assert.equal(state.active, null);
    session.command('prompts', 'submit', box);
    assert.equal(packet(sent[4].payload_b64).request.requestId, first.request.requestId + 1n);
    assert.equal(packet(sent[4].payload_b64).request.revision, 1n);
});

test('foreign replies cannot consume pending work and transport rejection retains accepted state', () => {
    let wire;
    const session = new BoxPromptSession(message => { wire = message; return true; });
    session.capabilities(capabilities('session-a'));
    session.command('prompts', 'submit', box);
    assert.equal(session.receive(ack(session, wire, true, {session_id: 'old-session'})), false);
    assert.equal(session.receive(ack(session, wire, true, {request_id: '999'})), false);
    assert.equal(session.state('prompts').active, null);
    session.receive(ack(session, wire));
    const active = session.state('prompts').active;
    session.command('prompts', 'submit', box);
    assert.equal(session.receive({type: 'prompt_error', version: 1, target: 'prompts', command_id: wire.command_id, reason: '<invalid>'}), true);
    assert.equal(session.state('prompts').active, active);
    assert.equal(session.state('prompts').pending, null);
    assert.match(session.state('prompts').message, /<invalid>/);
    const failure = {type: 'prompt_failure', version: 1, target: 'prompts', session_id: 'old-session',
        request_id: String(active.requestId), revision: String(active.revision), reason: 'Late'};
    assert.equal(session.receive(failure), false);
    assert.equal(session.receive({...failure, session_id: 'session-a', revision: '9'}), false);
    assert.equal(session.receive({...failure, session_id: 'session-a'}), true);
    assert.match(session.state('prompts').message, /Invocation failed/);
});

test('event-return fallback uses locally encoded identity and exhausted uint64 never wraps', () => {
    let wire;
    const session = new BoxPromptSession(message => { wire = message; return true; });
    session.capabilities(capabilities('session-a'));
    session.command('prompts', 'submit', box);
    session.receive(ack(session, wire, true, {basis: 'event-return', request_id: '0', revision: '0'}));
    assert.equal(session.state('prompts').active.requestId, 1n);
    session.state('prompts').active.revision = maximum;
    assert.throws(() => session.command('prompts', 'submit', box));
    assert.equal(session.state('prompts').pending, null);
});

test('closed socket never queues prompts and reconnect creates no replay or retained identity', () => {
    const sent = [];
    const socket = {readyState: 0, send: message => sent.push(message)};
    const session = new BoxPromptSession(message => sendConnected(socket, message, 1));
    session.capabilities(capabilities('session-a'));
    assert.equal(session.command('prompts', 'submit', box), false);
    assert.equal(sent.length, 0);
    session.disconnect();
    socket.readyState = 1;
    session.capabilities(capabilities('session-b'));
    assert.equal(sent.length, 0);
    assert.equal(session.state('prompts').active, null);
    assert.equal(session.command('prompts', 'submit', box), true);
    assert.equal(sent.length, 1);
    assert.equal(packet(JSON.parse(sent[0]).payload_b64).request.requestId, 1n);
    socket.send = () => { throw new Error('Disconnected during send'); };
    assert.equal(sendConnected(socket, {}, 1), false);
});

function associatedFrame() {
    const producer = new ProducerInfoT('mobileSam/parser', 'opk-std-ops/GenericPostprocess', 'MobileSAM');
    const layer = () => new LayerInfoT('', 'model', '', 'mobileSam', '', 'instanceSegmentation', '', producer);
    const request = new RequestEnvelopeT((1n << 63n) + 7n, 2n, new RequestDestinationT('sam', 'pipelineVideoFrame'));
    const envelope = new Envelope();
    envelope.add(new InstanceSegmentationMasksT(1, 0, layer(), [new InstanceSegmentationMaskT(
        new ObjectMetaT(5n, maximum), new BitmapDataT(1, 1, 'uint8', [1]), 0, 0.9, new BoundingBoxT(0, 0, 1, 1))]));
    envelope.add(new PromptAssociationT(1, 0, layer(), request, 'session-a', maximum));
    return decodeFrameResultsMessage({frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64')}).frame_results;
}

test('association decoder preserves identity and matching requires session, revision, destination, producer and parent', () => {
    const frame = associatedFrame();
    const association = frame.layers.find(item => item.payloadType === 'PromptAssociation');
    const data = association.detections[0].data;
    const request = {requestId: (1n << 63n) + 7n, revision: 2n};
    assert.equal(data.parentId, String(maximum));
    assert.equal(data.requestId, String(request.requestId));
    assert.equal(data.application, 0);
    assert.equal(matchingPromptResult(frame, target, 'session-a', request), true);
    assert.equal(matchingPromptResult(frame, target, 'new-session', request), false);
    assert.equal(matchingPromptResult(frame, target, 'session-a', {...request, revision: 3n}), false);
    assert.equal(matchingPromptResult(frame, {...target, source_name: 'other'}, 'session-a', request), false);
    for (const [key, value] of [['parentId', '0'], ['parentId', '99'], ['application', 9]]) {
        const original = data[key];
        data[key] = value;
        assert.equal(matchingPromptResult(frame, target, 'session-a', request), false);
        data[key] = original;
    }
    frame.layers.find(item => item.payloadType === 'InstanceSegmentationMasks').producer.instanceId = 'other';
    assert.equal(matchingPromptResult(frame, target, 'session-a', request), false);
    assert.equal(matchingPromptResult({layers: []}, target, 'session-a', request), false);
});

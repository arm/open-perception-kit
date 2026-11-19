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

import {Builder} from 'flatbuffers';
import {BoxPromptInputs, BoxPromptInputsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-prompt-inputs.js';
import {BoxPromptPayloadT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-prompt-payload.js';
import {PromptBoxT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/prompt-box.js';
import {RequestEnvelopeT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/request-envelope.js';
import {RequestDestinationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/request-destination.js';
import {calculateContainedRect} from './overlay-drawing.js';

const MAX_UINT64 = (1n << 64n) - 1n;

export function validPromptBox(box) {
    return box && ['left', 'top', 'right', 'bottom'].every(key => Number.isFinite(box[key]))
        && box.left >= 0 && box.left < box.right && box.right <= 1
        && box.top >= 0 && box.top < box.bottom && box.bottom <= 1;
}

export function promptPoint(x, y, width, height, videoWidth, videoHeight, clamp = false) {
    if (![x, y, width, height, videoWidth, videoHeight].every(Number.isFinite)
        || Math.min(width, height, videoWidth, videoHeight) <= 0) {
        return null;
    }
    const rect = calculateContainedRect(width, height, videoWidth, videoHeight);
    const point = {x: (x - rect.x) / rect.width, y: (y - rect.y) / rect.height};
    if (!clamp && (point.x < 0 || point.x > 1 || point.y < 0 || point.y > 1)) {
        return null;
    }
    return {x: Math.max(0, Math.min(1, point.x)), y: Math.max(0, Math.min(1, point.y))};
}

export function encodeBoxPrompt(target, requestId, revision, box) {
    if (typeof requestId !== 'bigint' || typeof revision !== 'bigint'
        || requestId <= 0n || requestId > MAX_UINT64 || revision <= 0n || revision > MAX_UINT64
        || !target?.inference_instance || !target?.source_name || !validPromptBox(box)) {
        throw new Error('A valid box, destination and nonzero uint64 request/revision are required.');
    }
    const destination = new RequestDestinationT(target.inference_instance, target.source_name);
    const request = new RequestEnvelopeT(requestId, revision, destination);
    const payload = new BoxPromptPayloadT(new PromptBoxT(box.left, box.top, box.right, box.bottom));
    const packet = new BoxPromptInputsT(1, 0, request, payload);
    const builder = new Builder();
    const root = packet.pack(builder);
    BoxPromptInputs.finishBoxPromptInputsBuffer(builder, root);
    return btoa(String.fromCharCode(...builder.asUint8Array()));
}

export function matchingPromptResult(frame, target, sessionId, request) {
    if (!frame || !target || !sessionId || !request) {
        return false;
    }
    for (const association of frame.layers || []) {
        if (association.payloadType !== 'PromptAssociation'
            || association.inferElementId !== target.destination_element) {
            continue;
        }
        for (const {data} of association.detections || []) {
            if (data.application !== 0 || data.sessionId !== sessionId || data.requestId !== String(request.requestId)
                || data.revision !== String(request.revision) || data.parentId === '0'
                || data.inferenceInstance !== target.inference_instance || data.sourceName !== target.source_name) {
                continue;
            }
            const producer = association.producer;
            if (!producer?.instanceId || !producer.component) {
                continue;
            }
            if ((frame.layers || []).some(layer => layer.payloadType === 'InstanceSegmentationMasks'
                && layer.inferElementId === association.inferElementId
                && layer.producer?.instanceId === producer.instanceId
                && layer.producer?.component === producer.component
                && (layer.detections || []).some(item => item.type === 'InstanceSegmentationMask'
                    && item.data.parentUuid === data.parentId))) {
                return true;
            }
        }
    }
    return false;
}

export class BoxPromptSession {
    constructor(send) {
        this.send = send;
        this.disconnect();
    }

    disconnect() {
        this.sessionId = '';
        this.targets = [];
        this.states = new Map();
        this.nextRequestId = 1n;
        this.nextCommandId = 1n;
    }

    capabilities(message) {
        if (message.version !== 1 || typeof message.session_id !== 'string' || !message.session_id
            || !Array.isArray(message.targets)) {
            return false;
        }
        if (this.sessionId !== message.session_id) {
            this.disconnect();
        }
        this.sessionId = message.session_id;
        this.targets = message.targets.filter(target => target && typeof target.target === 'string'
            && target.format === 'BPRM' && target.format_major === 1 && target.format_minor === 0
            && typeof target.inference_instance === 'string' && typeof target.source_name === 'string'
            && typeof target.destination_element === 'string');
        return true;
    }

    state(name) {
        if (!this.states.has(name)) {
            this.states.set(name, {active: null, pending: null, message: 'Local draft. Apply to submit.'});
        }
        return this.states.get(name);
    }

    command(name, command, box) {
        const target = this.targets.find(item => item.target === name);
        const state = this.state(name);
        if (!this.sessionId || !target || state.pending) {
            return false;
        }
        if (command === 'submit' && !target.drawing_supported) {
            return false;
        }
        const commandId = `ui-${this.nextCommandId++}`;
        const wire = {type: 'prompt_command', version: 1, command_id: commandId, target: name, command};
        let request = state.active;
        if (command === 'submit') {
            request = state.active ? {...state.active, revision: state.active.revision + 1n}
                : {requestId: this.nextRequestId++, revision: 1n};
            wire.payload_b64 = encodeBoxPrompt(target, request.requestId, request.revision, box);
        } else if (command === 'cancel') {
            if (!request) {
                return false;
            }
            wire.request_id = String(request.requestId);
            wire.revision = String(request.revision);
        } else {
            return false;
        }
        state.pending = {commandId, command, request};
        if (!this.send(wire)) {
            state.pending = null;
            state.message = 'Disconnected. Reconnect and Apply deliberately.';
            return false;
        }
        state.message = 'Awaiting ingress reply.';
        return true;
    }

    receive(message) {
        if (message.version !== 1 || (message.session_id && message.session_id !== this.sessionId)) {
            return false;
        }
        const state = this.states.get(message.target);
        if (!state) {
            return false;
        }
        if (message.type === 'prompt_failure') {
            if (message.session_id !== this.sessionId || !state.active
                || message.request_id !== String(state.active.requestId)
                || message.revision !== String(state.active.revision)) {
                return false;
            }
            state.message = `Invocation failed: ${message.reason || 'Unknown failure'}`;
            return true;
        }
        const pending = state.pending;
        if (!pending || message.command_id !== pending.commandId) {
            return false;
        }
        if (message.type === 'prompt_error') {
            state.pending = null;
            state.message = `Command not dispatched: ${message.reason || 'Unknown transport error'}`;
            return true;
        }
        if (message.type !== 'prompt_ack' || message.session_id !== this.sessionId
            || message.command !== pending.command || typeof message.accepted !== 'boolean') {
            return false;
        }
        const identityMatches = message.request_id === String(pending.request.requestId)
            && message.revision === String(pending.request.revision);
        if (message.accepted && !identityMatches && !(message.basis === 'event-return'
            && message.request_id === '0' && message.revision === '0')) {
            return false;
        }
        state.pending = null;
        if (message.accepted) {
            state.active = pending.command === 'submit' ? pending.request : null;
            state.message = pending.command === 'submit'
                ? `Accepted revision ${pending.request.revision} for future buffers. Waiting for matching results.`
                : 'Cancelled for future buffers. Already paired work may finish.';
        } else {
            state.message = `Ingress rejected command: ${message.reason || 'No reason supplied'}`;
        }
        return true;
    }
}

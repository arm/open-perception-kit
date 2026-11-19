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
import {parseControlMessage, sendConnected} from '../src/control-messages.js';

test('typed prompt replies and legacy status coexist without assuming pipeline_state on every message', () => {
    const legacy = {pipeline_state: {playing: true}, models: []};
    assert.deepEqual(parseControlMessage(JSON.stringify(legacy)), {kind: 'status', data: legacy});
    for (const type of ['prompt_capabilities', 'prompt_ack', 'prompt_error', 'prompt_failure']) {
        const data = {type, version: 1};
        assert.deepEqual(parseControlMessage(JSON.stringify(data)), {kind: 'prompt', data});
        assert.equal(parseControlMessage(JSON.stringify({...data, version: 2})), null);
    }
    for (const message of ['{', 'null', '[]', '4', '{"type":"future"}']) {
        assert.equal(parseControlMessage(message), null);
    }
    assert.deepEqual(parseControlMessage('{}'), {kind: 'status', data: {}});
});

test('connected-only sender never retries or queues after a closed socket', () => {
    const sent = [];
    const socket = {readyState: 0, send: text => sent.push(text)};
    assert.equal(sendConnected(socket, {type: 'prompt_command'}, 1), false);
    socket.readyState = 1;
    assert.equal(sent.length, 0);
    assert.equal(sendConnected(socket, {type: 'prompt_command'}, 1), true);
    assert.equal(sent.length, 1);
    assert.equal(sendConnected(null, {}, 1), false);
});

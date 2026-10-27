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

import test from 'node:test';
import assert from 'node:assert/strict';
import {MicrophoneClient} from '../src/microphone_client.js';

test('control availability gates microphone capture and hides it on disconnect', async () => {
    const globals = ['document', 'window', 'location', 'navigator'];
    const originals = globals.map(name => Object.getOwnPropertyDescriptor(globalThis, name));
    let click;
    let grant;
    let requests = 0;
    let stopped = false;
    const button = {
        style: {display: 'none'}, disabled: true,
        setAttribute() {},
        addEventListener(name, callback) { click = callback; },
    };
    const status = {hidden: true};
    const icon = {dataset: {icon: 'mic-off'}};
    const elements = {microphoneBtn: button, microphoneStatus: status, microphoneIcon: icon};
    const mocks = {
        document: {getElementById: id => elements[id]},
        window: {addEventListener() {}},
        location: {href: 'http://localhost:9999/', protocol: 'http:'},
        navigator: {mediaDevices: {getUserMedia() {
            requests++;
            return new Promise(resolve => { grant = resolve; });
        }}},
    };
    try {
        for (const name of globals)
            Object.defineProperty(globalThis, name, {configurable: true, value: mocks[name]});
        const {enableMicrophoneButton} = await import('../src/microphone.js');
        click();
        assert.equal(requests, 0);
        enableMicrophoneButton(true);
        assert.equal(button.style.display, 'inline-flex');
        assert.equal(button.disabled, false);
        assert.equal(status.hidden, false);
        click();
        assert.equal(requests, 1);
        assert.equal(icon.dataset.icon, 'mic');
        enableMicrophoneButton(false);
        assert.equal(icon.dataset.icon, 'mic-off');
        assert.equal(button.style.display, 'none');
        assert.equal(button.disabled, true);
        assert.equal(status.hidden, true);
        grant({getTracks: () => [{stop() { stopped = true; }}]});
        await Promise.resolve();
        assert.equal(stopped, true);
        enableMicrophoneButton(undefined);
        click();
        assert.equal(requests, 1);
    } finally {
        globals.forEach((name, index) => {
            if (originals[index])
                Object.defineProperty(globalThis, name, originals[index]);
            else
                delete globalThis[name];
        });
    }
});

function setup(getUserMedia) {
    const track = {kind: 'audio', stopped: false, stop() { this.stopped = true; }};
    const stream = {getTracks: () => [track], getAudioTracks: () => [track]};
    const sent = [];
    const candidates = [];
    const statuses = [];
    const pc = {
        addTransceiver(track, options) { assert.equal(options.direction, 'sendonly'); },
        async createOffer() { return {type: 'offer', sdp: 'microphone offer'}; },
        async setLocalDescription(offer) {
            this.localDescription = offer;
            this.onicecandidate({candidate: {candidate: 'local', sdpMLineIndex: 0}});
        },
        async setRemoteDescription(answer) { this.remoteDescription = answer; },
        async addIceCandidate(candidate) { candidates.push(candidate); },
        close() { this.closed = true; },
    };
    const ws = {send: message => sent.push(JSON.parse(message)), close() { this.closed = true; }};
    const client = new MicrophoneClient({
        getUserMedia: getUserMedia || (async options => {
            assert.deepEqual(options, {audio: {echoCancellation: true}, video: false});
            return stream;
        }),
        createPeer: () => pc,
        createSocket: url => { assert.equal(url, 'ws://localhost:8003/ws'); return ws; },
        signalingUrl: 'ws://localhost:8003/ws',
        onStatus: (active, message) => statuses.push({active, message}),
    });
    return {client, track, stream, sent, candidates, statuses, pc, ws};
}

test('microphone sends an offer before ICE and queues remote ICE until answer', async () => {
    const state = setup();
    try {
        await state.client.start();
        await state.ws.onopen();
        assert.deepEqual(state.sent.map(message => message.type), ['offer', 'candidate']);
        state.ws.onmessage({data: JSON.stringify({type: 'candidate', ice: {candidate: 'remote'}})});
        state.ws.onmessage({data: JSON.stringify({type: 'answer', sdp: 'answer'})});
        await state.client.session.messages;
        assert.equal(state.pc.remoteDescription.sdp, 'answer');
        assert.equal(state.candidates[0].candidate, 'remote');
        state.pc.connectionState = 'connected';
        state.pc.onconnectionstatechange();
        assert.equal(state.statuses.at(-1).message, 'Microphone sharing');
    } finally {
        state.client.stop();
    }
    assert.ok(state.track.stopped && state.pc.closed && state.ws.closed);
});

test('stopping while permission is pending releases late microphone stream', async () => {
    let grant;
    const state = setup(() => new Promise(resolve => { grant = resolve; }));
    const starting = state.client.start();
    state.client.stop();
    grant(state.stream);
    await starting;
    assert.ok(state.track.stopped);
    assert.equal(state.client.session, null);
});

test('permission denial returns to off state', async () => {
    const state = setup(async () => { throw new Error('Permission denied'); });
    await state.client.start();
    assert.equal(state.client.session, null);
    assert.match(state.statuses.at(-1).message, /Permission denied/);
});

test('signaling failure releases capture and allows another attempt', async () => {
    const state = setup();
    await state.client.start();
    state.ws.onerror();
    assert.ok(state.track.stopped && state.pc.closed && state.ws.closed);
    assert.equal(state.client.session, null);
    await state.client.start();
    assert.ok(state.client.session);
    state.client.stop();
});

test('malformed signaling stops capture', async () => {
    const state = setup();
    await state.client.start();
    const session = state.client.session;
    state.ws.onmessage({data: '{'});
    await session.messages;
    assert.ok(state.track.stopped);
    assert.equal(state.client.session, null);
});

/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

import {resolveWebRtcIceConfig, resolveWebRtcTimingConfig} from '../src/webrtc_config.js';

test('explicit zero timing config is preserved', () => {
    assert.deepEqual(resolveWebRtcTimingConfig({
        webrtc: {
            frameTimeoutMs: 0,
            reconnectDelayMs: 0,
            maxReconnectDelayMs: 0,
            backoffFactor: 0,
        },
    }), {
        frameTimeoutMs: 0,
        reconnectDelayMs: 0,
        maxReconnectDelayMs: 0,
        backoffFactor: 0,
    });
});

test('legacy frame timeout and defaults are used when WebRTC config is absent', () => {
    assert.deepEqual(resolveWebRtcTimingConfig({webrtcFrameTimeoutMs: 2500}), {
        frameTimeoutMs: 2500,
        reconnectDelayMs: 1000,
        maxReconnectDelayMs: 15000,
        backoffFactor: 1.1,
    });
});

test('default frame timeout tolerates slow inference pipelines', () => {
    assert.equal(resolveWebRtcTimingConfig({}).frameTimeoutMs, 30000);
});

test('configured ICE servers are used when present', () => {
    const iceServers = [
        {urls: 'stun:192.168.2.192:3478'},
        {urls: 'turn:192.168.2.192:3478', username: 'test', credential: 'test123'},
    ];

    assert.deepEqual(resolveWebRtcIceConfig({webrtc: {iceServers}}), {iceServers});
});

test('Google STUN is the browser fallback when ICE config is absent', () => {
    assert.deepEqual(resolveWebRtcIceConfig({}), {
        iceServers: [{urls: 'stun:stun.l.google.com:19302'}],
    });
});

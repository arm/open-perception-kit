// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

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

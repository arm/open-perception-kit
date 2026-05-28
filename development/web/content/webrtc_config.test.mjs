import assert from 'node:assert/strict';
import test from 'node:test';

import {resolveWebRtcTimingConfig} from './webrtc_config.js';

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

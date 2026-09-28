// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

export function resolveWebRtcTimingConfig(opkConfig = {}) {
    const webrtcConfig = opkConfig.webrtc || {};

    return {
        frameTimeoutMs: webrtcConfig.frameTimeoutMs ?? opkConfig.webrtcFrameTimeoutMs ?? 30000,
        reconnectDelayMs: webrtcConfig.reconnectDelayMs ?? 1000,
        maxReconnectDelayMs: webrtcConfig.maxReconnectDelayMs ?? 15000,
        backoffFactor: webrtcConfig.backoffFactor ?? 1.1,
    };
}

export function resolveWebRtcIceConfig(opkConfig = {}) {
    const webrtcConfig = opkConfig.webrtc || {};

    return {
        iceServers: webrtcConfig.iceServers ?? [{urls: 'stun:stun.l.google.com:19302'}],
    };
}

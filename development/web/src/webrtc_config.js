export function resolveWebRtcTimingConfig(pekConfig = {}) {
    const webrtcConfig = pekConfig.webrtc || {};

    return {
        frameTimeoutMs: webrtcConfig.frameTimeoutMs ?? pekConfig.webrtcFrameTimeoutMs ?? 30000,
        reconnectDelayMs: webrtcConfig.reconnectDelayMs ?? 1000,
        maxReconnectDelayMs: webrtcConfig.maxReconnectDelayMs ?? 15000,
        backoffFactor: webrtcConfig.backoffFactor ?? 1.1,
    };
}

export function resolveWebRtcIceConfig(pekConfig = {}) {
    const webrtcConfig = pekConfig.webrtc || {};

    return {
        iceServers: webrtcConfig.iceServers ?? [{urls: 'stun:stun.l.google.com:19302'}],
    };
}

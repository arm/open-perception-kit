export function resolveWebRtcTimingConfig(pekConfig = {}) {
    const webrtcConfig = pekConfig.webrtc || {};

    return {
        frameTimeoutMs: webrtcConfig.frameTimeoutMs ?? pekConfig.webrtcFrameTimeoutMs ?? 5000,
        reconnectDelayMs: webrtcConfig.reconnectDelayMs ?? 1000,
        maxReconnectDelayMs: webrtcConfig.maxReconnectDelayMs ?? 15000,
        backoffFactor: webrtcConfig.backoffFactor ?? 1.1,
    };
}

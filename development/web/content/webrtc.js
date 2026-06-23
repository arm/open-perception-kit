import {createWebRtcClient} from './webrtc_client.js';
import {resolveWebRtcIceConfig, resolveWebRtcTimingConfig} from './webrtc_config.js';

// ===== UI ELEMENT REFERENCES =====
const video = document.getElementById('video');
const statusPill = document.getElementById('status-pill');
const statusLabelEl = document.getElementById('status-label');
const statusSubtextEl = document.getElementById('status-subtext');
const statusLineEl = document.getElementById('status-line');
const overlay = document.getElementById('video-overlay');
const overlayText = document.getElementById('overlay-text');
const logEl = document.getElementById('log');

// ===== UI HELPERS =====
function setStatus(state, label, subtext) {
    if (statusPill) {
        statusPill.classList.remove('connecting', 'connected', 'reconnecting', 'disconnected');
        statusPill.classList.add(state);
    }
    statusLineEl.classList.remove('connecting', 'connected', 'reconnecting', 'disconnected');
    statusLineEl.classList.add(state);
    if (statusLabelEl)
        statusLabelEl.textContent = label.toUpperCase();
    if (subtext && statusSubtextEl)
        statusSubtextEl.textContent = subtext;

    switch (state) {
        case 'connecting':
        case 'reconnecting':
            overlay.classList.remove('hidden');
            overlayText.textContent = document.body.classList.contains('is-pipeline-restarting')
                ? 'Restarting'
                : 'Connecting…';
            break;
        case 'connected':
            if (!document.body.classList.contains('is-pipeline-restarting'))
                overlay.classList.add('hidden');
            break;
        case 'disconnected':
            overlay.classList.remove('hidden');
            overlayText.textContent = document.body.classList.contains('is-pipeline-restarting')
                ? 'Restarting'
                : 'Disconnected – waiting for stream…';
            break;
    }
}

function setStatusLine(text) {
    if (document.body.classList.contains('is-pipeline-restarting')) {
        const textEl = statusLineEl.querySelector('.status-line-text');
        if (textEl) {
            textEl.textContent = 'Restarting Pipeline';
        }
        return;
    }

    const textEl = statusLineEl.querySelector('.status-line-text');
    if (textEl) {
        textEl.textContent = text;
    } else {
        statusLineEl.textContent = text;
    }
}

function appendLog(message, type = 'info') {
    const div = document.createElement('div');
    div.className = 'log-line' + (type === 'error' ? ' error' : '');
    const time = new Date().toLocaleTimeString();
    div.innerHTML = `<span>[${time}]</span> <span class="log-tag">${type === 'error' ? 'ERR' : 'LOG'}</span>${message}`;
    logEl.appendChild(div);
    logEl.scrollTop = logEl.scrollHeight;

    console[type === 'error' ? 'error' : 'log']('[WebRTC UI]', message);
}

// ===== WEBRTC + SIGNALING LOGIC =====
const WS_PROTO = location.protocol === 'https:' ? 'wss' : 'ws';
const WS_HOST = location.hostname;

function fallbackWebRtcPort() {
    const pagePort = Number.parseInt(location.port || '', 10);
    if (pagePort === 9999 || pagePort === 10099) {
        return 8000;
    }

    return location.port || (location.protocol === 'https:' ? 443 : 80);
}

// Prefer configured wsPort, fallback to child signaling port for supervised UI.
const WS_PORT = (window.PEK_CONFIG && window.PEK_CONFIG.wsPort) || fallbackWebRtcPort();

const SIGNALING_URL = `${WS_PROTO}://${WS_HOST}:${WS_PORT}/ws`;
const WEBRTC_TIMING_CONFIG = resolveWebRtcTimingConfig(window.PEK_CONFIG || {});
const WEBRTC_ICE_CONFIG = resolveWebRtcIceConfig(window.PEK_CONFIG || {});
const PIPELINE_RESTART_SESSION_KEY = "pekPipelineRestarting";
let client = null;
let repeatedFailureRestartInProgress = false;

function setPipelineRestartUiBusy(busy) {
    document.body.classList.toggle("is-pipeline-restarting", busy);

    if (busy) {
        sessionStorage.setItem(PIPELINE_RESTART_SESSION_KEY, "true");
        setStatus('reconnecting', 'Reconnecting', 'Restarting pipeline after repeated connection failures...');
        setStatusLine('Restarting Pipeline');
        overlay?.classList.remove('hidden');
        if (overlayText)
            overlayText.textContent = 'Restarting';
    } else {
        sessionStorage.removeItem(PIPELINE_RESTART_SESSION_KEY);
    }
}

function hasSupervisorRestartApi() {
    return window.PEK_CONFIG?.supervised === true ||
        Boolean(document.getElementById("pipelineSelect") && document.getElementById("restartPipelineBtn"));
}

function canStartRepeatedFailurePipelineRestart() {
    return Boolean(
        hasSupervisorRestartApi() &&
        !repeatedFailureRestartInProgress &&
        !document.body.classList.contains("is-pipeline-restarting") &&
        sessionStorage.getItem(PIPELINE_RESTART_SESSION_KEY) !== "true"
    );
}

async function requestPipelineRestartAfterRepeatedFailure(detail) {
    if (!canStartRepeatedFailurePipelineRestart())
        return false;

    repeatedFailureRestartInProgress = true;
    setPipelineRestartUiBusy(true);
    window.dispatchEvent(new CustomEvent("pipeline-restart", {
        detail: {available: true, requested: true, auto: true},
    }));

    appendLog(
        `Restarting pipeline after ${detail.failureCount} repeated WebRTC connection failures ` +
        `(${detail.reason}).`,
        'error',
    );

    try {
        const response = await fetch("/api/pipeline-restart", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: "{}",
        });
        const result = await response.json().catch(() => ({}));

        if (!response.ok || !result.complete) {
            throw new Error(result.message || `pipeline restart failed: ${response.status}`);
        }

        appendLog(result.message || "Pipeline restarted after repeated WebRTC failures.");
        window.dispatchEvent(new CustomEvent("pipeline-layout-reset"));
        window.dispatchEvent(new CustomEvent("pipeline-restart", {
            detail: {available: true, requested: false, complete: true, auto: true},
        }));
    } catch (error) {
        appendLog(`Pipeline restart after repeated WebRTC failures failed: ${error.message || error}`, 'error');
        window.dispatchEvent(new CustomEvent("pipeline-restart", {
            detail: {available: true, requested: false, complete: false, auto: true},
        }));
    } finally {
        repeatedFailureRestartInProgress = false;
        setPipelineRestartUiBusy(false);
        client?.reconnectNow('pipeline restart after repeated WebRTC failures');
    }

    return true;
}

client = createWebRtcClient({
    video,
    signalingUrl: SIGNALING_URL,
    ...WEBRTC_TIMING_CONFIG,
    ...WEBRTC_ICE_CONFIG,
    pipelineRestartFailureThreshold: window.PEK_CONFIG?.pipelineRestartFailureThreshold ?? 3,
    onRepeatedFailure: (detail) => {
        if (!canStartRepeatedFailurePipelineRestart())
            return false;

        requestPipelineRestartAfterRepeatedFailure(detail);
        return true;
    },
    logger: appendLog,
    onStatus: setStatus,
    onStatusLine: setStatusLine,
    onRemoteTrack: (track) => {
        if (track.kind === 'audio')
            appendLog('Audio track attached. If muted=false, you should hear sound.');
    },
    WebSocketFactory: (url) => new WebSocket(url),
    RTCPeerConnectionFactory: (options) => new RTCPeerConnection(options),
    MediaStreamFactory: () => new MediaStream(),
    RTCSessionDescriptionFactory: (description) => new RTCSessionDescription(description),
    RTCIceCandidateFactory: (candidate) => new RTCIceCandidate(candidate),
    webSocketOpenState: WebSocket.OPEN,
    webSocketConnectingState: WebSocket.CONNECTING,
});

client.start();

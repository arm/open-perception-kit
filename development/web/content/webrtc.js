import {createWebRtcClient} from './webrtc_client.js';
import {resolveWebRtcTimingConfig} from './webrtc_config.js';

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
    statusPill.classList.remove('connecting', 'connected', 'reconnecting', 'disconnected');
    statusPill.classList.add(state);
    statusLabelEl.textContent = label.toUpperCase();
    if (subtext)
        statusSubtextEl.textContent = subtext;

    switch (state) {
    case 'connecting':
    case 'reconnecting':
        overlay.classList.remove('hidden');
        overlayText.textContent = 'Connecting…';
        break;
    case 'connected':
        overlay.classList.add('hidden');
        break;
    case 'disconnected':
        overlay.classList.remove('hidden');
        overlayText.textContent = 'Disconnected – waiting for stream…';
        break;
    }
}

function setStatusLine(text) {
    console.log('setStatusLine: ' + text);
    statusLineEl.innerHTML = text;
}

function appendLog(message, type = 'info') {
    const div = document.createElement('div');
    div.className = 'log-line' + (type === 'error' ? ' error' : '');
    const time = new Date().toLocaleTimeString();
    div.innerHTML = `<span>[${time}]</span> <span class="log-tag">${
        type === 'error' ? 'ERR' : 'LOG'}</span>${message}`;
    logEl.appendChild(div);
    logEl.scrollTop = logEl.scrollHeight;

    console[type === 'error' ? 'error' : 'log']('[WebRTC UI]', message);
}

// ===== WEBRTC + SIGNALING LOGIC =====
const WS_PROTO = location.protocol === 'https:' ? 'wss' : 'ws';
const WS_HOST = location.hostname;

// Prefer configured wsPort, fallback to page port if missing
const WS_PORT = (window.PEK_CONFIG && window.PEK_CONFIG.wsPort) ||
                (location.port || (location.protocol === 'https:' ? 443 : 80));

const SIGNALING_URL = `${WS_PROTO}://${WS_HOST}:${WS_PORT}/ws`;
const WEBRTC_TIMING_CONFIG = resolveWebRtcTimingConfig(window.PEK_CONFIG || {});

const client = createWebRtcClient({
    video,
    signalingUrl: SIGNALING_URL,
    ...WEBRTC_TIMING_CONFIG,
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

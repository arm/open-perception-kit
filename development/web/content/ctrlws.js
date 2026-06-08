
import {setPlayPause} from "./video-controls.js?v=dependency-toggle-lock-20260608"
import {enableAudioButton} from "./audio.js?v=dependency-toggle-lock-20260608"
import {modelsManager} from "./models.js?v=dependency-toggle-lock-20260608"
import {renderPerformanceMetrics} from "./performance-metrics.js?v=dependency-toggle-lock-20260608"
import {renderInferenceOutput} from "./inference-output.js?v=dependency-toggle-lock-20260608"

const CTRL_PROTO = location.protocol === 'https:' ? 'wss' : 'ws';
const CTRL_HOST = location.hostname;

function fallbackCtrlPort() {
    const pagePort = Number.parseInt(location.port || '', 10);
    if (pagePort === 9999 || pagePort === 10099) {
        return 8001;
    }

    return location.port || (location.protocol === 'https:' ? 443 : 80);
}

const CTRL_PORT = (window.PEK_CONFIG && window.PEK_CONFIG.ctrlPort) || fallbackCtrlPort();

function sameOriginCtrlProxyUrl() {
    const proxyPath = window.PEK_CONFIG?.ctrlProxyPath;
    if (proxyPath) {
        return `${CTRL_PROTO}://${CTRL_HOST}:${location.port || (location.protocol === 'https:' ? 443 : 80)}${proxyPath}`;
    }

    if (Number.parseInt(location.port || '', 10) === 9999) {
        return `${CTRL_PROTO}://${CTRL_HOST}:${location.port}/ctrl-ws`;
    }

    return "";
}

const CTRL_URL = sameOriginCtrlProxyUrl() || `${CTRL_PROTO}://${CTRL_HOST}:${CTRL_PORT}/ws`;

let ctrl = null;

let ctrlReconnectDelay = 1000;
const CTRL_RECONNECT_DELAY_MAX = 15000;
const CTRL_BACKOFF_FACTOR = 1.1;
const CTRL_HTTP_FALLBACK_URL = "/api/ctrl-snapshot";
const CTRL_HTTP_FALLBACK_INTERVAL = 1200;
const CTRL_HTTP_FALLBACK_AFTER_MS = 1800;

const muteBtn = document.getElementById("muteUnmuteBtn");
let backendReloadScheduled = false;
const RESTART_SESSION_KEY = "pekPipelineRestarting";
let lastCtrlMessageAt = 0;
let fallbackInFlight = false;

function isPipelineRestarting() {
    return document.body.classList.contains("is-pipeline-restarting") ||
        sessionStorage.getItem(RESTART_SESSION_KEY) === "true";
}

function reloadOnBackendShutdown() {
    if (backendReloadScheduled || isPipelineRestarting() || window.PEK_CONFIG?.supervised) {
        return;
    }

    backendReloadScheduled = true;
    setTimeout(() => {
        window.location.reload();
    }, 300);
}

function connectCtrl(manual = false) {
    if (ctrl && (ctrl.readyState === WebSocket.OPEN ||
                      ctrl.readyState === WebSocket.CONNECTING)) {
        return;
    }

    if (manual) {
        ctrlReconnectDelay = 1000;
    }

    ctrl = new WebSocket(CTRL_URL);

    ctrl.onopen = () => {
        ctrlReconnectDelay = 1000;
        backendReloadScheduled = false;
        sessionStorage.removeItem(RESTART_SESSION_KEY);
        flushQueue();
        window.dispatchEvent(new CustomEvent("ctrl-connection", {
            detail: { state: "connected" },
        }));
    };

    ctrl.onmessage = (event) => {
        handleCtrlData(JSON.parse(event.data));
    };

    ctrl.onerror =
        (err) => { ; };

    ctrl.onclose = () => {
        window.dispatchEvent(new CustomEvent("ctrl-connection", {
            detail: { state: "disconnected" },
        }));
        reloadOnBackendShutdown();
        setTimeout(() => {
            ctrlReconnectDelay = Math.min(ctrlReconnectDelay * CTRL_BACKOFF_FACTOR, CTRL_RECONNECT_DELAY_MAX);
            connectCtrl();
        }, ctrlReconnectDelay);
    };
} // connectCtrl

function handleCtrlData(data) {
    if (!data || data.error) return;

    lastCtrlMessageAt = Date.now();
    if (data.perception_data) {
        data.performance ??= data.perception_data.performance;
        data.inference_output ??= data.perception_data.inference_output;
    }
    window.dispatchEvent(new CustomEvent("ctrl-message", {
        detail: data,
    }));

    if (data.pipeline_restart) {
        window.dispatchEvent(new CustomEvent("pipeline-restart", {
            detail: data.pipeline_restart,
        }));
    }

    if (data.pipeline_switch) {
        window.dispatchEvent(new CustomEvent("pipeline-switch", {
            detail: data.pipeline_switch,
        }));
    }

    if (data.pipeline_state) {
        enableAudioButton(data.pipeline_state.audio);
        setPlayPause(data.pipeline_state.playing);
    }

    if (data.models) {
        modelsManager.render(data.models);
    }

    if (data.performance) {
        renderPerformanceMetrics(data.performance);
    }

    if (data.inference_output) {
        renderInferenceOutput(data.inference_output);
    }
}

async function pollCtrlSnapshot(force = false) {
    if (fallbackInFlight) return;
    if (!force && Date.now() - lastCtrlMessageAt < CTRL_HTTP_FALLBACK_AFTER_MS) return;

    fallbackInFlight = true;
    try {
        const response = await fetch(`${CTRL_HTTP_FALLBACK_URL}?t=${Date.now()}`, {
            cache: "no-store",
        });
        if (response.ok) {
            handleCtrlData(await response.json());
        }
    } catch (error) {
        console.warn("Control HTTP fallback failed", error);
    } finally {
        fallbackInFlight = false;
    }
}

// --- Shared send API (safe + works during reconnect) ---
const sendQueue = [];

function flushQueue() {
  while (ctrl && ctrl.readyState === WebSocket.OPEN && sendQueue.length) {
    ctrl.send(sendQueue.shift());
  }
}

/**
 * Send a JS object as JSON over ctrl websocket.
 * If not connected yet, queues and sends after reconnect.
 */
export function ctrlSend(obj) {
  const payload = JSON.stringify(obj);

  if (ctrl && ctrl.readyState === WebSocket.OPEN) {
    ctrl.send(payload);
    return true;
  }

  // Queue while connecting/reconnecting
  sendQueue.push(payload);

  // Kick connection if it isn't already going
  connectCtrl();
  return false;
}

export function ctrlIsOpen() {
  return !!ctrl && ctrl.readyState === WebSocket.OPEN;
}

connectCtrl();
setTimeout(() => pollCtrlSnapshot(true), CTRL_HTTP_FALLBACK_AFTER_MS);
setInterval(() => pollCtrlSnapshot(), CTRL_HTTP_FALLBACK_INTERVAL);

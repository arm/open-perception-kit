import {setPlayPause} from "./video-controls.js"
import {enableAudioButton} from "./audio.js"
import {setPerfOverlayButton} from "./perf_overlay.js"
import {modelsManager} from "./models.js"

const CTRL_PROTO = location.protocol === 'https:' ? 'wss' : 'ws';
const CTRL_HOST = location.hostname;

const CTRL_PORT = (window.PEK_CONFIG && window.PEK_CONFIG.ctrlPort) ||
                (location.port || (location.protocol === 'https:' ? 443 : 80));

const CTRL_URL = `${CTRL_PROTO}://${CTRL_HOST}:${CTRL_PORT}/ws`;

let ctrl = null;

let ctrlReconnectDelay = 1000;
const CTRL_RECONNECT_DELAY_MAX = 15000;
const CTRL_BACKOFF_FACTOR = 1.1;

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
        flushQueue();
    };

    ctrl.onmessage = async (event) => {
        const data = JSON.parse(event.data);
        console.log("ctrl.onmessage: " + event.data);

        enableAudioButton(data.pipeline_state.audio);
        setPlayPause(data.pipeline_state.playing);
        setPerfOverlayButton(data.perf_overlay.enabled);
        modelsManager.render(data.models);
    };

    ctrl.onerror = () => { ; };

    ctrl.onclose = () => {
        setTimeout(() => {
            ctrlReconnectDelay = Math.min(ctrlReconnectDelay * CTRL_BACKOFF_FACTOR, CTRL_RECONNECT_DELAY_MAX);
            connectCtrl();
        }, ctrlReconnectDelay);
    };
}

const sendQueue = [];

function flushQueue() {
    while (ctrl && ctrl.readyState === WebSocket.OPEN && sendQueue.length) {
        ctrl.send(sendQueue.shift());
    }
}

export function ctrlSend(obj) {
    const payload = JSON.stringify(obj);

    if (ctrl && ctrl.readyState === WebSocket.OPEN) {
        ctrl.send(payload);
        return true;
    }

    sendQueue.push(payload);
    connectCtrl();
    return false;
}

export function ctrlIsOpen() {
    return !!ctrl && ctrl.readyState === WebSocket.OPEN;
}

connectCtrl();

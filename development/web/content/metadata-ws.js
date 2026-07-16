const WS_PROTO = location.protocol === 'https:' ? 'wss' : 'ws';
const WS_HOST = location.hostname;
const DEFAULT_METADATA_PORT = 7001;
const RECONNECT_DELAY_MS = 1500;
const RECONNECT_DELAY_MAX_MS = 15000;
const BACKOFF_FACTOR = 1.2;

function metadataUrl() {
    const config = window.PEK_CONFIG || {};
    if (config.metadataUrl) {
        return config.metadataUrl;
    }

    const port = config.metadataPort || DEFAULT_METADATA_PORT;
    const endpoint = config.metadataEndpoint || '/ws';
    return `${WS_PROTO}://${WS_HOST}:${port}${endpoint}`;
}

function normaliseInferenceOutput(perception) {
    const layers = Array.isArray(perception?.layers) ? perception.layers : [];
    return {
        layers: layers.map((layer) => {
            const detections = Array.isArray(layer?.detections) ? layer.detections : [];
            return {
                ...layer,
                count: Number.isFinite(layer?.count) ? layer.count : detections.length,
            };
        }),
    };
}

function normalisePerformance(perception) {
    return {
        lines: Array.isArray(perception?.perfdata) ? perception.perfdata : [],
    };
}

function handleMetadataMessage(raw) {
    let message;
    try {
        message = JSON.parse(String(raw).trim());
    } catch (error) {
        console.warn('Invalid metadata WebSocket message', error);
        return;
    }

    const perception = message?.perception;
    if (!perception) {
        renderInferenceOutput(null);
        renderPerformanceMetrics(null);
        return;
    }

    const inference_output = normaliseInferenceOutput(perception);
    const performance = normalisePerformance(perception);

    window.dispatchEvent(new CustomEvent('metadata-message', {
        detail: {
            frame_counter: message.frame_counter,
            perception,
            inference_output,
            performance,
        },
    }));
}

let socket = null;
let reconnectDelay = RECONNECT_DELAY_MS;

function connect() {
    if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
        return;
    }

    socket = new WebSocket(metadataUrl());

    socket.onopen = () => {
        reconnectDelay = RECONNECT_DELAY_MS;
    };

    socket.onmessage = (event) => {
        handleMetadataMessage(event.data);
    };

    socket.onerror = () => { ; };

    socket.onclose = () => {
        window.setTimeout(() => {
            reconnectDelay = Math.min(reconnectDelay * BACKOFF_FACTOR, RECONNECT_DELAY_MAX_MS);
            connect();
        }, reconnectDelay);
    };
}

connect();

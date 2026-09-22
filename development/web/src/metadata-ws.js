import {decodeFrameResultsMessage} from './frame-results.js';

const WS_PROTO = location.protocol === 'https:' ? 'wss' : 'ws';
const WS_HOST = location.hostname;
const DEFAULT_METADATA_PORT = 8002;
const RECONNECT_DELAY_MS = 1500;
const RECONNECT_DELAY_MAX_MS = 15000;
const BACKOFF_FACTOR = 1.2;

function metadataUrl() {
    const config = window.OPK_CONFIG || {};
    if (config.metadataUrl) {
        return config.metadataUrl;
    }

    const port = config.metadataPort || DEFAULT_METADATA_PORT;
    const endpoint = config.metadataEndpoint || '/ws';
    return `${WS_PROTO}://${WS_HOST}:${port}${endpoint}`;
}

function normaliseInferenceOutput(frameResults) {
    const layers = Array.isArray(frameResults?.layers) ? frameResults.layers : [];
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

function normalisePerformance(frameResults) {
    return {
        lines: Array.isArray(frameResults?.perfdata) ? frameResults.perfdata : [],
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

    if (message?.frame_results_encoding == null && message?.frame_results_packet_b64 == null) {
        window.dispatchEvent(new CustomEvent('metadata-message', {
            detail: {
                frame_counter: Number.isInteger(message?.frame_counter) ? message.frame_counter : undefined,
                frame_results: null,
                inference_output: {layers: []},
                performance: {lines: []},
                decode_error: null,
            },
        }));
        return;
    }

    let decoded;
    try {
        decoded = decodeFrameResultsMessage(message);
    } catch (error) {
        console.warn('Invalid FrameResults metadata message', error);
        window.dispatchEvent(new CustomEvent('metadata-message', {
            detail: {
                frame_counter: Number.isInteger(message?.frame_counter) ? message.frame_counter : undefined,
                frame_results: null,
                inference_output: { layers: [] },
                performance: { lines: [] },
                decode_error: error instanceof Error ? error.message : String(error),
            },
        }));
        return;
    }

    const inference_output = normaliseInferenceOutput(decoded.frame_results);
    const performance = normalisePerformance(decoded.frame_results);

    window.dispatchEvent(new CustomEvent('metadata-message', {
        detail: {
            frame_counter: decoded.frame_counter,
            frame_results: decoded.frame_results,
            inference_output,
            performance,
            decode_error: null,
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

/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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

function publishMetadata(frameCounter, frameResults, decodeError = null) {
    window.dispatchEvent(new CustomEvent('metadata-message', {
        detail: {
            frame_counter: Number.isInteger(frameCounter) ? frameCounter : undefined,
            frame_results: frameResults,
            inference_output: normaliseInferenceOutput(frameResults),
            performance: normalisePerformance(frameResults),
            decode_error: decodeError,
        },
    }));
}

function handleMetadataMessage(raw) {
    let message;
    try {
        message = JSON.parse(String(raw).trim());
    } catch (error) {
        console.warn('Invalid metadata WebSocket message', error);
        publishMetadata(undefined, null, 'Invalid metadata JSON');
        return;
    }

    if (message?.frame_results_encoding == null && message?.frame_results_packet_b64 == null) {
        publishMetadata(message?.frame_counter, null);
        return;
    }

    let decoded;
    try {
        decoded = decodeFrameResultsMessage(message);
    } catch (error) {
        console.warn('Invalid FrameResults metadata message', error);
        publishMetadata(message?.frame_counter, null, error instanceof Error ? error.message : String(error));
        return;
    }

    publishMetadata(decoded.frame_counter, decoded.frame_results);
}

// The WebRTC client emits this only for metadata matched by RTP timestamp to
// the displayed frame. Keep wire decoding here, alongside the live channel.
window.addEventListener('video-frame-metadata', (event) => {
    let frame = null;
    if (event.detail) {
        try {
            frame = decodeFrameResultsMessage(event.detail).frame_results;
        } catch (error) {
            console.warn('Invalid video frame metadata', error);
        }
    }
    window.dispatchEvent(new CustomEvent('video-frame-results', {detail: frame}));
});

let socket = null;
let reconnectDelay = RECONNECT_DELAY_MS;

function connect() {
    if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
        return;
    }

    socket = new WebSocket(metadataUrl());

    socket.onopen = () => {
        reconnectDelay = RECONNECT_DELAY_MS;
        window.dispatchEvent(new CustomEvent('metadata-connection', {detail: {connected: true}}));
    };

    socket.onmessage = (event) => {
        handleMetadataMessage(event.data);
    };

    socket.onerror = () => { ; };

    socket.onclose = () => {
        window.dispatchEvent(new CustomEvent('metadata-connection', {detail: {connected: false}}));
        window.setTimeout(() => {
            reconnectDelay = Math.min(reconnectDelay * BACKOFF_FACTOR, RECONNECT_DELAY_MAX_MS);
            connect();
        }, reconnectDelay);
    };
}

connect();

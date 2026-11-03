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

import {MicrophoneClient} from './microphone_client.js';
import {resolveWebRtcIceConfig} from './webrtc_config.js';

const button = document.getElementById('microphoneBtn');
const icon = document.getElementById('microphoneIcon');
const status = document.getElementById('microphoneStatus');
const cameraButton = document.getElementById('cameraBtn');
const cameraIcon = document.getElementById('cameraIcon');
const cameraStatus = document.getElementById('cameraStatus');
let microphoneAvailable = false;
const microphoneConfig = window.OPK_CONFIG || {};
const signalingUrl = new URL(location.href);
signalingUrl.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
signalingUrl.port = String(microphoneConfig.micWsPort ?? 8003);
signalingUrl.pathname = '/ws';
signalingUrl.search = '';
signalingUrl.hash = '';

const client = new MicrophoneClient({
    getUserMedia: constraints => navigator.mediaDevices.getUserMedia(constraints),
    createPeer: options => new RTCPeerConnection(options),
    createSocket: url => new WebSocket(url),
    signalingUrl: signalingUrl.href,
    ...resolveWebRtcIceConfig(microphoneConfig),
    onStatus: (active, message, kind) => {
        const control = kind === 'video' ? cameraButton : button;
        const indicator = kind === 'video' ? cameraIcon : icon;
        const output = kind === 'video' ? cameraStatus : status;
        const name = kind === 'video' ? 'camera' : 'microphone';
        const glyph = kind === 'video' ? 'video' : 'mic';
        control.setAttribute('aria-pressed', String(active));
        control.setAttribute('aria-label', active ? `Stop sharing ${name}` : `Share ${name}`);
        control.title = message;
        indicator.dataset.icon = active ? glyph : `${glyph}-off`;
        output.textContent = message;
    },
});

export function enableMicrophoneButton(enable) {
    microphoneAvailable = enable === true;
    button.style.display = microphoneAvailable ? 'inline-flex' : 'none';
    button.disabled = !microphoneAvailable;
    status.hidden = !microphoneAvailable;
    cameraButton.style.display = microphoneAvailable ? 'inline-flex' : 'none';
    cameraButton.disabled = !microphoneAvailable;
    cameraStatus.hidden = !microphoneAvailable;
    if (!microphoneAvailable)
        client.stop();
}

button.addEventListener('click', () => {
    if (!microphoneAvailable)
        return;
    if (client.devices.has('audio')) {
        client.stopTrack('audio');
    } else if (!navigator.mediaDevices?.getUserMedia) {
        status.textContent = 'Microphone unavailable. Open this page through localhost.';
    } else {
        void client.start();
    }
});
cameraButton.addEventListener('click', () => {
    if (!microphoneAvailable) return;
    if (client.devices.has('video')) {
        client.stopTrack('video');
    } else if (!navigator.mediaDevices?.getUserMedia) {
        cameraStatus.textContent = 'Camera unavailable. Open this page through localhost.';
    } else {
        void client.start('video');
    }
});
window.addEventListener('pagehide', () => client.stop());

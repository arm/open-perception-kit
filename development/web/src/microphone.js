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
    onStatus: (active, message) => {
        button.setAttribute('aria-pressed', String(active));
        button.setAttribute('aria-label', active ? 'Stop sharing microphone' : 'Share microphone');
        button.title = message;
        icon.className = 'video-control-icon fa-solid ' +
            (active ? 'fa-microphone' : 'fa-microphone-slash');
        status.textContent = message;
    },
});

export function enableMicrophoneButton(enable) {
    microphoneAvailable = enable === true;
    button.style.display = microphoneAvailable ? 'inline-flex' : 'none';
    button.disabled = !microphoneAvailable;
    status.hidden = !microphoneAvailable;
    if (!microphoneAvailable)
        client.stop();
}

button.addEventListener('click', () => {
    if (!microphoneAvailable)
        return;
    if (client.session) {
        client.stop();
    } else if (!navigator.mediaDevices?.getUserMedia) {
        status.textContent = 'Microphone unavailable. Open this page through localhost.';
    } else {
        void client.start();
    }
});
window.addEventListener('pagehide', () => client.stop());

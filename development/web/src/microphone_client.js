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

export class MicrophoneClient {
    constructor({getUserMedia, createPeer, createSocket, signalingUrl, iceServers,
                 onStatus = () => {}, timeoutMs = 15000}) {
        Object.assign(this, {getUserMedia, createPeer, createSocket, signalingUrl,
                             iceServers, onStatus, timeoutMs});
        this.session = null;
    }

    stop(message = 'Microphone off') {
        const session = this.session;
        this.session = null;
        if (session) {
            clearTimeout(session.timer);
            session.stream?.getTracks().forEach(track => track.stop());
            session.pc?.close();
            session.ws?.close();
        }
        this.onStatus(false, message);
    }

    async start() {
        if (this.session)
            return;
        const session = {localIce: [], remoteIce: [], offered: false, messages: Promise.resolve()};
        this.session = session;
        const current = () => this.session === session;
        const fail = error => {
            if (current())
                this.stop('Microphone: ' + (error.message || String(error)));
        };
        this.onStatus(true, 'Waiting for microphone permission…');
        try {
            const stream = await this.getUserMedia({audio: true, video: false});
            if (!current()) {
                stream.getTracks().forEach(track => track.stop());
                return;
            }
            session.stream = stream;
            const tracks = stream.getAudioTracks();
            if (!tracks.length)
                throw new Error('No microphone audio track');
            this.onStatus(true, 'Connecting microphone…');
            const pc = session.pc = this.createPeer({iceServers: this.iceServers});
            for (const track of tracks) {
                pc.addTransceiver(track, {direction: 'sendonly', streams: [stream]});
                track.onended = () => fail(new Error('Microphone disconnected'));
            }
            const ws = session.ws = this.createSocket(this.signalingUrl);
            session.timer = setTimeout(() => fail(new Error('Connection timed out')), this.timeoutMs);
            const send = message => ws.send(JSON.stringify(message));
            pc.onicecandidate = event => {
                if (!current() || !event.candidate)
                    return;
                const message = {type: 'candidate', ice: event.candidate};
                try {
                    if (session.offered)
                        send(message);
                    else
                        session.localIce.push(message);
                } catch (error) {
                    fail(error);
                }
            };
            pc.onconnectionstatechange = () => {
                if (!current())
                    return;
                if (pc.connectionState === 'connected') {
                    clearTimeout(session.timer);
                    this.onStatus(true, 'Microphone sharing');
                } else if (['failed', 'closed', 'disconnected'].includes(pc.connectionState)) {
                    fail(new Error('Connection lost'));
                }
            };
            ws.onopen = async () => {
                try {
                    if (!current())
                        return;
                    const offer = await pc.createOffer();
                    if (!current())
                        return;
                    await pc.setLocalDescription(offer);
                    if (!current())
                        return;
                    send({type: 'offer', sdp: pc.localDescription.sdp});
                    session.offered = true;
                    for (const message of session.localIce)
                        send(message);
                    session.localIce = [];
                } catch (error) {
                    fail(error);
                }
            };
            ws.onmessage = event => {
                session.messages = session.messages.then(async () => {
                    if (!current())
                        return;
                    const message = JSON.parse(event.data);
                    if (message.type === 'answer' && typeof message.sdp === 'string') {
                        await pc.setRemoteDescription({type: 'answer', sdp: message.sdp});
                        for (const candidate of session.remoteIce) {
                            if (!current())
                                return;
                            await pc.addIceCandidate(candidate);
                        }
                        session.remoteIce = [];
                    } else if (message.type === 'candidate' && message.ice) {
                        if (pc.remoteDescription)
                            await pc.addIceCandidate(message.ice);
                        else
                            session.remoteIce.push(message.ice);
                    } else {
                        throw new Error('Invalid signaling response');
                    }
                }).catch(fail);
            };
            ws.onerror = () => fail(new Error('Signaling unavailable'));
            ws.onclose = () => fail(new Error('Signaling disconnected'));
        } catch (error) {
            fail(error);
        }
    }
}

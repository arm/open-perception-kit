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
        this.devices = new Map();
    }

    stop(message = 'Microphone off') {
        for (const kind of [...this.devices.keys()])
            this.release(kind, message);
        const session = this.session;
        this.session = null;
        if (session) {
            clearTimeout(session.timer);
            session.pc?.close();
            session.ws?.close();
        }
    }

    release(kind, message) {
        const device = this.devices.get(kind);
        if (!device) return;
        this.devices.delete(kind);
        device.stream?.getTracks().forEach(track => track.stop());
        if (device.transceiver) {
            this.session.pc.removeTrack(device.transceiver.sender);
            device.transceiver.direction = 'inactive';
        }
        this.onStatus(false, message, kind);
    }

    stopTrack(kind, message = `${kind === 'audio' ? 'Microphone' : 'Camera'} off`) {
        const attached = Boolean(this.devices.get(kind)?.transceiver);
        this.release(kind, message);
        if (!this.devices.size) this.stop();
        else if (attached) this.session?.negotiate();
    }

    async start(kind = 'audio') {
        if (!['audio', 'video'].includes(kind)) throw new Error('Invalid capture kind');
        if (this.devices.has(kind))
            return;
        const label = kind === 'audio' ? 'Microphone' : 'Camera';
        const device = {};
        this.devices.set(kind, device);
        this.onStatus(true, `Waiting for ${label.toLowerCase()} permission…`, kind);
        try {
            const stream = await this.getUserMedia({
                audio: kind === 'audio' ? {echoCancellation: true} : false,
                video: kind === 'video',
            });
            if (this.devices.get(kind) !== device) {
                stream.getTracks().forEach(track => track.stop());
                return;
            }
            device.stream = stream;
            const track = stream.getTracks().find(track => track.kind === kind);
            if (!track) throw new Error(`No ${label.toLowerCase()} track`);
            if (!this.session) this.connect();
            device.transceiver = this.session.pc.addTransceiver(track, {direction: 'sendonly', streams: [stream]});
            track.onended = () => {
                if (this.devices.get(kind) === device)
                    this.stopTrack(kind, `${label} disconnected`);
            };
            this.onStatus(true, `Connecting ${label.toLowerCase()}…`, kind);
            this.session.negotiate();
        } catch (error) {
            if (this.devices.get(kind) === device)
                this.stopTrack(kind, `${label}: ${error.message || String(error)}`);
        }
    }

    connect() {
        const session = {localIce: [], remoteIce: [], offered: false, messages: Promise.resolve()};
        this.session = session;
        const current = () => this.session === session;
        const fail = error => {
            if (current())
                this.stop('Capture: ' + (error.message || String(error)));
        };
        try {
            const pc = session.pc = this.createPeer({iceServers: this.iceServers});
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
                    if (!session.negotiating) clearTimeout(session.timer);
                    for (const [kind, device] of this.devices) {
                        if (device.transceiver)
                            this.onStatus(true, `${kind === 'audio' ? 'Microphone' : 'Camera'} sharing`, kind);
                    }
                } else if (['failed', 'closed', 'disconnected'].includes(pc.connectionState)) {
                    fail(new Error('Connection lost'));
                }
            };
            session.negotiate = async () => {
                session.dirty = true;
                if (!session.open || session.negotiating || !current()) return;
                session.negotiating = true;
                session.dirty = false;
                session.offered = false;
                clearTimeout(session.timer);
                session.timer = setTimeout(() => fail(new Error('Negotiation timed out')), this.timeoutMs);
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
            ws.onopen = () => {
                session.open = true;
                return session.negotiate();
            };
            ws.onmessage = event => {
                session.messages = session.messages.then(async () => {
                    if (!current())
                        return;
                    const message = JSON.parse(event.data);
                    if (message.type === 'answer' && typeof message.sdp === 'string') {
                        if (!session.negotiating || !session.offered)
                            throw new Error('Unexpected answer');
                        await pc.setRemoteDescription({type: 'answer', sdp: message.sdp});
                        if (!current()) return;
                        for (const candidate of session.remoteIce) {
                            if (!current())
                                return;
                            await pc.addIceCandidate(candidate);
                        }
                        session.remoteIce = [];
                        session.negotiating = false;
                        if (pc.connectionState === 'connected') {
                            clearTimeout(session.timer);
                            for (const [kind, device] of this.devices) {
                                if (device.transceiver)
                                    this.onStatus(true, `${kind === 'audio' ? 'Microphone' : 'Camera'} sharing`, kind);
                            }
                        }
                        if (session.dirty) await session.negotiate();
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

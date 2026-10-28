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

import assert from 'node:assert/strict';
import test from 'node:test';

import {createWebRtcClient} from '../src/webrtc_client.js';
import './microphone.test.js';

test('only a video frame reports connected, and late negotiation cannot downgrade it', async () => {
    const env = createEnv();
    const client = env.createClient();

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.latestSocket().receive({type: 'answer', sdp: 'answer'});
    await env.flush();
    const track = env.attachVideoTrack();
    track.onunmute();
    client.setPaused(true);
    client.setPaused(false);
    env.latestPeerConnection().setIceState('connected');
    env.latestPeerConnection().setIceState('completed');
    await env.flush();

    assert.equal(env.statuses.some(({state}) => state === 'connected'), false);
    assert.equal(env.statusLines.at(-1), 'Waiting for video…');

    env.video.emitFrame();
    assert.equal(env.statuses.at(-1).state, 'connected');
    assert.equal(env.statusLines.at(-1), 'Connected');
    const statusCount = env.statuses.length;
    const lineCount = env.statusLines.length;
    env.video.emitFrame();
    assert.equal(env.statuses.length, statusCount);
    assert.equal(env.statusLines.length, lineCount);

    env.latestSocket().receive({type: 'answer', sdp: 'late-answer'});
    await env.flush();
    env.latestPeerConnection().setIceState('connected');
    env.latestPeerConnection().setIceState('completed');
    assert.equal(env.statuses.at(-1).state, 'connected');
    assert.equal(env.statusLines.at(-1), 'Connected');
    assert.deepEqual(env.statuses.map(({subtext}) => subtext), env.statusLines);
});

test('ICE failure remains visible through retry and clears on the first video frame', async () => {
    const env = createEnv();
    const client = env.createClient({reconnectDelayMs: 25});
    const message = 'Connection failed. Try TURN.';

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.latestPeerConnection().setIceState('failed');
    assert.equal(env.statuses.at(-1).state, 'reconnecting');
    assert.equal(env.statuses.at(-1).subtext, message);
    assert.equal(env.statusLines.at(-1), message);

    env.clock.tick(24);
    assert.equal(env.peerConnections.length, 1);
    env.clock.tick(1);
    const retryStatusCount = env.statuses.length - 1;
    const retryLineCount = env.statusLines.length - 1;
    env.openLatestSocket();
    await env.flush();
    env.latestSocket().receive({type: 'answer', sdp: 'answer'});
    await env.flush();
    const track = env.attachVideoTrack();
    track.onunmute();
    client.setPaused(true);
    client.setPaused(false);
    env.latestPeerConnection().setIceState('connected');
    assert.equal(env.peerConnections.length, 2);
    assert.ok(env.statuses.slice(retryStatusCount).every(({subtext}) => subtext === message));
    assert.ok(env.statusLines.slice(retryLineCount).every((line) => line === message));

    env.video.emitFrame();
    assert.equal(env.statuses.at(-1).state, 'connected');
    assert.equal(env.statusLines.at(-1), 'Connected');

    env.latestSocket().serverClose();
    assert.equal(env.statusLines.at(-1), 'Signaling unavailable. Check server.');
    client.stop();
    client.start();
    assert.equal(env.statusLines.at(-1), 'Connecting…');
});

test('ICE failure recognizes TURN and TLS TURN in string and array URLs', async () => {
    for (const urls of [
        'turn:relay.test:3478',
        ['stun:relay.test:3478', 'turn:relay.test:3478'],
        'turns:relay.test:5349',
        ['stun:relay.test:3478', 'turns:relay.test:5349'],
    ]) {
        const env = createEnv();
        const iceServers = [{urls}];
        const client = env.createClient({iceServers});

        client.start();
        env.openLatestSocket();
        await env.flush();
        env.latestPeerConnection().setIceState('failed');

        assert.equal(env.statusLines.at(-1), 'Connection failed. Check network/TURN.');
        assert.deepEqual(env.latestPeerConnection().options, {iceServers});
        assert.deepEqual(env.latestPeerConnection().transceivers, [
            {kind: 'video', options: {direction: 'recvonly'}},
            {kind: 'audio', options: {direction: 'recvonly'}},
        ]);
    }
});

test('disconnection and missing video report their own short errors', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 100, reconnectDelayMs: 5});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.latestPeerConnection().setIceState('disconnected');
    assert.equal(env.statusLines.at(-1), 'Connection lost. Retrying…');

    env.clock.tick(5);
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();
    env.video.emitFrame();
    env.clock.tick(100);
    assert.equal(env.statusLines.at(-1), 'No video. Retrying…');
    assert.equal(client.getDebugState().restartTimerCount, 1);
});

test('signaling errors report the server without adding a restart', async () => {
    const env = createEnv();
    const client = env.createClient({reconnectDelayMs: 25});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();
    env.video.emitFrame();
    env.latestSocket().onerror(new Error('unavailable'));

    assert.equal(env.statuses.at(-1).state, 'disconnected');
    assert.equal(env.statusLines.at(-1), 'Signaling unavailable. Check server.');
    assert.equal(client.getDebugState().restartTimerCount, 0);
    assert.equal(env.latestPeerConnection().closeCount, 0);
    env.latestPeerConnection().setIceState('completed');
    assert.equal(env.statusLines.at(-1), 'Signaling unavailable. Check server.');

    env.latestSocket().serverClose();
    assert.equal(env.statusLines.at(-1), 'Signaling unavailable. Check server.');
    assert.equal(client.getDebugState().restartTimerCount, 1);
});

test('offer and malformed signaling failures report negotiation without a TURN hint', async () => {
    for (const failure of ['offer', 'message']) {
        const env = createEnv();
        const client = env.createClient({reconnectDelayMs: 25});

        client.start();
        if (failure === 'offer') {
            env.latestPeerConnection().createOffer = async () => {
                throw new Error('offer failed');
            };
        }
        env.openLatestSocket();
        await env.flush();
        if (failure === 'message')
            await env.latestSocket().onmessage({data: '{'});

        assert.equal(env.statusLines.at(-1), 'Negotiation failed. Retrying…');
        assert.equal(client.getDebugState().restartTimerCount, 1);
        assert.equal(env.latestPeerConnection().closeCount, 1);
    }
});

test('frame heartbeat keeps the session alive', async () => {
    const env = createEnv();
    const frames = [];
    const client = env.createClient({
        frameTimeoutMs: 1000, reconnectDelayMs: 0,
        onVideoFrame: (message) => frames.push(message),
    });

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();

    env.clock.tick(900);
    const metadata = {type: 'frame-metadata', rtp_timestamp: 42};
    env.latestSocket().receive(metadata);
    env.latestSocket().receive({type: 'frame-metadata', rtp_timestamp: 43});
    env.video.emitFrame(42);
    assert.deepEqual(frames.at(-1), metadata);
    env.video.emitFrame(44);
    assert.equal(frames.at(-1), null);
    env.video.emitFrame(42);
    assert.equal(frames.at(-1), null);
    env.clock.tick(900);

    assert.equal(env.peerConnections.length, 1);
    assert.equal(client.getDebugState().restartTimerCount, 0);
});

test('ICE failed schedules exactly one restart', async () => {
    const env = createEnv();
    const client = env.createClient({reconnectDelayMs: 25});

    client.start();
    env.openLatestSocket();
    await env.flush();

    const firstPc = env.latestPeerConnection();
    firstPc.setIceState('failed');
    firstPc.setIceState('disconnected');

    assert.equal(client.getDebugState().restartTimerCount, 1);
    assert.equal(firstPc.closeCount, 1);

    env.clock.tick(25);
    await env.flush();

    assert.equal(env.peerConnections.length, 2);
});

test('WebSocket close closes the old peer and starts a new offer cycle', async () => {
    const env = createEnv();
    const frames = [];
    const client = env.createClient({
        reconnectDelayMs: 10,
        onVideoFrame: (message) => frames.push(message),
    });

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();
    const metadata = {type: 'frame-metadata', rtp_timestamp: 42};
    env.latestSocket().receive(metadata);
    env.video.emitFrame(42);
    assert.deepEqual(frames.at(-1), metadata);

    const firstSocket = env.latestSocket();
    const firstPc = env.latestPeerConnection();
    firstSocket.serverClose();

    assert.equal(firstPc.closeCount, 1);
    assert.equal(frames.at(-1), null);

    env.clock.tick(10);
    env.openLatestSocket();
    await env.flush();

    assert.equal(env.webSockets.length, 2);
    assert.equal(env.latestSocket().sentMessages[0].type, 'offer');
});

test('connected but no frames restarts through the watchdog', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 100, reconnectDelayMs: 5});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();

    env.clock.tick(100);
    assert.equal(client.getDebugState().restartTimerCount, 1);

    env.clock.tick(5);
    await env.flush();
    assert.equal(env.peerConnections.length, 2);
});

test('intentional pause suspends the frame watchdog until resume', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 100, reconnectDelayMs: 5});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();

    client.setPaused(true);
    env.clock.tick(500);

    assert.equal(env.peerConnections.length, 1);
    assert.equal(client.getDebugState().restartTimerCount, 0);

    client.setPaused(false);
    env.clock.tick(100);

    assert.equal(client.getDebugState().restartTimerCount, 1);
});

test('playing report before video does not start the frame watchdog', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 100, reconnectDelayMs: 5});

    client.start();
    env.openLatestSocket();
    await env.flush();
    client.setPaused(false);
    env.clock.tick(100);

    assert.equal(client.getDebugState().restartTimerCount, 0);

    env.attachVideoTrack();
    env.clock.tick(100);

    assert.equal(client.getDebugState().restartTimerCount, 1);
});

test('redundant playing report does not refresh the frame watchdog', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 100, reconnectDelayMs: 5});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();
    env.clock.tick(90);
    client.setPaused(false);
    env.clock.tick(10);

    assert.equal(client.getDebugState().restartTimerCount, 1);
});

test('frame timeout can be disabled without reconnect loop', async () => {
    const env = createEnv();
    const frames = [];
    const client = env.createClient({
        frameTimeoutMs: 0, reconnectDelayMs: 0,
        onVideoFrame: (message) => frames.push(message),
    });

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();

    const metadata = {type: 'frame-metadata', rtp_timestamp: 99};
    env.latestSocket().receive(metadata);
    env.video.emitFrame(99);
    assert.deepEqual(frames.at(-1), metadata);
    env.latestSocket().receive(metadata);
    env.clock.tick(2100);
    env.video.emitFrame(99);
    assert.equal(frames.at(-1), null);

    assert.equal(env.peerConnections.length, 1);
    assert.equal(client.getDebugState().restartTimerCount, 0);
});

test('video frame heartbeat does not spam the default logger', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 1000, reconnectDelayMs: 0});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();

    env.video.emitFrame();
    env.video.emitFrame();

    assert.equal(env.logs.some((log) => log.message === 'Video heartbeat: video frame'), false);
});

test('video track attachment starts media element playback', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 1000, reconnectDelayMs: 0});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();
    await env.flush();

    assert.equal(env.video.playCount, 1);
    assert.equal(env.video.paused, false);
});

test('video track unmute resumes media element playback', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 1000, reconnectDelayMs: 0});

    client.start();
    env.openLatestSocket();
    await env.flush();
    const track = env.attachVideoTrack();
    await env.flush();
    env.video.paused = true;
    track.onunmute();
    await env.flush();

    assert.equal(env.video.playCount, 2);
    assert.equal(env.video.paused, false);
});

test('video playback rejection does not restart the WebRTC session', async () => {
    const env = createEnv();
    env.video.playError = new Error('playback blocked');
    const client = env.createClient({frameTimeoutMs: 1000, reconnectDelayMs: 0});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();
    await env.flush();

    assert.equal(env.peerConnections.length, 1);
    assert.equal(client.getDebugState().restartTimerCount, 0);
    assert.equal(env.statuses.at(-1).state, 'disconnected');
    assert.equal(env.statusLines.at(-1), 'Playback failed. Check browser.');
    assert.equal(env.logs.some((log) =>
        log.message === 'Unable to start video playback: playback blocked'), true);
});

test('a synchronous playback error is display-only and a later frame clears it', async () => {
    const env = createEnv();
    env.video.play = () => {
        throw new Error('playback blocked');
    };
    const client = env.createClient();

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();

    assert.equal(env.statuses.at(-1).state, 'disconnected');
    assert.equal(env.statusLines.at(-1), 'Playback failed. Check browser.');
    assert.equal(client.getDebugState().restartTimerCount, 0);
    assert.equal(env.latestPeerConnection().closeCount, 0);
    env.latestPeerConnection().setIceState('connected');
    assert.equal(env.statusLines.at(-1), 'Playback failed. Check browser.');

    env.video.emitFrame();
    assert.equal(env.statuses.at(-1).state, 'connected');
    assert.equal(env.statusLines.at(-1), 'Connected');
});

test('status line updates are plain text for the compact UI', async () => {
    const env = createEnv();
    const statusLines = [];
    const client = env.createClient({
        frameTimeoutMs: 1000,
        reconnectDelayMs: 0,
        onStatusLine: (message) => statusLines.push(message),
    });

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.latestSocket().receive({type: 'answer', sdp: 'answer'});
    await env.flush();
    env.attachVideoTrack();
    env.latestPeerConnection().setIceState('failed');
    env.clock.tick(0);
    env.openLatestSocket();
    env.latestSocket().serverClose();

    assert.ok(statusLines.length > 0);
    assert.equal(statusLines.some((message) => /<\/?[a-z][^>]*>/i.test(message)), false);
});

test('currentTime fallback heartbeat keeps the session alive without requestVideoFrameCallback', async () => {
    const env = createEnv();
    env.video.requestVideoFrameCallback = undefined;
    env.video.cancelVideoFrameCallback = undefined;
    const client = env.createClient({frameTimeoutMs: 400, reconnectDelayMs: 0});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();

    env.clock.tick(100);
    assert.equal(env.statuses.some(({state}) => state === 'connected'), false);
    env.video.currentTime = 1;
    env.clock.tick(100);
    assert.equal(env.statuses.some(({state}) => state === 'connected'), false);
    env.video.videoWidth = 640;
    env.video.readyState = 1;
    env.video.currentTime = 2;
    env.clock.tick(100);
    assert.equal(env.statuses.some(({state}) => state === 'connected'), false);
    env.video.readyState = 2;
    env.video.currentTime = 3;
    env.clock.tick(100);
    assert.equal(env.statuses.at(-1).state, 'connected');
    assert.equal(env.statusLines.at(-1), 'Connected');
    env.clock.tick(300);

    assert.equal(env.peerConnections.length, 1);
    assert.equal(client.getDebugState().restartTimerCount, 0);
});

test('loadeddata reports video without adding polling when frame timeout is disabled', async () => {
    const env = createEnv();
    env.video.requestVideoFrameCallback = undefined;
    env.video.cancelVideoFrameCallback = undefined;
    const client = env.createClient({frameTimeoutMs: 0});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();
    assert.equal(env.statuses.some(({state}) => state === 'connected'), false);
    assert.equal(env.clock.timers.size, 0);

    env.video.dispatchEvent(new Event('loadeddata'));
    assert.equal(env.statuses.some(({state}) => state === 'connected'), false);
    env.video.videoWidth = 640;
    env.video.dispatchEvent(new Event('loadeddata'));
    assert.equal(env.statuses.at(-1).state, 'connected');
    assert.equal(env.statusLines.at(-1), 'Connected');
    assert.equal(env.clock.timers.size, 0);
    env.clock.tick(60000);
    assert.equal(env.peerConnections.length, 1);
    assert.equal(client.getDebugState().restartTimerCount, 0);
});

test('loadeddata listener is removed on restart and stale callbacks cannot update status', async () => {
    const env = createEnv();
    env.video.requestVideoFrameCallback = undefined;
    env.video.cancelVideoFrameCallback = undefined;
    const added = [];
    const removed = [];
    const addEventListener = env.video.addEventListener.bind(env.video);
    const removeEventListener = env.video.removeEventListener.bind(env.video);
    env.video.addEventListener = (type, listener, options) => {
        added.push({type, listener});
        addEventListener(type, listener, options);
    };
    env.video.removeEventListener = (type, listener, options) => {
        removed.push({type, listener});
        removeEventListener(type, listener, options);
    };
    const client = env.createClient({frameTimeoutMs: 0, reconnectDelayMs: 25});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();
    const loadeddata = added.find(({type}) => type === 'loadeddata');
    assert.ok(loadeddata);
    env.latestSocket().serverClose();
    assert.ok(removed.some(({type, listener}) =>
        type === 'loadeddata' && listener === loadeddata.listener));

    env.clock.tick(25);
    env.openLatestSocket();
    await env.flush();
    const statusCount = env.statuses.length;
    const lineCount = env.statusLines.length;
    env.video.videoWidth = 640;
    loadeddata.listener(new Event('loadeddata'));
    env.video.dispatchEvent(new Event('loadeddata'));
    assert.equal(env.statuses.length, statusCount);
    assert.equal(env.statusLines.length, lineCount);

    env.attachVideoTrack();
    env.video.dispatchEvent(new Event('loadeddata'));
    assert.equal(env.statuses.at(-1).state, 'connected');
    assert.equal(env.statusLines.at(-1), 'Connected');
});

test('video track ended schedules one full restart', async () => {
    const env = createEnv();
    const client = env.createClient({reconnectDelayMs: 10});

    client.start();
    env.openLatestSocket();
    await env.flush();
    const track = env.attachVideoTrack();

    track.onended();
    track.onended();

    assert.equal(env.statusLines.at(-1), 'Video ended. Retrying…');
    assert.equal(client.getDebugState().restartTimerCount, 1);
    env.clock.tick(10);
    await env.flush();
    assert.equal(env.peerConnections.length, 2);
});

test('stale answer and candidate events cannot modify the new generation', async () => {
    const env = createEnv();
    const frames = [];
    const client = env.createClient({
        reconnectDelayMs: 0, onVideoFrame: (message) => frames.push(message),
    });

    client.start();
    env.openLatestSocket();
    await env.flush();

    const oldSocket = env.latestSocket();
    const oldPc = env.latestPeerConnection();
    oldSocket.receive({type: 'frame-metadata', rtp_timestamp: 42});
    oldPc.setIceState('failed');
    env.clock.tick(0);
    env.openLatestSocket();
    await env.flush();

    const newPc = env.latestPeerConnection();
    const statusCount = env.statuses.length;
    const lineCount = env.statusLines.length;
    oldSocket.receive({type: 'answer', sdp: 'stale-answer'});
    oldSocket.receive({type: 'candidate', ice: {candidate: 'stale', sdpMLineIndex: 0}});
    oldSocket.receive({type: 'frame-metadata', rtp_timestamp: 43});
    oldPc.setIceState('connected');
    await env.flush();

    assert.equal(newPc.remoteDescriptions.length, 0);
    assert.equal(newPc.iceCandidates.length, 0);
    assert.equal(env.statuses.length, statusCount);
    assert.equal(env.statusLines.length, lineCount);
    env.attachVideoTrack();
    env.video.emitFrame(42);
    assert.equal(frames.at(-1), null);
    env.video.emitFrame(43);
    assert.equal(frames.at(-1), null);
});

test('rapid repeated failures do not create parallel peer connections', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 100, reconnectDelayMs: 50});

    client.start();
    env.openLatestSocket();
    await env.flush();

    const firstPc = env.latestPeerConnection();
    firstPc.setIceState('failed');
    firstPc.setIceState('failed');
    env.latestSocket().serverClose();

    assert.equal(client.getDebugState().restartTimerCount, 1);
    assert.equal(firstPc.closeCount, 1);
    assert.equal(env.peerConnections.length, 1);

    env.clock.tick(50);
    await env.flush();

    assert.equal(env.peerConnections.length, 2);
});

class FakeClock {
    constructor() {
        this.nowMs = 0;
        this.nextId = 1;
        this.timers = new Map();
    }

    now() {
        return this.nowMs;
    }

    setTimeout(callback, delayMs) {
        const id = this.nextId++;
        this.timers.set(id, {time: this.nowMs + delayMs, callback});
        return id;
    }

    clearTimeout(id) {
        this.timers.delete(id);
    }

    tick(ms) {
        const end = this.nowMs + ms;
        while (true) {
            let nextId = null;
            let nextTimer = null;
            for (const [id, timer] of this.timers.entries()) {
                if (timer.time <= end && (!nextTimer || timer.time < nextTimer.time)) {
                    nextId = id;
                    nextTimer = timer;
                }
            }

            if (!nextTimer)
                break;

            this.nowMs = nextTimer.time;
            this.timers.delete(nextId);
            nextTimer.callback();
        }
        this.nowMs = end;
    }
}

class FakeVideo extends EventTarget {
    constructor() {
        super();
        this.srcObject = null;
        this.currentTime = 0;
        this.videoWidth = 0;
        this.readyState = 0;
        this.paused = true;
        this.playCount = 0;
        this.playError = null;
        this.nextFrameId = 1;
        this.frameCallbacks = new Map();
    }

    play() {
        this.playCount += 1;
        if (this.playError)
            return Promise.reject(this.playError);

        this.paused = false;
        return Promise.resolve();
    }

    requestVideoFrameCallback(callback) {
        const id = this.nextFrameId++;
        this.frameCallbacks.set(id, callback);
        return id;
    }

    cancelVideoFrameCallback(id) {
        this.frameCallbacks.delete(id);
    }

    emitFrame(rtpTimestamp) {
        this.currentTime += 0.033;
        const callbacks = [...this.frameCallbacks.entries()];
        this.frameCallbacks.clear();
        for (const [, callback] of callbacks)
            callback(this.currentTime, {mediaTime: this.currentTime, rtpTimestamp});
    }
}

class FakeMediaStream {
    constructor() {
        this.tracks = [];
    }

    addTrack(track) {
        this.tracks.push(track);
    }

    getTracks() {
        return this.tracks;
    }
}

class FakeTrack {
    constructor(kind) {
        this.kind = kind;
        this.stopCount = 0;
        this.onmute = null;
        this.onunmute = null;
        this.onended = null;
    }

    stop() {
        this.stopCount += 1;
    }
}

class FakePeerConnection {
    constructor(options) {
        this.options = options;
        this.localDescription = null;
        this.remoteDescriptions = [];
        this.iceCandidates = [];
        this.closeCount = 0;
        this.iceConnectionState = 'new';
        this.iceGatheringState = 'new';
        this.signalingState = 'stable';
        this.transceivers = [];
    }

    addTransceiver(kind, options) {
        this.transceivers.push({kind, options});
    }

    async createOffer() {
        return {type: 'offer', sdp: `offer-${this.transceivers.length}`};
    }

    async setLocalDescription(description) {
        this.localDescription = description;
    }

    async setRemoteDescription(description) {
        this.remoteDescriptions.push(description);
    }

    async addIceCandidate(candidate) {
        this.iceCandidates.push(candidate);
    }

    close() {
        this.closeCount += 1;
    }

    setIceState(state) {
        this.iceConnectionState = state;
        if (this.oniceconnectionstatechange)
            this.oniceconnectionstatechange();
    }

    emitTrack(track) {
        if (this.ontrack)
            this.ontrack({track});
    }
}

class FakeWebSocket {
    constructor(url) {
        this.url = url;
        this.readyState = 0;
        this.sentMessages = [];
        this.closeCount = 0;
        this.onopen = null;
        this.onmessage = null;
        this.onerror = null;
        this.onclose = null;
    }

    open() {
        this.readyState = 1;
        if (this.onopen)
            this.onopen();
    }

    send(payload) {
        this.sentMessages.push(JSON.parse(payload));
    }

    close() {
        this.closeCount += 1;
        this.readyState = 3;
        if (this.onclose)
            this.onclose();
    }

    serverClose() {
        this.readyState = 3;
        if (this.onclose)
            this.onclose();
    }

    receive(message) {
        if (this.onmessage)
            this.onmessage({data: JSON.stringify(message)});
    }
}

function createEnv() {
    const clock = new FakeClock();
    const video = new FakeVideo();
    const peerConnections = [];
    const webSockets = [];
    const logs = [];
    const statuses = [];
    const statusLines = [];

    return {
        clock,
        video,
        peerConnections,
        webSockets,
        logs,
        statuses,
        statusLines,
        createClient(overrides = {}) {
            return createWebRtcClient({
                video,
                signalingUrl: 'ws://example.test/ws',
                frameTimeoutMs: 1000,
                reconnectDelayMs: 0,
                maxReconnectDelayMs: 5000,
                setTimeout: clock.setTimeout.bind(clock),
                clearTimeout: clock.clearTimeout.bind(clock),
                now: clock.now.bind(clock),
                logger: (message, type) => logs.push({message, type}),
                onStatus: (state, label, subtext) => statuses.push({state, label, subtext}),
                onStatusLine: (message) => statusLines.push(message),
                onRemoteTrack: () => {},
                WebSocketFactory: (url) => {
                    const socket = new FakeWebSocket(url);
                    webSockets.push(socket);
                    return socket;
                },
                RTCPeerConnectionFactory: (options) => {
                    const pc = new FakePeerConnection(options);
                    peerConnections.push(pc);
                    return pc;
                },
                MediaStreamFactory: () => new FakeMediaStream(),
                RTCSessionDescriptionFactory: (description) => description,
                RTCIceCandidateFactory: (candidate) => candidate,
                webSocketOpenState: 1,
                webSocketConnectingState: 0,
                ...overrides,
            });
        },
        latestPeerConnection() {
            return peerConnections[peerConnections.length - 1];
        },
        latestSocket() {
            return webSockets[webSockets.length - 1];
        },
        openLatestSocket() {
            this.latestSocket().open();
        },
        attachVideoTrack() {
            const track = new FakeTrack('video');
            this.latestPeerConnection().emitTrack(track);
            return track;
        },
        async flush() {
            await Promise.resolve();
            await Promise.resolve();
        },
    };
}

import assert from 'node:assert/strict';
import test from 'node:test';

import {createWebRtcClient} from './webrtc_client.js';

test('frame heartbeat keeps the session alive', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 1000, reconnectDelayMs: 0});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();

    env.clock.tick(900);
    env.video.emitFrame();
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
    const client = env.createClient({reconnectDelayMs: 10});

    client.start();
    env.openLatestSocket();
    await env.flush();

    const firstSocket = env.latestSocket();
    const firstPc = env.latestPeerConnection();
    firstSocket.serverClose();

    assert.equal(firstPc.closeCount, 1);

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

test('frame timeout can be disabled without reconnect loop', async () => {
    const env = createEnv();
    const client = env.createClient({frameTimeoutMs: 0, reconnectDelayMs: 0});

    client.start();
    env.openLatestSocket();
    await env.flush();
    env.attachVideoTrack();

    env.clock.tick(1000);

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
    assert.equal(env.logs.some((log) =>
        log.message === 'Unable to start video playback: playback blocked'), true);
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
    env.video.currentTime = 1;
    env.clock.tick(100);
    env.clock.tick(300);

    assert.equal(env.peerConnections.length, 1);
    assert.equal(client.getDebugState().restartTimerCount, 0);
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

    assert.equal(client.getDebugState().restartTimerCount, 1);
    env.clock.tick(10);
    await env.flush();
    assert.equal(env.peerConnections.length, 2);
});

test('stale answer and candidate events cannot modify the new generation', async () => {
    const env = createEnv();
    const client = env.createClient({reconnectDelayMs: 0});

    client.start();
    env.openLatestSocket();
    await env.flush();

    const oldSocket = env.latestSocket();
    env.latestPeerConnection().setIceState('failed');
    env.clock.tick(0);
    env.openLatestSocket();
    await env.flush();

    const newPc = env.latestPeerConnection();
    oldSocket.receive({type: 'answer', sdp: 'stale-answer'});
    oldSocket.receive({type: 'candidate', ice: {candidate: 'stale', sdpMLineIndex: 0}});
    await env.flush();

    assert.equal(newPc.remoteDescriptions.length, 0);
    assert.equal(newPc.iceCandidates.length, 0);
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

class FakeVideo {
    constructor() {
        this.srcObject = null;
        this.currentTime = 0;
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

    emitFrame() {
        this.currentTime += 0.033;
        const callbacks = [...this.frameCallbacks.entries()];
        this.frameCallbacks.clear();
        for (const [, callback] of callbacks)
            callback(this.currentTime, {mediaTime: this.currentTime});
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
    constructor() {
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

    return {
        clock,
        video,
        peerConnections,
        webSockets,
        logs,
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
                onStatus: () => {},
                onStatusLine: () => {},
                onRemoteTrack: () => {},
                WebSocketFactory: (url) => {
                    const socket = new FakeWebSocket(url);
                    webSockets.push(socket);
                    return socket;
                },
                RTCPeerConnectionFactory: () => {
                    const pc = new FakePeerConnection();
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

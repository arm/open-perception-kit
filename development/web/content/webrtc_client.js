export function createWebRtcClient(config) {
    return new WebRtcClient(config);
}

class WebRtcClient {
    constructor(config) {
        this.video = requireConfig(config, 'video');
        this.signalingUrl = requireConfig(config, 'signalingUrl');
        this.WebSocketFactory = requireConfig(config, 'WebSocketFactory');
        this.RTCPeerConnectionFactory = requireConfig(config, 'RTCPeerConnectionFactory');
        this.MediaStreamFactory = requireConfig(config, 'MediaStreamFactory');

        this.RTCSessionDescriptionFactory =
            config.RTCSessionDescriptionFactory || ((description) => description);
        this.RTCIceCandidateFactory = config.RTCIceCandidateFactory || ((candidate) => candidate);

        this.frameTimeoutMs = config.frameTimeoutMs ?? 5000;
        this.reconnectDelayMs = config.reconnectDelayMs ?? 1000;
        this.maxReconnectDelayMs = config.maxReconnectDelayMs ?? 15000;
        this.backoffFactor = config.backoffFactor ?? 1.1;
        this.logFrameHeartbeats = config.logFrameHeartbeats ?? false;
        this.iceServers = config.iceServers || [{urls: 'stun:stun.l.google.com:19302'}];

        this.setTimeout = config.setTimeout || globalThis.setTimeout.bind(globalThis);
        this.clearTimeout = config.clearTimeout || globalThis.clearTimeout.bind(globalThis);
        this.now = config.now || (() => Date.now());

        this.openState = config.webSocketOpenState ?? 1;
        this.connectingState = config.webSocketConnectingState ?? 0;

        this.logger = config.logger || (() => {});
        this.onStatus = config.onStatus || (() => {});
        this.onStatusLine = config.onStatusLine || (() => {});
        this.onRemoteTrack = config.onRemoteTrack || (() => {});

        this.generation = 0;
        this.session = null;
        this.restartTimer = null;
        this.started = false;
        this.currentReconnectDelayMs = this.reconnectDelayMs;
    }

    start() {
        if (this.started)
            return;

        this.started = true;
        this.onStatus('connecting', 'Connecting', 'Initializing...');
        this.onStatusLine('Starting WebRTC client & signaling...');
        this.startNewSession('initial start');
    }

    reconnectNow(reason = 'manual reconnect') {
        this.currentReconnectDelayMs = this.reconnectDelayMs;
        this.cancelRestartTimer();
        this.restartNow(reason);
    }

    stop() {
        this.started = false;
        this.cancelRestartTimer();
        this.closeCurrentSession('stop');
    }

    getDebugState() {
        return {
            generation: this.generation,
            hasSession: Boolean(this.session),
            restartTimerCount: this.restartTimer ? 1 : 0,
        };
    }

    startNewSession(reason) {
        if (!this.started)
            return;

        this.closeCurrentSession(reason);

        const generation = ++this.generation;
        const remoteStream = this.MediaStreamFactory();
        const session = {
            generation,
            reason,
            pc: null,
            ws: null,
            remoteStream,
            closed: false,
            intentionalClose: false,
            receivingVideo: false,
            lastFrameAt: this.now(),
            lastVideoTime: this.video.currentTime || 0,
            frameCallbackHandle: null,
            fallbackTimer: null,
            frameWatchdogTimer: null,
        };

        this.session = session;
        this.resetVideoElement(remoteStream);

        this.log(`Starting WebRTC session generation ${generation}: ${reason}`);
        this.onStatus('connecting', 'Connecting', 'Connecting to signaling server...');
        this.onStatusLine('Connecting to signaling server...');

        session.pc = this.createPeerConnection(session);
        session.ws = this.createSignalingSocket(session);
    }

    createPeerConnection(session) {
        const pc = this.RTCPeerConnectionFactory({iceServers: this.iceServers});

        pc.addTransceiver('video', {direction: 'recvonly'});
        pc.addTransceiver('audio', {direction: 'recvonly'});

        pc.onicecandidate = (event) => {
            if (!this.isCurrent(session) || !event.candidate)
                return;

            if (session.ws && session.ws.readyState === this.openState) {
                try {
                    this.log('Sending ICE candidate');
                    session.ws.send(JSON.stringify({type: 'candidate', ice: event.candidate}));
                } catch (err) {
                    this.log(`Error sending ICE candidate: ${formatError(err)}`, 'error');
                    this.scheduleRestart(session, 'candidate send failed');
                }
            }
        };

        pc.ontrack = (event) => {
            if (!this.isCurrent(session))
                return;

            this.log(`Received track kind=${event.track.kind}`);
            session.remoteStream.addTrack(event.track);
            this.attachRemoteStream(session.remoteStream);
            this.onRemoteTrack(event.track);

            if (event.track.kind === 'video') {
                session.receivingVideo = true;
                this.observeVideoTrack(session, event.track);
                this.markFrameHeartbeat(session, 'video track attached');
                this.onStatus('connected', 'Connected', 'Receiving video stream');
                this.onStatusLine('WebRTC connected. Video stream should be visible.');
            }
        };

        pc.oniceconnectionstatechange = () => {
            if (!this.isCurrent(session))
                return;

            const state = pc.iceConnectionState;
            this.log(`ICE connection state: ${state}`);
            if (state === 'connected' || state === 'completed') {
                this.onStatus('connected', 'Connected', 'Peer connection is stable.');
            } else if (state === 'failed' || state === 'disconnected') {
                this.onStatus('disconnected', 'Disconnected', 'Trying to recover connection...');
                this.onStatusLine(`ICE state: ${state} - will try to restart WebRTC.`);
                this.scheduleRestart(session, `ICE ${state}`);
            }
        };

        pc.onicegatheringstatechange = () => {
            if (this.isCurrent(session))
                this.log(`ICE gathering state: ${pc.iceGatheringState}`);
        };

        pc.onsignalingstatechange = () => {
            if (this.isCurrent(session))
                this.log(`Signaling state: ${pc.signalingState}`);
        };

        return pc;
    }

    createSignalingSocket(session) {
        this.log(`Connecting to signaling: ${this.signalingUrl}`);
        const ws = this.WebSocketFactory(this.signalingUrl);

        ws.onopen = () => {
            if (!this.isCurrent(session))
                return;

            this.log('Signaling WebSocket open');
            this.currentReconnectDelayMs = this.reconnectDelayMs;
            this.onStatus('connecting', 'Connecting', 'Signaling connected - creating offer...');
            this.startOffer(session);
        };

        ws.onmessage = async (event) => {
            if (!this.isCurrent(session))
                return;

            let data;
            try {
                data = JSON.parse(event.data);
            } catch (err) {
                this.log(`Invalid signaling message: ${formatError(err)}`, 'error');
                this.scheduleRestart(session, 'invalid signaling message');
                return;
            }

            this.log(`Received signaling message: ${data.type}`);

            try {
                if (data.type === 'answer') {
                    await session.pc.setRemoteDescription(
                        this.RTCSessionDescriptionFactory({type: 'answer', sdp: data.sdp}));
                    if (!this.isCurrent(session))
                        return;

                    this.onStatus('connected', 'Connected', 'Answer received from server.');
                    if (!session.receivingVideo)
                        this.onStatusLine('Answer received. Waiting for video track...');
                } else if (data.type === 'candidate' && data.ice) {
                    if (!data.ice.candidate) {
                        this.log('End of candidates');
                        return;
                    }
                    await session.pc.addIceCandidate(this.RTCIceCandidateFactory(data.ice));
                }
            } catch (err) {
                this.log(`Error handling signaling message: ${formatError(err)}`, 'error');
                this.scheduleRestart(session, 'signaling message failed');
            }
        };

        ws.onerror = (err) => {
            if (this.isCurrent(session))
                this.log(`Signaling WebSocket error: ${formatError(err)}`, 'error');
        };

        ws.onclose = () => {
            if (!this.isCurrent(session) || session.intentionalClose)
                return;

            this.log('Signaling WebSocket closed. Scheduling reconnect.');
            this.onStatus('disconnected', 'Disconnected', 'Signaling closed - will retry...');
            this.onStatusLine('Signaling connection closed. Will retry automatically.');

            const delay = this.nextReconnectDelay();
            this.scheduleRestart(session, 'signaling closed', delay);
        };

        return ws;
    }

    async startOffer(session) {
        if (!this.isCurrent(session) || !session.ws || session.ws.readyState !== this.openState)
            return;

        try {
            this.onStatus('connecting', 'Connecting', 'Creating offer and sending to server...');
            this.onStatusLine('Creating offer and sending it to the signaling server...');

            const offer = await session.pc.createOffer();
            if (!this.isCurrent(session))
                return;

            this.log('Created offer');
            await session.pc.setLocalDescription(offer);
            if (!this.isCurrent(session))
                return;

            this.log('Set local description with offer');
            session.ws.send(JSON.stringify({type: 'offer', sdp: session.pc.localDescription.sdp}));
            this.armFrameWatchdog(session);
        } catch (err) {
            if (!this.isCurrent(session))
                return;
            this.log(`Error during WebRTC offer: ${formatError(err)}`, 'error');
            this.scheduleRestart(session, 'offer failed');
        }
    }

    observeVideoTrack(session, track) {
        track.onmute = () => {
            if (this.isCurrent(session))
                this.log('Video track muted');
        };
        track.onunmute = () => {
            if (!this.isCurrent(session))
                return;
            this.log('Video track unmuted');
            this.markFrameHeartbeat(session, 'video track unmuted');
        };
        track.onended = () => {
            if (!this.isCurrent(session))
                return;
            this.log('Video track ended');
            this.scheduleRestart(session, 'video track ended');
        };

        if (this.frameTimeoutMs <= 0) {
            return;
        }

        if (typeof this.video.requestVideoFrameCallback === 'function') {
            const watchFrames = () => {
                if (!this.isCurrent(session))
                    return;
                session.frameCallbackHandle = this.video.requestVideoFrameCallback(() => {
                    if (!this.isCurrent(session))
                        return;
                    this.markFrameHeartbeat(session, 'video frame');
                    watchFrames();
                });
            };
            watchFrames();
        } else {
            this.observeCurrentTimeFallback(session);
        }
    }

    observeCurrentTimeFallback(session) {
        const intervalMs = Math.min(500, Math.max(100, Math.floor(this.frameTimeoutMs / 4)));
        const tick = () => {
            if (!this.isCurrent(session))
                return;

            const currentTime = this.video.currentTime || 0;
            if (currentTime !== session.lastVideoTime) {
                session.lastVideoTime = currentTime;
                this.markFrameHeartbeat(session, 'video currentTime');
            }

            session.fallbackTimer = this.setTimeout(tick, intervalMs);
        };

        session.fallbackTimer = this.setTimeout(tick, intervalMs);
    }

    markFrameHeartbeat(session, source) {
        if (!this.isCurrent(session))
            return;

        session.lastFrameAt = this.now();
        if (this.logFrameHeartbeats || !isHighFrequencyHeartbeat(source))
            this.log(`Video heartbeat: ${source}`);
        this.armFrameWatchdog(session);
    }

    armFrameWatchdog(session) {
        if (!this.isCurrent(session))
            return;

        if (session.frameWatchdogTimer)
            this.clearTimeout(session.frameWatchdogTimer);
        session.frameWatchdogTimer = null;

        if (this.frameTimeoutMs <= 0)
            return;

        session.frameWatchdogTimer = this.setTimeout(() => {
            if (!this.isCurrent(session))
                return;

            const ageMs = this.now() - session.lastFrameAt;
            if (ageMs >= this.frameTimeoutMs) {
                this.log(`No video frame for ${ageMs}ms. Restarting WebRTC.`, 'error');
                this.scheduleRestart(session, 'video frame timeout');
                return;
            }

            this.armFrameWatchdog(session);
        }, this.frameTimeoutMs);
    }

    scheduleRestart(session, reason, delayMs = this.reconnectDelayMs) {
        if (!this.isCurrent(session) || this.restartTimer)
            return;

        this.log(`Scheduling WebRTC restart in ${delayMs}ms: ${reason}`);
        this.onStatus('reconnecting', 'Reconnecting', 'Re-establishing WebRTC connection...');
        this.closeSession(session, reason);

        this.restartTimer = this.setTimeout(() => {
            this.restartTimer = null;
            this.restartNow(reason);
        }, delayMs);
    }

    restartNow(reason) {
        if (!this.started)
            return;

        this.log(`Restarting WebRTC: ${reason}`);
        this.startNewSession(reason);
    }

    closeCurrentSession(reason) {
        if (this.session)
            this.closeSession(this.session, reason);
    }

    closeSession(session, reason) {
        if (!session || session.closed)
            return;

        session.closed = true;
        session.intentionalClose = true;

        if (session.frameWatchdogTimer)
            this.clearTimeout(session.frameWatchdogTimer);
        if (session.fallbackTimer)
            this.clearTimeout(session.fallbackTimer);
        if (session.frameCallbackHandle &&
            typeof this.video.cancelVideoFrameCallback === 'function') {
            this.video.cancelVideoFrameCallback(session.frameCallbackHandle);
        }

        stopTracks(this.video.srcObject);
        this.video.srcObject = null;

        try {
            if (session.pc)
                session.pc.close();
        } catch (err) {
            this.log(`Error closing RTCPeerConnection: ${formatError(err)}`, 'error');
        }

        try {
            if (session.ws &&
                (session.ws.readyState === this.openState ||
                 session.ws.readyState === this.connectingState)) {
                session.ws.close();
            }
        } catch (err) {
            this.log(`Error closing signaling WebSocket: ${formatError(err)}`, 'error');
        }

        if (this.session === session)
            this.session = null;

        this.log(`Closed WebRTC session generation ${session.generation}: ${reason}`);
    }

    resetVideoElement(remoteStream) {
        stopTracks(this.video.srcObject);
        this.video.srcObject = remoteStream;
        this.video.autoplay = true;
        this.video.playsInline = true;
    }

    attachRemoteStream(remoteStream) {
        if (this.video.srcObject !== remoteStream)
            this.video.srcObject = remoteStream;
    }

    cancelRestartTimer() {
        if (!this.restartTimer)
            return;

        this.clearTimeout(this.restartTimer);
        this.restartTimer = null;
    }

    nextReconnectDelay() {
        const delay = this.currentReconnectDelayMs;
        this.currentReconnectDelayMs = Math.min(
            this.currentReconnectDelayMs * this.backoffFactor, this.maxReconnectDelayMs);
        return delay;
    }

    isCurrent(session) {
        return Boolean(session) && this.session === session && !session.closed &&
               session.generation === this.generation;
    }

    log(message, type = 'info') {
        this.logger(message, type);
    }
}

function requireConfig(config, key) {
    if (!config || !config[key])
        throw new Error(`Missing WebRTC client config: ${key}`);
    return config[key];
}

function stopTracks(stream) {
    if (!stream || typeof stream.getTracks !== 'function')
        return;

    for (const track of stream.getTracks()) {
        if (track && typeof track.stop === 'function')
            track.stop();
    }
}

function isHighFrequencyHeartbeat(source) {
    return source === 'video frame' || source === 'video currentTime';
}

function formatError(err) {
    if (!err)
        return 'unknown error';
    return err.message || String(err);
}

// Copyright (C) 2025 Arm Limited. All rights reserved.

// development/web/src/webrtc_client.js
function createWebRtcClient(config) {
  return new WebRtcClient(config);
}
async function dumpSelectedCandidatePair(pc) {
  if (!pc || typeof pc.getStats !== "function")
    return;
  const stats = await pc.getStats();
  let selectedPair = null;
  const localCandidates = /* @__PURE__ */ new Map();
  const remoteCandidates = /* @__PURE__ */ new Map();
  stats.forEach((report) => {
    if (report.type === "local-candidate") {
      localCandidates.set(report.id, report);
    }
    if (report.type === "remote-candidate") {
      remoteCandidates.set(report.id, report);
    }
    if (report.type === "candidate-pair" && report.selected) {
      selectedPair = report;
    }
  });
  if (!selectedPair) {
    console.log("No selected ICE candidate pair yet");
    return;
  }
  const local = localCandidates.get(selectedPair.localCandidateId);
  const remote = remoteCandidates.get(selectedPair.remoteCandidateId);
  console.log("SELECTED ICE PAIR:", selectedPair);
  console.log("LOCAL CANDIDATE:", local);
  console.log("REMOTE CANDIDATE:", remote);
}
async function dumpAllCandidatePairs(pc) {
  if (!pc || typeof pc.getStats !== "function")
    return;
  const stats = await pc.getStats();
  stats.forEach((report) => {
    if (report.type === "candidate-pair") {
      console.log("CANDIDATE PAIR:", report);
    }
  });
}
var WebRtcClient = class {
  constructor(config) {
    this.video = requireConfig(config, "video");
    this.signalingUrl = requireConfig(config, "signalingUrl");
    this.WebSocketFactory = requireConfig(config, "WebSocketFactory");
    this.RTCPeerConnectionFactory = requireConfig(config, "RTCPeerConnectionFactory");
    this.MediaStreamFactory = requireConfig(config, "MediaStreamFactory");
    this.RTCSessionDescriptionFactory = config.RTCSessionDescriptionFactory || ((description) => description);
    this.RTCIceCandidateFactory = config.RTCIceCandidateFactory || ((candidate) => candidate);
    this.frameTimeoutMs = config.frameTimeoutMs ?? 3e4;
    this.reconnectDelayMs = config.reconnectDelayMs ?? 1e3;
    this.maxReconnectDelayMs = config.maxReconnectDelayMs ?? 15e3;
    this.backoffFactor = config.backoffFactor ?? 1.1;
    this.logFrameHeartbeats = config.logFrameHeartbeats ?? false;
    this.iceServers = config.iceServers || [{ urls: "stun:stun.l.google.com:19302" }];
    this.setTimeout = config.setTimeout || globalThis.setTimeout.bind(globalThis);
    this.clearTimeout = config.clearTimeout || globalThis.clearTimeout.bind(globalThis);
    this.now = config.now || (() => Date.now());
    this.openState = config.webSocketOpenState ?? 1;
    this.connectingState = config.webSocketConnectingState ?? 0;
    this.logger = config.logger || (() => {
    });
    this.onStatus = config.onStatus || (() => {
    });
    this.onStatusLine = config.onStatusLine || (() => {
    });
    this.onRemoteTrack = config.onRemoteTrack || (() => {
    });
    this.generation = 0;
    this.session = null;
    this.restartTimer = null;
    this.started = false;
    this.paused = false;
    this.currentReconnectDelayMs = this.reconnectDelayMs;
  }
  start() {
    if (this.started)
      return;
    this.started = true;
    this.onStatus("connecting", "Connecting", "Initializing...");
    this.onStatusLine("Starting WebRTC client & signaling...");
    this.startNewSession("initial start");
  }
  reconnectNow(reason = "manual reconnect") {
    this.currentReconnectDelayMs = this.reconnectDelayMs;
    this.cancelRestartTimer();
    this.restartNow(reason);
  }
  stop() {
    this.started = false;
    this.cancelRestartTimer();
    this.closeCurrentSession("stop");
  }
  setPaused(paused) {
    const wasPaused = this.paused;
    this.paused = Boolean(paused);
    const session = this.session;
    if (!this.isCurrent(session))
      return;
    if (this.paused) {
      if (session.frameWatchdogTimer)
        this.clearTimeout(session.frameWatchdogTimer);
      session.frameWatchdogTimer = null;
      return;
    }
    if (wasPaused && session.receivingVideo)
      this.markFrameHeartbeat(session, "feed resumed");
  }
  getDebugState() {
    return {
      generation: this.generation,
      hasSession: Boolean(this.session),
      restartTimerCount: this.restartTimer ? 1 : 0
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
      frameWatchdogTimer: null
    };
    this.session = session;
    this.resetVideoElement(remoteStream);
    this.log(`Starting WebRTC session generation ${generation}: ${reason}`);
    this.onStatus("connecting", "Connecting", "Connecting to signaling server...");
    this.onStatusLine("Connecting to signaling server...");
    session.pc = this.createPeerConnection(session);
    session.ws = this.createSignalingSocket(session);
  }
  createPeerConnection(session) {
    const pc = this.RTCPeerConnectionFactory({
      iceServers: this.iceServers
    });
    pc.addTransceiver("video", { direction: "recvonly" });
    pc.addTransceiver("audio", { direction: "recvonly" });
    pc.onicecandidate = (event) => {
      if (!this.isCurrent(session) || !event.candidate)
        return;
      console.log("Candidate: " + event.candidate.candidate);
      if (session.ws && session.ws.readyState === this.openState) {
        try {
          this.log("Sending ICE candidate");
          session.ws.send(JSON.stringify({ type: "candidate", ice: event.candidate }));
        } catch (err) {
          this.log(`Error sending ICE candidate: ${formatError(err)}`, "error");
          this.scheduleRestart(session, "candidate send failed");
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
      if (event.track.kind === "video") {
        session.receivingVideo = true;
        this.observeVideoTrack(session, event.track);
        this.startVideoPlayback(session);
        this.markFrameHeartbeat(session, "video track attached");
        this.onStatus("connected", "Connected", "Receiving video stream");
        this.onStatusLine("WebRTC connected. Video stream should be visible.");
      }
    };
    pc.oniceconnectionstatechange = () => {
      if (!this.isCurrent(session))
        return;
      this.setTimeout(() => {
        if (this.isCurrent(session)) {
          dumpSelectedCandidatePair(pc).catch(() => {
          });
        }
      }, 1e3);
      this.setTimeout(() => {
        if (this.isCurrent(session)) {
          dumpAllCandidatePairs(pc).catch(() => {
          });
        }
      }, 3e3);
      const state = pc.iceConnectionState;
      this.log(`ICE connection state: ${state}`);
      if (state === "connected" || state === "completed") {
        this.onStatus("connected", "Connected", "Peer connection is stable.");
      } else if (state === "failed" || state === "disconnected") {
        this.onStatus("disconnected", "Disconnected", "Trying to recover connection...");
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
      this.log("Signaling WebSocket open");
      this.currentReconnectDelayMs = this.reconnectDelayMs;
      this.onStatus("connecting", "Connecting", "Signaling connected - creating offer...");
      this.startOffer(session);
    };
    ws.onmessage = async (event) => {
      if (!this.isCurrent(session))
        return;
      let data;
      try {
        data = JSON.parse(event.data);
      } catch (err) {
        this.log(`Invalid signaling message: ${formatError(err)}`, "error");
        this.scheduleRestart(session, "invalid signaling message");
        return;
      }
      this.log(`Received signaling message: ${data.type}`);
      try {
        if (data.type === "answer") {
          await session.pc.setRemoteDescription(
            this.RTCSessionDescriptionFactory({ type: "answer", sdp: data.sdp })
          );
          if (!this.isCurrent(session))
            return;
          this.onStatus("connected", "Connected", "Answer received from server.");
          if (!session.receivingVideo)
            this.onStatusLine("Answer received. Waiting for video track...");
        } else if (data.type === "candidate" && data.ice) {
          if (!data.ice.candidate) {
            this.log("End of candidates");
            return;
          }
          await session.pc.addIceCandidate(this.RTCIceCandidateFactory(data.ice));
        }
      } catch (err) {
        this.log(`Error handling signaling message: ${formatError(err)}`, "error");
        this.scheduleRestart(session, "signaling message failed");
      }
    };
    ws.onerror = (err) => {
      if (this.isCurrent(session))
        this.log(`Signaling WebSocket error: ${formatError(err)}`, "error");
    };
    ws.onclose = () => {
      if (!this.isCurrent(session) || session.intentionalClose)
        return;
      this.log("Signaling WebSocket closed. Scheduling reconnect.");
      this.onStatus("disconnected", "Disconnected", "Signaling closed - will retry...");
      this.onStatusLine("Signaling connection closed. Will retry automatically.");
      const delay = this.nextReconnectDelay();
      this.scheduleRestart(session, "signaling closed", delay);
    };
    return ws;
  }
  async startOffer(session) {
    if (!this.isCurrent(session) || !session.ws || session.ws.readyState !== this.openState)
      return;
    try {
      this.onStatus("connecting", "Connecting", "Creating offer and sending to server...");
      this.onStatusLine("Creating offer and sending it to the signaling server...");
      const offer = await session.pc.createOffer();
      if (!this.isCurrent(session))
        return;
      this.log("Created offer");
      await session.pc.setLocalDescription(offer);
      if (!this.isCurrent(session))
        return;
      this.log("Set local description with offer");
      session.ws.send(JSON.stringify({ type: "offer", sdp: session.pc.localDescription.sdp }));
    } catch (err) {
      if (!this.isCurrent(session))
        return;
      this.log(`Error during WebRTC offer: ${formatError(err)}`, "error");
      this.scheduleRestart(session, "offer failed");
    }
  }
  observeVideoTrack(session, track) {
    track.onmute = () => {
      if (this.isCurrent(session))
        this.log("Video track muted");
    };
    track.onunmute = () => {
      if (!this.isCurrent(session))
        return;
      this.log("Video track unmuted");
      this.startVideoPlayback(session);
      this.markFrameHeartbeat(session, "video track unmuted");
    };
    track.onended = () => {
      if (!this.isCurrent(session))
        return;
      this.log("Video track ended");
      this.scheduleRestart(session, "video track ended");
    };
    if (this.frameTimeoutMs <= 0) {
      return;
    }
    if (typeof this.video.requestVideoFrameCallback === "function") {
      const watchFrames = () => {
        if (!this.isCurrent(session))
          return;
        session.frameCallbackHandle = this.video.requestVideoFrameCallback(() => {
          if (!this.isCurrent(session))
            return;
          this.markFrameHeartbeat(session, "video frame");
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
        this.markFrameHeartbeat(session, "video currentTime");
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
    if (this.frameTimeoutMs <= 0 || this.paused)
      return;
    session.frameWatchdogTimer = this.setTimeout(() => {
      if (!this.isCurrent(session))
        return;
      const ageMs = this.now() - session.lastFrameAt;
      if (ageMs >= this.frameTimeoutMs) {
        this.log(`No video frame for ${ageMs}ms. Restarting WebRTC.`, "error");
        this.scheduleRestart(session, "video frame timeout");
        return;
      }
      this.armFrameWatchdog(session);
    }, this.frameTimeoutMs);
  }
  scheduleRestart(session, reason, delayMs = this.reconnectDelayMs) {
    if (!this.isCurrent(session) || this.restartTimer)
      return;
    this.log(`Scheduling WebRTC restart in ${delayMs}ms: ${reason}`);
    this.onStatus("reconnecting", "Reconnecting", "Re-establishing WebRTC connection...");
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
    if (session.frameCallbackHandle && typeof this.video.cancelVideoFrameCallback === "function") {
      this.video.cancelVideoFrameCallback(session.frameCallbackHandle);
    }
    stopTracks(this.video.srcObject);
    this.video.srcObject = null;
    try {
      if (session.pc)
        session.pc.close();
    } catch (err) {
      this.log(`Error closing RTCPeerConnection: ${formatError(err)}`, "error");
    }
    try {
      if (session.ws && (session.ws.readyState === this.openState || session.ws.readyState === this.connectingState)) {
        session.ws.close();
      }
    } catch (err) {
      this.log(`Error closing signaling WebSocket: ${formatError(err)}`, "error");
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
  startVideoPlayback(session) {
    if (!this.isCurrent(session) || typeof this.video.play !== "function")
      return;
    let playback;
    try {
      playback = this.video.play();
    } catch (err) {
      this.log(`Unable to start video playback: ${formatError(err)}`, "error");
      return;
    }
    if (playback && typeof playback.catch === "function") {
      playback.catch((err) => {
        if (this.isCurrent(session))
          this.log(`Unable to start video playback: ${formatError(err)}`, "error");
      });
    }
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
      this.currentReconnectDelayMs * this.backoffFactor,
      this.maxReconnectDelayMs
    );
    return delay;
  }
  isCurrent(session) {
    return Boolean(session) && this.session === session && !session.closed && session.generation === this.generation;
  }
  log(message, type = "info") {
    this.logger(message, type);
  }
};
function requireConfig(config, key) {
  if (!config || !config[key])
    throw new Error(`Missing WebRTC client config: ${key}`);
  return config[key];
}
function stopTracks(stream) {
  if (!stream || typeof stream.getTracks !== "function")
    return;
  for (const track of stream.getTracks()) {
    if (track && typeof track.stop === "function")
      track.stop();
  }
}
function isHighFrequencyHeartbeat(source) {
  return source === "video frame" || source === "video currentTime";
}
function formatError(err) {
  if (!err)
    return "unknown error";
  return err.message || String(err);
}

// development/web/src/webrtc_config.js
function resolveWebRtcTimingConfig(pekConfig = {}) {
  const webrtcConfig = pekConfig.webrtc || {};
  return {
    frameTimeoutMs: webrtcConfig.frameTimeoutMs ?? pekConfig.webrtcFrameTimeoutMs ?? 3e4,
    reconnectDelayMs: webrtcConfig.reconnectDelayMs ?? 1e3,
    maxReconnectDelayMs: webrtcConfig.maxReconnectDelayMs ?? 15e3,
    backoffFactor: webrtcConfig.backoffFactor ?? 1.1
  };
}
function resolveWebRtcIceConfig(pekConfig = {}) {
  const webrtcConfig = pekConfig.webrtc || {};
  return {
    iceServers: webrtcConfig.iceServers ?? [{ urls: "stun:stun.l.google.com:19302" }]
  };
}

// development/web/src/webrtc.js
var video = document.getElementById("video");
var statusPill = document.getElementById("status-pill");
var statusLabelEl = document.getElementById("status-label");
var statusSubtextEl = document.getElementById("status-subtext");
var statusLineEl = document.getElementById("status-line");
var overlay = document.getElementById("video-overlay");
var overlayText = document.getElementById("overlay-text");
var logEl = document.getElementById("log");
function setStatus(state, label, subtext) {
  if (statusPill) {
    statusPill.classList.remove("connecting", "connected", "reconnecting", "disconnected");
    statusPill.classList.add(state);
  }
  statusLineEl.classList.remove("connecting", "connected", "reconnecting", "disconnected");
  statusLineEl.classList.add(state);
  if (statusLabelEl)
    statusLabelEl.textContent = label.toUpperCase();
  if (subtext && statusSubtextEl)
    statusSubtextEl.textContent = subtext;
  switch (state) {
    case "connecting":
    case "reconnecting":
      overlay.classList.remove("hidden");
      overlayText.textContent = "Connecting\u2026";
      break;
    case "connected":
      overlay.classList.add("hidden");
      break;
    case "disconnected":
      overlay.classList.remove("hidden");
      overlayText.textContent = "Disconnected \u2013 waiting for stream\u2026";
      break;
  }
}
function setStatusLine(text) {
  console.log("setStatusLine: " + text);
  const textEl = statusLineEl.querySelector(".status-line-text");
  if (textEl) {
    textEl.textContent = text;
  } else {
    statusLineEl.textContent = text;
  }
}
function appendLog(message, type = "info") {
  const div = document.createElement("div");
  div.className = "log-line" + (type === "error" ? " error" : "");
  const time = (/* @__PURE__ */ new Date()).toLocaleTimeString();
  div.innerHTML = `<span>[${time}]</span> <span class="log-tag">${type === "error" ? "ERR" : "LOG"}</span>${message}`;
  logEl.appendChild(div);
  logEl.scrollTop = logEl.scrollHeight;
  console[type === "error" ? "error" : "log"]("[WebRTC UI]", message);
}
var WS_PROTO = location.protocol === "https:" ? "wss" : "ws";
var WS_HOST = location.hostname;
var WS_PORT = window.PEK_CONFIG && window.PEK_CONFIG.wsPort || (location.port || (location.protocol === "https:" ? 443 : 80));
var SIGNALING_URL = `${WS_PROTO}://${WS_HOST}:${WS_PORT}/ws`;
var WEBRTC_TIMING_CONFIG = resolveWebRtcTimingConfig(window.PEK_CONFIG || {});
var WEBRTC_ICE_CONFIG = resolveWebRtcIceConfig(window.PEK_CONFIG || {});
var client = createWebRtcClient({
  video,
  signalingUrl: SIGNALING_URL,
  ...WEBRTC_TIMING_CONFIG,
  ...WEBRTC_ICE_CONFIG,
  logger: appendLog,
  onStatus: setStatus,
  onStatusLine: setStatusLine,
  onRemoteTrack: (track) => {
    if (track.kind === "audio")
      appendLog("Audio track attached. If muted=false, you should hear sound.");
  },
  WebSocketFactory: (url) => new WebSocket(url),
  RTCPeerConnectionFactory: (options) => new RTCPeerConnection(options),
  MediaStreamFactory: () => new MediaStream(),
  RTCSessionDescriptionFactory: (description) => new RTCSessionDescription(description),
  RTCIceCandidateFactory: (candidate) => new RTCIceCandidate(candidate),
  webSocketOpenState: WebSocket.OPEN,
  webSocketConnectingState: WebSocket.CONNECTING
});
window.addEventListener("feed-pause-change", (event) => {
  client.setPaused(event.detail?.paused);
});
client.start();

// development/web/src/video-controls.js
var playBtn = document.getElementById("playPauseBtn");
var playIcon = document.getElementById("playPauseIcon");
var playText = playBtn ? playBtn.querySelector(".video-control-text") : null;
var video2 = document.getElementById("video");
var isPipelinePlaying = true;
var lastToggleRequestedAt = 0;
var DUPLICATE_EVENT_MS = 350;
var setBusy = (busy) => {
  if (!playBtn) return;
  playBtn.disabled = busy;
  playBtn.style.opacity = busy ? "0.7" : "";
};
var renderPlayPause = () => {
  if (!playBtn || !playIcon || !playText) return;
  const isPaused = !isPipelinePlaying;
  document.body.classList.toggle("is-feed-paused", isPaused);
  playIcon.classList.toggle("fa-pause", isPipelinePlaying);
  playIcon.classList.toggle("fa-play", isPaused);
  playText.textContent = isPaused ? "Resume" : "Pause";
  playBtn.setAttribute("aria-label", isPaused ? "Resume pipeline" : "Pause pipeline");
  setBusy(false);
};
var dispatchPauseState = () => {
  window.dispatchEvent(new CustomEvent("feed-pause-change", {
    detail: { paused: Boolean(window.PEK_FEED_PAUSED) }
  }));
};
var freezeFeedFrame = () => {
  if (!video2) return;
  const wrapper = video2.closest(".video-wrapper");
  if (!wrapper) return;
  let canvas2 = document.getElementById("videoFreezeFrame");
  if (!canvas2) {
    canvas2 = document.createElement("canvas");
    canvas2.id = "videoFreezeFrame";
    canvas2.className = "video-freeze-frame";
    canvas2.setAttribute("aria-hidden", "true");
    wrapper.appendChild(canvas2);
  }
  const width = video2.videoWidth || Math.max(1, Math.round(video2.clientWidth || wrapper.clientWidth || 1280));
  const height = video2.videoHeight || Math.max(1, Math.round(video2.clientHeight || wrapper.clientHeight || 720));
  canvas2.width = width;
  canvas2.height = height;
  try {
    const context = canvas2.getContext("2d");
    context?.drawImage(video2, 0, 0, width, height);
  } catch (error) {
    console.warn("Video freeze frame failed", error);
  }
  canvas2.classList.add("is-visible");
};
var resumeFeedFrame = async () => {
  const canvas2 = document.getElementById("videoFreezeFrame");
  canvas2?.classList.remove("is-visible");
  try {
    if (video2?.paused) {
      await video2.play();
    }
  } catch (error) {
    console.warn("Video resume failed", error);
  }
};
var setPlayPause = (isPlaying) => {
  if (typeof isPlaying === "boolean") {
    isPipelinePlaying = isPlaying;
    window.PEK_FEED_PAUSED = !isPipelinePlaying;
    if (isPipelinePlaying) {
      void resumeFeedFrame();
    } else {
      freezeFeedFrame();
    }
    renderPlayPause();
    dispatchPauseState();
    return;
  }
  renderPlayPause();
};
var requestPlayPause = (event) => {
  event?.preventDefault();
  const now = Date.now();
  if (now - lastToggleRequestedAt < DUPLICATE_EVENT_MS) return;
  lastToggleRequestedAt = now;
  setBusy(true);
  ctrlSend({ type: "play_pause" });
};
playBtn?.addEventListener("click", requestPlayPause);
renderPlayPause();

// development/web/src/audio.js
var video3 = document.getElementById("video");
var muteBtn = document.getElementById("muteUnmuteBtn");
var muteIcon = document.getElementById("muteUnmuteIcon");
var muteText = muteBtn.querySelector(".video-control-text");
muteBtn.addEventListener("click", () => {
  video3.muted = !video3.muted;
  if (video3.muted) {
    muteIcon.textContent = "\u{1F507}";
    muteText.textContent = "Unmute";
    muteBtn.setAttribute("aria-label", "Unmute");
  } else {
    muteIcon.textContent = "\u{1F50A}";
    muteText.textContent = "Mute";
    muteBtn.setAttribute("aria-label", "Mute");
  }
});
function enableAudioButton(enable) {
  if (muteBtn) {
    muteBtn.style.display = enable ? "inline-flex" : "none";
  }
}

// development/web/src/models.js
var PREFERRED_MODEL_ORDER = [
  "YoloV11",
  "OsnetX025Reid",
  "Ultraface",
  "CameraContact",
  "GazeDetection"
];
function modelKey(modelOrName) {
  return String(modelOrName?.name || modelOrName || "").toLowerCase();
}
function orderModels(models) {
  const preferred = new Map(PREFERRED_MODEL_ORDER.map((name, index) => [modelKey(name), index]));
  return [...models].sort((a, b) => {
    const aOrder = preferred.get(modelKey(a));
    const bOrder = preferred.get(modelKey(b));
    if (aOrder !== void 0 || bOrder !== void 0) {
      return (aOrder ?? Number.MAX_SAFE_INTEGER) - (bOrder ?? Number.MAX_SAFE_INTEGER);
    }
    return String(a.name || "").localeCompare(String(b.name || ""));
  });
}
var ModelsManager = class {
  constructor() {
    this.container = document.getElementById("models-container");
    this.updateInterval = null;
    this._lastModelsSignature = "";
  }
  render(models) {
    if (!this.container) return;
    const nextSignature = JSON.stringify(models.map((model) => ({
      active: Boolean(model.active),
      element_name: model.element_name || "",
      name: model.name || ""
    })));
    if (nextSignature === this._lastModelsSignature) {
      return;
    }
    this._lastModelsSignature = nextSignature;
    this.container.innerHTML = "";
    if (models.length === 0) {
      this.container.innerHTML = `
                <div class="models-empty">
                    No models registered yet
                </div>
            `;
      return;
    }
    orderModels(models).forEach((model) => {
      const modelItem = this.createModelItem(model);
      this.container.appendChild(modelItem);
    });
  }
  createModelItem(model) {
    const item = document.createElement("div");
    item.className = "model-item";
    item.classList.toggle("model-active", Boolean(model.active));
    item.innerHTML = `
            <div class="model-info">
                <div class="model-name">${model.name}</div>
            </div>
            <div class="model-actions">
                <label class="model-toggle-switch" aria-label="Toggle ${model.name}">
                    <input type="checkbox" role="switch" ${model.active ? "checked" : ""}>
                    <span class="model-toggle-track" aria-hidden="true">
                        <span class="model-toggle-thumb"></span>
                    </span>
                </label>
            </div>
        `;
    const toggle = item.querySelector('input[type="checkbox"]');
    toggle.addEventListener("change", () => {
      const shouldBeActive = toggle.checked;
      this.handleToggle(model, shouldBeActive, item, toggle);
    });
    return item;
  }
  async handleToggle(model, shouldBeActive, item, toggle) {
    if (model.active === shouldBeActive) {
      return;
    }
    const previousActive = model.active;
    toggle.disabled = true;
    item.classList.add("model-pending");
    model.active = shouldBeActive;
    item.classList.toggle("model-active", shouldBeActive);
    try {
      ctrlSend({ type: "model_toggle", name: model.element_name });
    } catch (error) {
      console.error("Error toggling model:", error);
      model.active = previousActive;
      toggle.checked = previousActive;
      item.classList.toggle("model-active", previousActive);
    } finally {
      toggle.disabled = false;
      item.classList.remove("model-pending");
    }
  }
  renderError(message) {
    if (!this.container) return;
    this.container.innerHTML = `
            <div class="models-error">
                <strong>Error loading models:</strong><br>
                ${message}
            </div>
        `;
  }
  destroy() {
    if (this.updateInterval) {
      clearInterval(this.updateInterval);
      this.updateInterval = null;
    }
  }
};
var modelsManager = new ModelsManager();

// development/web/src/ctrlws.js
var CTRL_PROTO = location.protocol === "https:" ? "wss" : "ws";
var CTRL_HOST = location.hostname;
var CTRL_PORT = window.PEK_CONFIG && window.PEK_CONFIG.ctrlPort || (location.port || (location.protocol === "https:" ? 443 : 80));
var CTRL_URL = `${CTRL_PROTO}://${CTRL_HOST}:${CTRL_PORT}/ws`;
var ctrl = null;
var ctrlReconnectDelay = 1e3;
var CTRL_RECONNECT_DELAY_MAX = 15e3;
var CTRL_BACKOFF_FACTOR = 1.1;
function connectCtrl(manual = false) {
  if (ctrl && (ctrl.readyState === WebSocket.OPEN || ctrl.readyState === WebSocket.CONNECTING)) {
    return;
  }
  if (manual) {
    ctrlReconnectDelay = 1e3;
  }
  ctrl = new WebSocket(CTRL_URL);
  ctrl.onopen = () => {
    ctrlReconnectDelay = 1e3;
    flushQueue();
  };
  ctrl.onmessage = async (event) => {
    const data = JSON.parse(event.data);
    console.log("ctrl.onmessage: " + event.data);
    enableAudioButton(data.pipeline_state.audio);
    setPlayPause(data.pipeline_state.playing);
    modelsManager.render(data.models);
  };
  ctrl.onerror = () => {
    ;
  };
  ctrl.onclose = () => {
    setTimeout(() => {
      ctrlReconnectDelay = Math.min(ctrlReconnectDelay * CTRL_BACKOFF_FACTOR, CTRL_RECONNECT_DELAY_MAX);
      connectCtrl();
    }, ctrlReconnectDelay);
  };
}
var sendQueue = [];
function flushQueue() {
  while (ctrl && ctrl.readyState === WebSocket.OPEN && sendQueue.length) {
    ctrl.send(sendQueue.shift());
  }
}
function ctrlSend(obj) {
  const payload = JSON.stringify(obj);
  if (ctrl && ctrl.readyState === WebSocket.OPEN) {
    ctrl.send(payload);
    return true;
  }
  sendQueue.push(payload);
  connectCtrl();
  return false;
}
connectCtrl();

// development/web/src/output-panels.js
var STORAGE_KEY = "pek-layout:output-panels:v1";
var panels = {
  inference: {
    button: document.getElementById("toggleInferenceOutput"),
    section: document.querySelector("[data-inference-output-section]"),
    label: "Inference Output"
  },
  metrics: {
    button: document.getElementById("togglePerformanceOutput"),
    section: document.querySelector("[data-performance-metrics-section]"),
    label: "Performance Metrics"
  },
  debug: {
    button: document.getElementById("toggleDebugOutput"),
    section: document.querySelector("[data-debug-log-section]"),
    label: "Debug Log"
  }
};
var panelKeys = Object.keys(panels);
var DEFAULT_VISIBILITY = {
  inference: true,
  metrics: false,
  debug: false
};
var visibility = readVisibility();
function readVisibility() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) {
      return { ...DEFAULT_VISIBILITY };
    }
    const stored = JSON.parse(raw);
    return Object.fromEntries(
      panelKeys.map((key) => [
        key,
        typeof stored[key] === "boolean" ? stored[key] : DEFAULT_VISIBILITY[key]
      ])
    );
  } catch {
    localStorage.removeItem(STORAGE_KEY);
    return { ...DEFAULT_VISIBILITY };
  }
}
function persistVisibility() {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(visibility));
}
function visiblePanelKeys() {
  return panelKeys.filter((key) => visibility[key] !== false);
}
function setPanelSectionVisibility(panel, visible) {
  if (panel.section) {
    panel.section.hidden = !visible;
  }
}
function setPanelButtonState(panel, visible) {
  if (!panel.button) {
    return;
  }
  const icon2 = panel.button.querySelector("i");
  panel.button.setAttribute("aria-pressed", visible ? "true" : "false");
  panel.button.setAttribute("aria-label", (visible ? "Hide " : "Show ") + panel.label);
  if (icon2) {
    icon2.className = visible ? "fa-solid fa-eye" : "fa-solid fa-eye-slash";
  }
}
function applyPanelState(key) {
  const panel = panels[key];
  const visible = visibility[key] !== false;
  setPanelSectionVisibility(panel, visible);
  setPanelButtonState(panel, visible);
}
function dispatchVisibilityChange(visiblePanels) {
  document.body.classList.toggle("output-panels-empty", visiblePanels.length === 0);
  window.dispatchEvent(new CustomEvent("output-panels-change", {
    detail: {
      visiblePanels
    }
  }));
}
function updatePanelVisibility(visiblePanels) {
  for (const key of panelKeys) {
    applyPanelState(key);
  }
  dispatchVisibilityChange(visiblePanels);
}
function applyVisibility({ animate = false } = {}) {
  const visiblePanels = visiblePanelKeys();
  const update = () => updatePanelVisibility(visiblePanels);
  if (animate && window.animateBottomDockHeightChange) {
    window.animateBottomDockHeightChange(update);
  } else {
    update();
  }
}
for (const key of panelKeys) {
  panels[key].button?.addEventListener("click", () => {
    visibility = {
      ...visibility,
      [key]: visibility[key] === false
    };
    persistVisibility();
    applyVisibility({ animate: true });
  });
}
applyVisibility();

// development/web/src/layout-resize.js
var root = document.documentElement;
var sidePanel = document.getElementById("sidePanel");
var sideHandle = document.getElementById("sidePanelResizeHandle");
var dock = document.querySelector(".bottom-dock");
var dockPanels = document.querySelector(".bottom-dock-panels");
var dockHandle = document.getElementById("bottomDockResizeHandle");
var dockColumnHandles = [...document.querySelectorAll("[data-bottom-column-resize]")];
var dockSections = {
  metrics: document.querySelector("[data-performance-metrics-section]"),
  inference: document.querySelector("[data-inference-output-section]"),
  debug: document.querySelector("[data-debug-log-section]")
};
var sidebarStorageKey = "pek-layout:sidebar-width:v1";
var dockStorageKey = "pek-layout:bottom-dock-height:v1";
var dockColumnStorageKey = "pek-layout:bottom-dock-columns:v1";
var dockColumnKeys = ["inference", "metrics", "debug"];
var dockColumnVariables = {
  metrics: "--bottom-metrics-column",
  inference: "--bottom-inference-column",
  debug: "--bottom-debug-column"
};
var dockColumnFitFrame = null;
var dockColumnTransitionFitFrame = null;
var dockHeightAnimationFrame = null;
function px(value) {
  const parsed = Number.parseFloat(value);
  return Number.isFinite(parsed) ? parsed : null;
}
function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}
function sidebarLimits() {
  const viewport = window.innerWidth || 1200;
  return {
    min: 280,
    max: Math.max(340, Math.min(560, viewport * 0.48))
  };
}
function dockLimits() {
  const viewport = window.innerHeight || 900;
  return {
    min: 116,
    max: Math.max(220, Math.min(560, viewport * 0.62))
  };
}
function visibleDockColumnKeys() {
  return dockColumnKeys.filter((key) => {
    const section = dockSections[key];
    return section && !section.hidden && getComputedStyle(section).display !== "none";
  });
}
function configureDockColumnHandles() {
  const visibleKeys = visibleDockColumnKeys();
  dockColumnKeys.forEach((key) => {
    if (dockSections[key]) {
      dockSections[key].style.gridArea = key;
    }
  });
  dockColumnHandles.forEach((handle, index) => {
    const leftKey = visibleKeys[index];
    const rightKey = visibleKeys[index + 1];
    const active = Boolean(leftKey && rightKey);
    handle.hidden = !active;
    if (active) {
      handle.dataset.bottomColumnResize = `${leftKey}-${rightKey}`;
      handle.style.gridArea = `resize${index}`;
    } else {
      handle.style.gridArea = "";
    }
  });
  if (!dockPanels)
    return;
  const columns = [];
  const areas = [];
  visibleKeys.forEach((key, index) => {
    columns.push(`var(${dockColumnVariables[key]})`);
    areas.push(key);
    if (index < visibleKeys.length - 1) {
      columns.push("10px");
      areas.push(`resize${index}`);
    }
  });
  dockPanels.style.gridTemplateColumns = columns.length ? columns.join(" ") : "minmax(0, 1fr)";
  dockPanels.style.gridTemplateAreas = areas.length ? `"${areas.join(" ")}"` : "none";
}
function visibleDockColumnHandles() {
  return dockColumnHandles.filter((handle) => !handle.hidden && getComputedStyle(handle).display !== "none");
}
function dockColumnHandleWidth() {
  const handle = visibleDockColumnHandles()[0];
  const width = handle ? handle.getBoundingClientRect().width : 10;
  return width || 10;
}
function dockColumnAvailableWidth() {
  if (!dockPanels) return 0;
  const handleSpace = visibleDockColumnHandles().length * dockColumnHandleWidth();
  return Math.max(0, dockPanels.getBoundingClientRect().width - handleSpace);
}
function dockColumnMinimums(available) {
  const compact = available < 900;
  return {
    metrics: compact ? 180 : 220,
    inference: compact ? 220 : 260,
    debug: compact ? 220 : 260
  };
}
function defaultDockColumnWidths() {
  const available = dockColumnAvailableWidth();
  const visibleKeys = visibleDockColumnKeys();
  const evenWidth = visibleKeys.length ? available / visibleKeys.length : 0;
  return Object.fromEntries(dockColumnKeys.map((key) => [
    key,
    visibleKeys.includes(key) ? evenWidth : 0
  ]));
}
function readDockColumnWidths() {
  const visibleKeys = visibleDockColumnKeys();
  const measured = {};
  for (const key of dockColumnKeys) {
    measured[key] = dockSections[key]?.getBoundingClientRect().width || 0;
  }
  if (visibleKeys.length && visibleKeys.every((key) => measured[key] > 0))
    return measured;
  return defaultDockColumnWidths();
}
function normalizeDockColumnWidths(widths) {
  const available = dockColumnAvailableWidth();
  const visibleKeys = visibleDockColumnKeys();
  if (!available)
    return widths;
  if (!visibleKeys.length)
    return Object.fromEntries(dockColumnKeys.map((key) => [key, 0]));
  const minimums = dockColumnMinimums(available);
  const minTotal = visibleKeys.reduce((sum, key) => sum + minimums[key], 0);
  const usableMinimums = minTotal > available ? Object.fromEntries(visibleKeys.map((key) => [key, minimums[key] * available / minTotal])) : minimums;
  const next = Object.fromEntries(dockColumnKeys.map((key) => {
    if (!visibleKeys.includes(key)) {
      return [key, 0];
    }
    const requestedWidth = Number.isFinite(widths[key]) ? widths[key] : 0;
    return [key, Math.max(usableMinimums[key], requestedWidth)];
  }));
  let total = visibleKeys.reduce((sum, key) => sum + next[key], 0);
  if (total > available) {
    let excess = total - available;
    for (const key of ["inference", "metrics", "debug"].filter((item) => visibleKeys.includes(item))) {
      const shrinkable = Math.max(0, next[key] - usableMinimums[key]);
      const shrink = Math.min(shrinkable, excess);
      next[key] -= shrink;
      excess -= shrink;
      if (excess <= 0)
        break;
    }
  } else if (total < available) {
    const extra = available - total;
    const expandableTotal = visibleKeys.reduce((sum, key) => sum + next[key], 0) || 1;
    for (const key of visibleKeys) {
      next[key] += extra * next[key] / expandableTotal;
    }
  }
  return next;
}
function setDockColumnWidths(widths, persist = false) {
  configureDockColumnHandles();
  if (!dockPanels || !visibleDockColumnKeys().length)
    return;
  const next = normalizeDockColumnWidths(widths);
  root.style.setProperty("--bottom-metrics-column", `${Math.round(next.metrics)}px`);
  root.style.setProperty("--bottom-inference-column", `${Math.round(next.inference)}px`);
  root.style.setProperty("--bottom-debug-column", `${Math.round(next.debug)}px`);
  if (persist) {
    localStorage.setItem(dockColumnStorageKey, JSON.stringify({
      metrics: Math.round(next.metrics),
      inference: Math.round(next.inference),
      debug: Math.round(next.debug)
    }));
  }
}
function setEqualDockColumnWidths(persist = false) {
  configureDockColumnHandles();
  if (!dockPanels)
    return;
  const available = dockColumnAvailableWidth();
  const visibleKeys = visibleDockColumnKeys();
  const evenWidth = visibleKeys.length ? available / visibleKeys.length : 0;
  const next = Object.fromEntries(dockColumnKeys.map((key) => [
    key,
    visibleKeys.includes(key) ? evenWidth : 0
  ]));
  root.style.setProperty("--bottom-metrics-column", `${Math.round(next.metrics)}px`);
  root.style.setProperty("--bottom-inference-column", `${Math.round(next.inference)}px`);
  root.style.setProperty("--bottom-debug-column", `${Math.round(next.debug)}px`);
  if (persist) {
    localStorage.setItem(dockColumnStorageKey, JSON.stringify({
      metrics: Math.round(next.metrics),
      inference: Math.round(next.inference),
      debug: Math.round(next.debug)
    }));
  }
}
function scheduleDockColumnFit(persist = false) {
  if (dockColumnFitFrame)
    cancelAnimationFrame(dockColumnFitFrame);
  dockColumnFitFrame = requestAnimationFrame(() => {
    dockColumnFitFrame = null;
    setDockColumnWidths(readDockColumnWidths(), persist);
  });
}
function fitDockColumnsDuringTransition(duration = 260) {
  const startedAt = performance.now();
  if (dockColumnTransitionFitFrame)
    cancelAnimationFrame(dockColumnTransitionFitFrame);
  const fit = () => {
    setDockColumnWidths(readDockColumnWidths());
    if (performance.now() - startedAt < duration) {
      dockColumnTransitionFitFrame = requestAnimationFrame(fit);
      return;
    }
    dockColumnTransitionFitFrame = null;
    scheduleDockColumnFit();
  };
  dockColumnTransitionFitFrame = requestAnimationFrame(fit);
}
function setSidebarWidth(width, persist = false) {
  const { min, max } = sidebarLimits();
  const nextWidth = clamp(width, min, max);
  root.style.setProperty("--sidebar-width", `${nextWidth}px`);
  scheduleDockColumnFit(persist);
  if (persist) {
    localStorage.setItem(sidebarStorageKey, String(Math.round(nextWidth)));
  }
}
function setDockHeight(height, persist = false) {
  const { min, max } = dockLimits();
  const nextHeight = clamp(height, min, max);
  root.style.setProperty("--bottom-dock-height", `${nextHeight}px`);
  if (persist) {
    localStorage.setItem(dockStorageKey, String(Math.round(nextHeight)));
  }
}
function dockTargetHeight() {
  const style = getComputedStyle(root);
  const expandedHeight = px(style.getPropertyValue("--bottom-dock-height")) || 244;
  const collapsedHeight = px(style.getPropertyValue("--bottom-dock-collapsed-height")) || 48;
  const isFullscreen2 = document.body.classList.contains("video-fullscreen");
  const outputsShown = document.body.classList.contains("fullscreen-outputs-enabled");
  const outputsEmpty = document.body.classList.contains("output-panels-empty");
  if (isFullscreen2 && !outputsShown)
    return 0;
  return outputsEmpty ? collapsedHeight : expandedHeight;
}
function easeOutCubic(progress) {
  return 1 - Math.pow(1 - progress, 3);
}
function animateBottomDockHeightChange(change) {
  const mainContent = document.querySelector(".main-content");
  if (!mainContent || !dock) {
    change();
    return;
  }
  if (dockHeightAnimationFrame)
    cancelAnimationFrame(dockHeightAnimationFrame);
  const startHeight = dock.getBoundingClientRect().height;
  mainContent.style.setProperty("--bottom-dock-current-height", `${startHeight}px`);
  change();
  const endHeight = dockTargetHeight();
  if (Math.abs(startHeight - endHeight) < 1 || window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    mainContent.style.setProperty("--bottom-dock-current-height", `${endHeight}px`);
    requestAnimationFrame(() => mainContent.style.removeProperty("--bottom-dock-current-height"));
    scheduleDockColumnFit();
    return;
  }
  const startedAt = performance.now();
  const duration = 220;
  const step = (now) => {
    const progress = clamp((now - startedAt) / duration, 0, 1);
    const nextHeight = startHeight + (endHeight - startHeight) * easeOutCubic(progress);
    mainContent.style.setProperty("--bottom-dock-current-height", `${nextHeight}px`);
    if (progress < 1) {
      dockHeightAnimationFrame = requestAnimationFrame(step);
      return;
    }
    dockHeightAnimationFrame = null;
    mainContent.style.setProperty("--bottom-dock-current-height", `${endHeight}px`);
    requestAnimationFrame(() => mainContent.style.removeProperty("--bottom-dock-current-height"));
    scheduleDockColumnFit();
  };
  dockHeightAnimationFrame = requestAnimationFrame(step);
}
window.animateBottomDockHeightChange = animateBottomDockHeightChange;
function restoreLayoutSizes() {
  const storedSidebarWidth = px(localStorage.getItem(sidebarStorageKey));
  if (storedSidebarWidth) {
    setSidebarWidth(storedSidebarWidth);
  }
  const storedDockHeight = px(localStorage.getItem(dockStorageKey));
  if (storedDockHeight) {
    setDockHeight(storedDockHeight);
  }
  try {
    const storedDockColumns = JSON.parse(localStorage.getItem(dockColumnStorageKey) || "null");
    if (storedDockColumns && dockColumnKeys.every((key) => px(storedDockColumns[key]))) {
      setDockColumnWidths(storedDockColumns);
    }
  } catch {
    localStorage.removeItem(dockColumnStorageKey);
  }
}
function startSidebarResize(event) {
  if (event.button !== void 0 && event.button !== 0) return;
  if (!sidePanel) return;
  event.preventDefault();
  const startX = event.clientX;
  const startWidth = sidePanel.getBoundingClientRect().width;
  document.body.classList.add("is-resizing-layout", "is-resizing-sidebar");
  sideHandle?.setPointerCapture?.(event.pointerId);
  const move = (moveEvent) => {
    setSidebarWidth(startWidth + moveEvent.clientX - startX);
  };
  const stop = () => {
    document.body.classList.remove("is-resizing-layout", "is-resizing-sidebar");
    const currentWidth = px(getComputedStyle(root).getPropertyValue("--sidebar-width")) || startWidth;
    setSidebarWidth(currentWidth, true);
    window.removeEventListener("pointermove", move);
    window.removeEventListener("pointerup", stop);
    window.removeEventListener("pointercancel", stop);
  };
  window.addEventListener("pointermove", move);
  window.addEventListener("pointerup", stop, { once: true });
  window.addEventListener("pointercancel", stop, { once: true });
}
function startDockResize(event) {
  if (event.button !== void 0 && event.button !== 0) return;
  if (!dock) return;
  event.preventDefault();
  const startY = event.clientY;
  const startHeight = dock.getBoundingClientRect().height;
  document.body.classList.add("is-resizing-layout", "is-resizing-bottom-dock");
  dockHandle?.setPointerCapture?.(event.pointerId);
  const move = (moveEvent) => {
    setDockHeight(startHeight + startY - moveEvent.clientY);
  };
  const stop = () => {
    document.body.classList.remove("is-resizing-layout", "is-resizing-bottom-dock");
    const currentHeight = px(getComputedStyle(root).getPropertyValue("--bottom-dock-height")) || startHeight;
    setDockHeight(currentHeight, true);
    window.removeEventListener("pointermove", move);
    window.removeEventListener("pointerup", stop);
    window.removeEventListener("pointercancel", stop);
  };
  window.addEventListener("pointermove", move);
  window.addEventListener("pointerup", stop, { once: true });
  window.addEventListener("pointercancel", stop, { once: true });
}
function startDockColumnResize(event) {
  if (event.button !== void 0 && event.button !== 0) return;
  if (!dock || !visibleDockColumnHandles().includes(event.currentTarget)) return;
  const resizeTarget = event.currentTarget.dataset.bottomColumnResize;
  const [leftKey, rightKey] = resizeTarget.split("-");
  if (!dockSections[leftKey] || !dockSections[rightKey]) return;
  event.preventDefault();
  const startX = event.clientX;
  const startWidths = readDockColumnWidths();
  const pairWidth = startWidths[leftKey] + startWidths[rightKey];
  const minimums = dockColumnMinimums(dockColumnAvailableWidth());
  const pairMinimumTotal = minimums[leftKey] + minimums[rightKey];
  const minScale = pairMinimumTotal > pairWidth ? pairWidth / pairMinimumTotal : 1;
  const minLeft = minimums[leftKey] * minScale;
  const minRight = minimums[rightKey] * minScale;
  document.body.classList.add("is-resizing-layout", "is-resizing-bottom-column");
  event.currentTarget.setPointerCapture?.(event.pointerId);
  const move = (moveEvent) => {
    const delta = moveEvent.clientX - startX;
    const nextLeft = clamp(startWidths[leftKey] + delta, minLeft, pairWidth - minRight);
    const nextWidths = {
      ...startWidths,
      [leftKey]: nextLeft,
      [rightKey]: pairWidth - nextLeft
    };
    setDockColumnWidths(nextWidths);
  };
  const stop = () => {
    document.body.classList.remove("is-resizing-layout", "is-resizing-bottom-column");
    setDockColumnWidths(readDockColumnWidths(), true);
    window.removeEventListener("pointermove", move);
    window.removeEventListener("pointerup", stop);
    window.removeEventListener("pointercancel", stop);
  };
  window.addEventListener("pointermove", move);
  window.addEventListener("pointerup", stop, { once: true });
  window.addEventListener("pointercancel", stop, { once: true });
}
restoreLayoutSizes();
configureDockColumnHandles();
if (visibleDockColumnKeys().length > 1) {
  setEqualDockColumnWidths();
} else {
  scheduleDockColumnFit();
}
if (dockPanels && "ResizeObserver" in window) {
  const dockPanelsResizeObserver = new ResizeObserver(() => {
    scheduleDockColumnFit();
  });
  dockPanelsResizeObserver.observe(dockPanels);
}
sideHandle?.addEventListener("pointerdown", startSidebarResize);
dockHandle?.addEventListener("pointerdown", startDockResize);
dockColumnHandles.forEach((handle) => handle.addEventListener("pointerdown", startDockColumnResize));
window.addEventListener("resize", () => {
  const currentSidebarWidth = px(getComputedStyle(root).getPropertyValue("--sidebar-width"));
  if (currentSidebarWidth) setSidebarWidth(currentSidebarWidth, true);
  const currentDockHeight = px(getComputedStyle(root).getPropertyValue("--bottom-dock-height"));
  if (currentDockHeight) setDockHeight(currentDockHeight, true);
  scheduleDockColumnFit(true);
});
window.addEventListener("output-panels-change", (event) => {
  configureDockColumnHandles();
  const visiblePanelCount = event.detail?.visiblePanels?.length || 0;
  if (visiblePanelCount > 1) {
    requestAnimationFrame(() => setEqualDockColumnWidths(true));
    return;
  }
  scheduleDockColumnFit(true);
});
window.addEventListener("video-layout-change", () => {
  configureDockColumnHandles();
  fitDockColumnsDuringTransition();
});
document.querySelector(".card-body")?.addEventListener("transitionend", (event) => {
  if (event.propertyName === "grid-template-columns") {
    scheduleDockColumnFit();
  }
});
document.querySelector(".main-content")?.addEventListener("transitionend", (event) => {
  if (["--bottom-dock-current-height", "grid-template-rows"].includes(event.propertyName)) {
    scheduleDockColumnFit();
  }
});

// development/web/src/video-fullscreen.js
var button = document.getElementById("videoFullscreenBtn");
var icon = document.getElementById("videoFullscreenIcon");
var controls = document.querySelector(".video-control-buttons");
var videoWrapper = document.querySelector(".video-wrapper");
var outputsButton = document.getElementById("fullscreenOutputsBtn");
var outputsIcon = document.getElementById("fullscreenOutputsIcon");
var videoFeedButton = document.getElementById("toggleVideoFeedBtn");
var videoFeedIcon = document.getElementById("toggleVideoFeedIcon");
var isFullscreen = false;
var hideTimer = null;
var outputsAvailable = true;
var outputsInFullscreen = (localStorage.getItem("pek-video:fullscreen-outputs:v1") ?? localStorage.getItem("pek-video:fullscreen-metrics:v1")) === "true";
var videoFeedHidden = localStorage.getItem("pek-video:feed-hidden:v1") === "true";
function notifyVideoLayoutChange() {
  window.dispatchEvent(new CustomEvent("video-layout-change", {
    detail: {
      isFullscreen,
      outputsInFullscreen,
      outputsAvailable,
      videoFeedHidden
    }
  }));
  requestAnimationFrame(() => {
    window.dispatchEvent(new CustomEvent("video-layout-change", {
      detail: {
        isFullscreen,
        outputsInFullscreen,
        outputsAvailable,
        videoFeedHidden
      }
    }));
  });
}
function setControlsVisible(visible) {
  document.body.classList.toggle("video-fullscreen-controls-visible", visible);
}
function scheduleHideControls(delay = 1300) {
  window.clearTimeout(hideTimer);
  hideTimer = window.setTimeout(() => {
    if (isFullscreen && !controls?.matches(":hover, :focus-within") && !videoWrapper?.matches(":hover")) {
      setControlsVisible(false);
    }
  }, delay);
}
function isPointerInsideVideoWrapper(event, margin = 0) {
  const rect = videoWrapper?.getBoundingClientRect();
  if (!rect) return false;
  return event.clientX >= rect.left - margin && event.clientX <= rect.right + margin && event.clientY >= rect.top - margin && event.clientY <= rect.bottom + margin;
}
function setVideoFeedHidden(hidden) {
  videoFeedHidden = hidden;
  document.body.classList.toggle("video-feed-hidden", videoFeedHidden);
  localStorage.setItem("pek-video:feed-hidden:v1", videoFeedHidden ? "true" : "false");
  if (videoFeedButton) {
    videoFeedButton.setAttribute("aria-label", videoFeedHidden ? "Show video feed" : "Hide video feed");
    videoFeedButton.setAttribute("aria-pressed", videoFeedHidden ? "true" : "false");
    videoFeedButton.dataset.tooltip = videoFeedHidden ? "Show video feed" : "Hide video feed";
  }
  if (videoFeedIcon) {
    videoFeedIcon.className = videoFeedHidden ? "fa-solid fa-eye" : "fa-solid fa-eye-slash";
  }
  notifyVideoLayoutChange();
}
function setOutputsInFullscreen(enabled, { animate = true } = {}) {
  const update = () => {
    outputsInFullscreen = enabled;
    document.body.classList.toggle("fullscreen-outputs-enabled", outputsInFullscreen);
    localStorage.setItem("pek-video:fullscreen-outputs:v1", outputsInFullscreen ? "true" : "false");
    if (outputsButton) {
      outputsButton.setAttribute(
        "aria-label",
        outputsInFullscreen ? "Hide outputs in fullscreen" : "Show outputs in fullscreen"
      );
      outputsButton.setAttribute("aria-pressed", outputsInFullscreen ? "true" : "false");
      outputsButton.dataset.tooltip = outputsInFullscreen ? "Hide outputs in fullscreen" : "Show outputs in fullscreen";
    }
    if (outputsIcon) {
      outputsIcon.className = outputsInFullscreen ? "fa-solid fa-gauge" : "fa-solid fa-gauge video-gauge-icon--outline";
    }
  };
  if (animate && window.animateBottomDockHeightChange) {
    window.animateBottomDockHeightChange(update);
  } else {
    update();
  }
  notifyVideoLayoutChange();
}
function setOutputsAvailable(available) {
  outputsAvailable = available;
  if (outputsButton) {
    outputsButton.hidden = false;
  }
  notifyVideoLayoutChange();
}
function setFullscreen(nextFullscreen) {
  isFullscreen = nextFullscreen;
  document.body.classList.toggle("video-fullscreen", isFullscreen);
  if (button) {
    button.setAttribute("aria-label", isFullscreen ? "Exit fullscreen video" : "Fullscreen video");
    button.dataset.tooltip = isFullscreen ? "Exit Fullscreen" : "Fullscreen";
  }
  if (icon) {
    icon.className = isFullscreen ? "fa-solid fa-down-left-and-up-right-to-center" : "fa-solid fa-up-right-and-down-left-from-center";
  }
  setControlsVisible(isFullscreen);
  if (isFullscreen) {
    scheduleHideControls();
  } else {
    window.clearTimeout(hideTimer);
  }
  notifyVideoLayoutChange();
}
setVideoFeedHidden(videoFeedHidden);
setOutputsInFullscreen(outputsInFullscreen, { animate: false });
setOutputsAvailable(outputsAvailable);
button?.addEventListener("click", () => {
  const nextFullscreen = !isFullscreen;
  setFullscreen(nextFullscreen);
  if (nextFullscreen) {
    button.blur();
  }
});
outputsButton?.addEventListener("click", () => {
  setOutputsInFullscreen(!outputsInFullscreen);
});
videoFeedButton?.addEventListener("click", () => {
  setVideoFeedHidden(!videoFeedHidden);
});
window.addEventListener("output-panels-change", (event) => {
  setOutputsAvailable((event.detail?.visiblePanels?.length || 0) > 0);
});
controls?.addEventListener("mouseenter", () => {
  if (isFullscreen) {
    setControlsVisible(true);
    window.clearTimeout(hideTimer);
  }
});
controls?.addEventListener("mouseleave", () => {
  if (isFullscreen) scheduleHideControls(600);
});
videoWrapper?.addEventListener("mouseenter", () => {
  if (isFullscreen) {
    setControlsVisible(true);
    scheduleHideControls();
  }
});
videoWrapper?.addEventListener("mousemove", () => {
  if (isFullscreen) {
    setControlsVisible(true);
    scheduleHideControls();
  }
});
document.addEventListener("mousemove", (event) => {
  if (!isFullscreen) return;
  if (isPointerInsideVideoWrapper(event, 8)) {
    setControlsVisible(true);
    scheduleHideControls();
    return;
  }
  if (!controls?.matches(":hover, :focus-within")) {
    scheduleHideControls(250);
  }
});
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && isFullscreen) {
    setFullscreen(false);
  }
});

// development/web/src/copy-utils.js?v=icon-copy-buttons-20260608
async function writeClipboard(text) {
  if (!navigator.clipboard?.writeText) {
    throw new Error("Clipboard API is unavailable");
  }
  try {
    await navigator.clipboard.writeText(text);
  } catch (error) {
    throw new Error("Clipboard write failed", { cause: error });
  }
}
function setButtonIcon(button2, iconName) {
  const icon2 = button2?.querySelector("i");
  if (!icon2)
    return;
  icon2.className = `fa-solid fa-${iconName}`;
}
function setButtonFeedback(button2, state, label) {
  button2.dataset.copyState = state;
  button2.setAttribute("aria-label", label);
  button2.title = label;
  setButtonIcon(button2, state === "copied" ? "check" : "copy");
}
async function copyTextWithFeedback(button2, text, emptyText = "Empty", fallbackBuffer = null) {
  if (!button2) return;
  if (!String(text || "").trim()) return;
  const originalLabel = button2.getAttribute("aria-label") || button2.title || "Copy";
  if (button2.copyFeedbackTimer) {
    clearTimeout(button2.copyFeedbackTimer);
    button2.copyFeedbackTimer = null;
  }
  button2.dataset.copyState = "copying";
  try {
    await writeClipboard(text);
    setButtonFeedback(button2, text ? "copied" : "empty", text ? "Copied" : emptyText);
  } catch (error) {
    console.debug("Clipboard copy failed", error);
    if (fallbackBuffer) {
      fallbackBuffer.value = text;
      fallbackBuffer.classList.add("is-visible");
      fallbackBuffer.focus();
      fallbackBuffer.select();
      fallbackBuffer.setSelectionRange(0, fallbackBuffer.value.length);
      setButtonFeedback(button2, "manual-copy", "Select text to copy");
    } else {
      setButtonFeedback(button2, "failed", "Copy failed");
    }
  }
  button2.copyFeedbackTimer = setTimeout(() => {
    setButtonFeedback(button2, "idle", originalLabel);
    button2.copyFeedbackTimer = null;
  }, 1500);
}
function setCopyButtonAvailable(button2, available) {
  if (!button2) return;
  button2.disabled = !available;
  button2.setAttribute("aria-disabled", available ? "false" : "true");
}

// development/web/src/performance-metrics.js
var body = document.getElementById("performanceMetricsBody");
var copyButton = document.getElementById("copyPerformanceMetricsBtn");
var MAX_METRIC_LINE_LENGTH = 240;
var currentRows = [];
function decimalText(value) {
  const parts = String(value || "").split(".");
  if (parts.length > 2 || parts.some((part) => part.length === 0)) {
    return false;
  }
  return parts.every((part) => [...part].every((char) => char >= "0" && char <= "9"));
}
function parseMillisToken(token) {
  if (!token.endsWith("ms")) {
    return "";
  }
  const value = token.slice(0, -2);
  return decimalText(value) ? `${value}ms` : "";
}
function parsePipelineFps(text) {
  if (!text.startsWith("Pipeline")) {
    return null;
  }
  const rest = text.slice("Pipeline".length).trim();
  const valueText = (rest.startsWith(":") ? rest.slice(1) : rest).trim();
  if (!valueText.endsWith(" FPS")) {
    return null;
  }
  const fps = valueText.slice(0, -" FPS".length).trim();
  return decimalText(fps) ? { stage: "Pipeline", current: `${fps} FPS`, p95: "" } : null;
}
function parseP95Text(text) {
  if (!text) {
    return "";
  }
  if (!text.startsWith("(p95:") || !text.endsWith(")")) {
    return "";
  }
  return parseMillisToken(text.slice(5, -1).trim());
}
function parseTimedMetric(text) {
  const separator = text.indexOf(":");
  if (separator <= 0) {
    return null;
  }
  const stage = text.slice(0, separator).trim();
  const rest = text.slice(separator + 1).trim();
  const firstSpace = rest.indexOf(" ");
  const currentToken = firstSpace < 0 ? rest : rest.slice(0, firstSpace);
  const current = parseMillisToken(currentToken);
  if (!stage || !current) {
    return null;
  }
  const p95Text = firstSpace < 0 ? "" : rest.slice(firstSpace + 1).trim();
  return { stage, current, p95: parseP95Text(p95Text) };
}
function parseMetricLine(line) {
  const text = String(line || "").slice(0, MAX_METRIC_LINE_LENGTH).replaceAll("\u2550", "").trim();
  if (!text) return null;
  return parsePipelineFps(text) || parseTimedMetric(text);
}
function createCell(text, className) {
  const cell = document.createElement("td");
  if (className) {
    cell.className = className;
  }
  cell.textContent = String(text ?? "");
  return cell;
}
function renderEmptyRow() {
  const row = document.createElement("tr");
  const cell = createCell("No metrics yet", "performance-metrics-empty");
  cell.colSpan = 3;
  row.appendChild(cell);
  return row;
}
function renderMetricRow(metric) {
  const row = document.createElement("tr");
  row.appendChild(createCell(metric.stage));
  row.appendChild(createCell(metric.current));
  row.appendChild(createCell(metric.p95));
  return row;
}
function renderRows(rows) {
  if (!body) return;
  currentRows = rows;
  updateCopyButtonState();
  body.replaceChildren(...rows.length ? rows.map(renderMetricRow) : [renderEmptyRow()]);
}
function renderPerformanceMetrics(performance2) {
  const lines = Array.isArray(performance2?.lines) ? performance2.lines : [];
  renderRows(lines.map(parseMetricLine).filter(Boolean));
}
function updateCopyButtonState() {
  setCopyButtonAvailable(copyButton, currentRows.length > 0);
}
function getMetricsText() {
  if (!currentRows.length) return "";
  return [
    "Stage	Current	P95",
    ...currentRows.map((row) => `${row.stage}	${row.current}	${row.p95}`)
  ].join("\n");
}
copyButton?.addEventListener("click", () => {
  copyTextWithFeedback(copyButton, getMetricsText());
});
window.addEventListener("metadata-message", (event) => {
  if (event.detail?.performance) {
    renderPerformanceMetrics(event.detail.performance);
  }
});
updateCopyButtonState();

// development/web/src/inference-output.js
var body2 = document.getElementById("inferenceOutputBody");
var copyButton2 = document.getElementById("copyInferenceOutputBtn");
var currentLayers = [];
var latestOutput = null;
var number = (value, digits = 2) => {
  if (typeof value !== "number" || !Number.isFinite(value)) return "";
  return value.toFixed(digits);
};
function detectionSummary(detection) {
  const type = detection?.type || "Detection";
  const data = detection?.data || {};
  if (type === "Rect") {
    const label = data.text || `Class ${data.classId ?? "-"}`;
    const confidence = number(data.confidence);
    return {
      title: label,
      detail: `box x ${number(data.x)} y ${number(data.y)} w ${number(data.width)} h ${number(data.height)}`,
      meta: confidence ? `${confidence}` : ""
    };
  }
  if (type === "YawPitch") {
    return {
      title: "Yaw / pitch",
      detail: `yaw ${number(data.yaw)} pitch ${number(data.pitch)}`,
      meta: number(data.confidence)
    };
  }
  if (type === "Classification") {
    const best = Array.isArray(data.candidates) ? data.candidates[0] : null;
    return {
      title: best?.text || `Class ${best?.classId ?? "-"}`,
      detail: "classification",
      meta: number(best?.confidence)
    };
  }
  if (type === "LocalizedText") {
    return {
      title: data.text || "Text",
      detail: `text x ${number(data.x)} y ${number(data.y)} w ${number(data.w)} h ${number(data.h)}`,
      meta: ""
    };
  }
  if (type === "TrackTrace") {
    return {
      title: `Track ${data.trackId ?? "-"}`,
      detail: "trace",
      meta: ""
    };
  }
  if (type === "PersonClassification") {
    return {
      title: "Person classification",
      detail: `yes ${number(data.yesConfidence)} no ${number(data.noConfidence)}`,
      meta: ""
    };
  }
  return {
    title: type,
    detail: Object.keys(data).slice(0, 4).join(", ") || "output",
    meta: ""
  };
}
function layerTitle(layer) {
  return layer.model || layer.contentType || layer.engine || "Layer";
}
function createNode(tag, className, text) {
  const node = document.createElement(tag);
  if (className) {
    node.className = className;
  }
  if (text !== void 0) {
    node.textContent = String(text);
  }
  return node;
}
function renderEmpty(message) {
  return createNode("div", "inference-output-empty", message);
}
function renderDetection(row) {
  const item = createNode("div", "inference-detection");
  const main = createNode("div", "inference-detection-main");
  main.appendChild(createNode("span", "inference-detection-title", row.title));
  if (row.meta) {
    main.appendChild(createNode("span", "inference-detection-meta", row.meta));
  }
  item.appendChild(main);
  item.appendChild(createNode("div", "inference-detection-detail", row.detail));
  return item;
}
function renderLayer(layer) {
  const detections = Array.isArray(layer.detections) ? layer.detections : [];
  const rows = detections.map(detectionSummary);
  const hiddenCount = Math.max(0, (layer.count || 0) - rows.length);
  const container = createNode("div", "inference-layer");
  const header = createNode("div", "inference-layer-header");
  header.appendChild(createNode("span", "inference-layer-title", layerTitle(layer)));
  header.appendChild(createNode("span", "inference-layer-count", layer.count || 0));
  container.appendChild(header);
  container.appendChild(createNode("div", "inference-layer-kind", layer.contentType || layer.labelFamily || "output"));
  if (rows.length) {
    const detectionsNode = createNode("div", "inference-detections");
    rows.forEach((row) => detectionsNode.appendChild(renderDetection(row)));
    container.appendChild(detectionsNode);
  } else {
    container.appendChild(renderEmpty("No detections"));
  }
  if (hiddenCount) {
    container.appendChild(renderEmpty(`${hiddenCount} more not shown`));
  }
  return container;
}
function renderInferenceOutput(output) {
  if (!body2) return;
  latestOutput = output || latestOutput;
  if (window.PEK_FEED_PAUSED) {
    currentLayers = [];
    updateCopyButtonState2();
    body2.replaceChildren(renderEmpty("Inference paused."));
    return;
  }
  const layers = Array.isArray(output?.layers) ? output.layers.filter((layer) => layer.model || layer.contentType || layer.engine) : [];
  currentLayers = layers;
  updateCopyButtonState2();
  if (!layers.length) {
    body2.replaceChildren(renderEmpty("No inference output yet, enable a model to see inference here."));
    return;
  }
  body2.replaceChildren(...layers.map(renderLayer));
}
function copyableLayers() {
  return currentLayers.filter((layer) => {
    const detections = Array.isArray(layer.detections) ? layer.detections : [];
    return detections.length > 0 || Number(layer.count || 0) > 0;
  });
}
function updateCopyButtonState2() {
  setCopyButtonAvailable(copyButton2, copyableLayers().length > 0);
}
function getInferenceText() {
  const layers = copyableLayers();
  if (!layers.length) return "";
  return layers.map((layer) => {
    const detections = Array.isArray(layer.detections) ? layer.detections : [];
    const rows = detections.map(detectionSummary);
    const lines = [
      `${layerTitle(layer)} (${layer.contentType || layer.labelFamily || "output"}): ${layer.count || 0}`,
      ...rows.map((row) => {
        const meta = row.meta ? ` [${row.meta}]` : "";
        return `- ${row.title}${meta}: ${row.detail}`;
      })
    ];
    return lines.join("\n");
  }).join("\n\n");
}
copyButton2?.addEventListener("click", () => {
  copyTextWithFeedback(copyButton2, getInferenceText());
});
window.addEventListener("metadata-message", (event) => {
  if (event.detail?.inference_output) {
    renderInferenceOutput(event.detail.inference_output);
  }
});
window.addEventListener("feed-pause-change", () => {
  renderInferenceOutput(latestOutput);
});
updateCopyButtonState2();

// development/web/src/debug-log.js
var copyButton3 = document.getElementById("copyDebugLogBtn");
var logEl2 = document.getElementById("log");
var copyBuffer = document.getElementById("debugLogCopyBuffer");
function getLogText() {
  if (!logEl2) return "";
  return Array.from(logEl2.querySelectorAll(".log-line")).map((line) => line.textContent.trim()).filter(Boolean).join("\n");
}
async function copyLog() {
  if (!copyButton3) return;
  const logText = getLogText();
  copyBuffer?.classList.remove("is-visible");
  await copyTextWithFeedback(copyButton3, logText, "Copy", copyBuffer);
}
function updateCopyButtonState3() {
  setCopyButtonAvailable(copyButton3, !!getLogText());
}
copyButton3?.addEventListener("click", copyLog);
if (logEl2) {
  new MutationObserver(updateCopyButtonState3).observe(logEl2, {
    childList: true,
    subtree: true,
    characterData: true
  });
}
updateCopyButtonState3();

// development/web/src/metadata-ws.js
var WS_PROTO2 = location.protocol === "https:" ? "wss" : "ws";
var WS_HOST2 = location.hostname;
var DEFAULT_METADATA_PORT = 8002;
var RECONNECT_DELAY_MS = 1500;
var RECONNECT_DELAY_MAX_MS = 15e3;
var BACKOFF_FACTOR = 1.2;
function metadataUrl() {
  const config = window.PEK_CONFIG || {};
  if (config.metadataUrl) {
    return config.metadataUrl;
  }
  const port = config.metadataPort || DEFAULT_METADATA_PORT;
  const endpoint = config.metadataEndpoint || "/ws";
  return `${WS_PROTO2}://${WS_HOST2}:${port}${endpoint}`;
}
function normaliseInferenceOutput(perception) {
  const layers = Array.isArray(perception?.layers) ? perception.layers : [];
  return {
    layers: layers.map((layer) => {
      const detections = Array.isArray(layer?.detections) ? layer.detections : [];
      return {
        ...layer,
        count: Number.isFinite(layer?.count) ? layer.count : detections.length
      };
    })
  };
}
function normalisePerformance(perception) {
  return {
    lines: Array.isArray(perception?.perfdata) ? perception.perfdata : []
  };
}
function normaliseMetadataMessage(message) {
  if (message && typeof message === "object" && Object.hasOwn(message, "perception")) {
    return {
      frame_counter: message.frame_counter,
      perception: message.perception && typeof message.perception === "object" ? message.perception : null
    };
  }
  if (message && typeof message === "object" && (Array.isArray(message.layers) || Array.isArray(message.perfdata))) {
    return {
      frame_counter: message.frame_counter,
      perception: message
    };
  }
  return {
    frame_counter: message?.frame_counter,
    perception: null
  };
}
function handleMetadataMessage(raw) {
  let message;
  try {
    message = JSON.parse(String(raw).trim());
  } catch (error) {
    console.warn("Invalid metadata WebSocket message", error);
    return;
  }
  const { frame_counter, perception } = normaliseMetadataMessage(message);
  if (!perception) {
    window.dispatchEvent(new CustomEvent("metadata-message", {
      detail: {
        frame_counter,
        perception: null,
        inference_output: { layers: [] },
        performance: { lines: [] }
      }
    }));
    return;
  }
  const inference_output = normaliseInferenceOutput(perception);
  const performance2 = normalisePerformance(perception);
  window.dispatchEvent(new CustomEvent("metadata-message", {
    detail: {
      frame_counter,
      perception,
      inference_output,
      performance: performance2
    }
  }));
}
var socket = null;
var reconnectDelay = RECONNECT_DELAY_MS;
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
  socket.onerror = () => {
    ;
  };
  socket.onclose = () => {
    window.setTimeout(() => {
      reconnectDelay = Math.min(reconnectDelay * BACKOFF_FACTOR, RECONNECT_DELAY_MAX_MS);
      connect();
    }, reconnectDelay);
  };
}
connect();

// development/web/src/osd-renderer.js
var DEFAULT_COLORS = {
  objects: "#ff3030",
  faces: "#2600ff",
  text: "#ffffff",
  textBg: "rgba(0, 0, 0, 0.78)",
  performance: "#66ff00",
  cameraContact: "#66ff00",
  noContact: "#ff4040",
  gaze: "#fafad2",
  trackTraces: "#ffff00"
};
var DEFAULT_RENDER_OPTIONS = {
  objects: true,
  faces: true,
  gaze: true,
  cameraContact: true,
  trackTraces: true,
  classification: true,
  personStatus: true,
  performance: true
};
function resizeCanvasToDisplaySize(canvas2) {
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.round(canvas2.clientWidth * dpr));
  const height = Math.max(1, Math.round(canvas2.clientHeight * dpr));
  if (canvas2.width !== width || canvas2.height !== height) {
    canvas2.width = width;
    canvas2.height = height;
  }
}
function renderOsd(canvas2, video5, perception, options = {}) {
  resizeCanvasToDisplaySize(canvas2);
  const ctx = canvas2.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const width = canvas2.width / dpr;
  const height = canvas2.height / dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);
  if (!perception || !Array.isArray(perception.layers)) {
    return;
  }
  const mapper = createCoordinateMapper({
    canvasWidth: width,
    canvasHeight: height,
    videoWidth: video5.videoWidth,
    videoHeight: video5.videoHeight,
    perception
  });
  const renderOptions = {
    ...DEFAULT_RENDER_OPTIONS,
    ...options,
    colors: { ...DEFAULT_COLORS, ...options.colors || {} }
  };
  const now = options.now ?? Date.now();
  drawLayers(ctx, perception, mapper, renderOptions, now);
  if (renderOptions.gaze) {
    drawGazeVectors(ctx, perception, mapper, renderOptions.colors);
  }
  if (renderOptions.cameraContact) {
    drawCameraContactMarkers(ctx, perception, mapper, renderOptions.colors);
  }
  if (renderOptions.performance) {
    drawPerformance(ctx, perception, renderOptions.colors);
  }
}
function createCoordinateMapper({ canvasWidth, canvasHeight, videoWidth, videoHeight, perception }) {
  const frame = findVideoFrame(perception);
  const sourceWidth = positiveNumber(frame?.originalWidth) || positiveNumber(videoWidth) || canvasWidth;
  const sourceHeight = positiveNumber(frame?.originalHeight) || positiveNumber(videoHeight) || canvasHeight;
  const display = calculateContainedRect(canvasWidth, canvasHeight, positiveNumber(videoWidth) || sourceWidth, positiveNumber(videoHeight) || sourceHeight);
  const scaleX = display.width / sourceWidth;
  const scaleY = display.height / sourceHeight;
  return {
    sourceWidth,
    sourceHeight,
    display,
    point(x, y) {
      return {
        x: display.x + Number(x || 0) * scaleX,
        y: display.y + Number(y || 0) * scaleY
      };
    },
    lengthX(value) {
      return Number(value || 0) * scaleX;
    },
    lengthY(value) {
      return Number(value || 0) * scaleY;
    },
    rect(rect) {
      const origin = this.point(rect.x, rect.y);
      return {
        x: origin.x,
        y: origin.y,
        width: this.lengthX(rect.width ?? rect.w),
        height: this.lengthY(rect.height ?? rect.h)
      };
    }
  };
}
function calculateContainedRect(canvasWidth, canvasHeight, videoWidth, videoHeight) {
  if (!positiveNumber(videoWidth) || !positiveNumber(videoHeight)) {
    return { x: 0, y: 0, width: canvasWidth, height: canvasHeight };
  }
  const canvasRatio = canvasWidth / canvasHeight;
  const videoRatio = videoWidth / videoHeight;
  if (canvasRatio > videoRatio) {
    const height2 = canvasHeight;
    const width2 = height2 * videoRatio;
    return { x: (canvasWidth - width2) / 2, y: 0, width: width2, height: height2 };
  }
  const width = canvasWidth;
  const height = width / videoRatio;
  return { x: 0, y: (canvasHeight - height) / 2, width, height };
}
function findVideoFrame(perception) {
  for (const layer of perception?.layers || []) {
    for (const detection of layer.detections || []) {
      if (detection?.type === "VideoFrame" && detection.data) {
        return detection.data;
      }
    }
  }
  return null;
}
function collectRectsByContentType(perception, contentType) {
  const rects = [];
  for (const layer of perception?.layers || []) {
    if (layer.contentType !== contentType) {
      continue;
    }
    for (const detection of layer.detections || []) {
      if (detection?.type === "Rect" && detection.data) {
        rects.push(detection.data);
      }
    }
  }
  return rects;
}
function findParentRect(perception, contentType, parentUuid) {
  return collectRectsByContentType(perception, contentType).find((rect) => rect.uuid === parentUuid) || null;
}
function layerDetectionData(layer, detectionType) {
  if (!Array.isArray(layer.detections)) {
    return [];
  }
  return layer.detections.filter((detection) => detection?.type === detectionType).map((detection) => detection.data);
}
function drawLayerDetections(layer, detectionType, drawDetection) {
  for (const data of layerDetectionData(layer, detectionType)) {
    drawDetection(data);
  }
}
function drawConfiguredLayer(layer, renderer) {
  if (!renderer.enabled || layer.contentType !== renderer.contentType) {
    return;
  }
  drawLayerDetections(layer, renderer.detectionType, renderer.draw);
}
function drawLayers(ctx, perception, mapper, renderOptions, now) {
  const renderers = [
    {
      enabled: renderOptions.trackTraces,
      contentType: "trackTrace",
      detectionType: "TrackTrace",
      draw: (data) => drawTrackTrace(ctx, data, mapper, renderOptions.colors)
    },
    {
      enabled: renderOptions.objects,
      contentType: "genericObject",
      detectionType: "Rect",
      draw: (data) => drawObjectBox(ctx, data, mapper, renderOptions.colors.objects)
    },
    {
      enabled: renderOptions.faces,
      contentType: "humanFace",
      detectionType: "Rect",
      draw: (data) => drawFace(ctx, data, mapper, renderOptions.colors)
    },
    {
      enabled: renderOptions.classification,
      contentType: "classification",
      detectionType: "Classification",
      draw: (data) => drawClassification(ctx, data, mapper.display, renderOptions.colors)
    },
    {
      enabled: renderOptions.personStatus,
      contentType: "personClassification",
      detectionType: "PersonClassification",
      draw: (data) => drawPersonClassification(ctx, data, mapper.display, now, renderOptions.colors)
    }
  ];
  for (const layer of perception.layers) {
    for (const renderer of renderers) {
      drawConfiguredLayer(layer, renderer);
    }
  }
}
function drawObjectBox(ctx, rect, mapper, color) {
  const box = mapper.rect(rect);
  if (box.width <= 0 || box.height <= 0) {
    return;
  }
  ctx.save();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.strokeRect(box.x, box.y, box.width, box.height);
  const label = rect.label || rect.text || confidenceLabel(rect.confidence);
  if (label) {
    drawTextChip(ctx, label, box.x, Math.max(2, box.y - 22), 13, color);
  }
  ctx.restore();
}
function drawFace(ctx, rect, mapper, colors) {
  const box = mapper.rect(rect);
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
  const radius = Math.abs(box.width) / 2;
  ctx.save();
  ctx.strokeStyle = colors.faces;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, Math.PI * 2);
  ctx.stroke();
  ctx.restore();
}
function drawClassification(ctx, classification, display, colors) {
  const candidates = Array.isArray(classification?.candidates) ? classification.candidates : [];
  if (candidates.length === 0) {
    return;
  }
  const fontSize = 14;
  const lineHeight = fontSize * 1.5;
  const padding = 10;
  const startX = display.x + padding;
  const startY = display.y + display.height - candidates.length * lineHeight - padding;
  candidates.forEach((candidate, index) => {
    const text = `#${index + 1}: ${candidate.text || candidate.classId} (${((candidate.confidence || 0) * 100).toFixed(1)}%)`;
    drawTextChip(ctx, text, startX, startY + index * lineHeight, fontSize, colors.classification);
  });
}
function drawPersonClassification(ctx, personClassification, display, now, colors) {
  if (now % 1e3 >= 800) {
    return;
  }
  const isPerson = Number(personClassification?.yesConfidence || 0) > Number(personClassification?.noConfidence || 0);
  const label = isPerson ? "PERSON" : "NON-PERSON";
  const color = isPerson ? colors.personStatus : colors.noContact;
  const fontSize = Math.max(28, Math.min(64, display.width / 12));
  ctx.save();
  ctx.font = `700 ${fontSize}px monospace`;
  const metrics = ctx.measureText(label);
  const x = display.x + (display.width - metrics.width) / 2;
  const y = display.y + display.height / 2;
  drawTextChip(ctx, label, x, y, fontSize, color);
  ctx.restore();
}
function drawTrackTrace(ctx, trace, mapper, colors) {
  const points = Array.isArray(trace?.points) ? trace.points : [];
  if (points.length < 2) {
    return;
  }
  const color = colors.trackTraces;
  const segments = points.length - 1;
  for (let i = 0; i < segments; i += 1) {
    const from = mapper.point(points[i].x, points[i].y);
    const to = mapper.point(points[i + 1].x, points[i + 1].y);
    const alpha = 0.35 + 0.65 * ((i + 1) / segments);
    drawArrow(ctx, from, to, hexToRgba(color, alpha), 4, 0);
  }
}
function drawGazeVectors(ctx, perception, mapper, colors) {
  for (const layer of perception.layers) {
    if (layer.contentType !== "eyeYawPitch") {
      continue;
    }
    for (const detection of layer.detections || []) {
      if (detection?.type !== "YawPitch") {
        continue;
      }
      const yp = detection.data;
      const parent = findParentRect(perception, "humanFace", yp.parentUuid);
      if (!parent || nearZero(yp.yaw) && nearZero(yp.pitch)) {
        continue;
      }
      const parentBox = mapper.rect(parent);
      const start = { x: parentBox.x + parentBox.width / 2, y: parentBox.y + parentBox.height / 2 };
      const end = gazeEndpoint(start, yp.yaw, yp.pitch, 120);
      drawArrow(ctx, start, end, colors.gaze, 3, 10);
    }
  }
}
function cameraContactClassifications(perception) {
  const classifications = [];
  for (const layer of perception.layers) {
    if (layer.contentType === "cameraContact") {
      classifications.push(...layerDetectionData(layer, "Classification"));
    }
  }
  return classifications;
}
function drawCameraContactMarker(ctx, classification, perception, mapper, colors) {
  const candidate = classification?.candidates?.[0];
  const face = findParentRect(perception, "humanFace", classification?.parentUuid);
  if (!candidate || !face) {
    return;
  }
  const box = mapper.rect(face);
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
  const hasContact = candidate.classId === 1;
  const radiusBase = Math.min(Math.abs(box.width), Math.abs(box.height)) * 0.5;
  const radius = hasContact ? clamp2(radiusBase * 0.65, 18, 80) : clamp2(radiusBase * 1.15, 28, 140);
  const color = hasContact ? colors.cameraContact : colors.noContact;
  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = hasContact ? 5 : 8;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, Math.PI * 2);
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(cx, cy, hasContact ? 5 : 7, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}
function drawCameraContactMarkers(ctx, perception, mapper, colors) {
  for (const classification of cameraContactClassifications(perception)) {
    drawCameraContactMarker(ctx, classification, perception, mapper, colors);
  }
}
function drawPerformance(ctx, perception, colors) {
  const lines = Array.isArray(perception.perfdata) ? perception.perfdata : [];
  let y = 10;
  for (const line of lines) {
    drawTextChip(ctx, String(line), 10, y, 16, colors.performance);
    y += 16;
  }
}
function drawTextChip(ctx, text, x, y, fontSize, color = DEFAULT_COLORS.text) {
  ctx.save();
  ctx.font = `${fontSize}px monospace`;
  ctx.textBaseline = "top";
  const paddingX = 4;
  const paddingY = 2;
  const metrics = ctx.measureText(text);
  const width = metrics.width + paddingX * 2;
  const height = fontSize + paddingY * 2;
  ctx.fillStyle = DEFAULT_COLORS.textBg;
  ctx.fillRect(x, y, width, height);
  ctx.fillStyle = color;
  ctx.fillText(text, x + paddingX, y + paddingY);
  ctx.restore();
}
function drawArrow(ctx, from, to, color, width = 3, headSize = 10) {
  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = width;
  ctx.lineCap = "round";
  ctx.beginPath();
  ctx.moveTo(from.x, from.y);
  ctx.lineTo(to.x, to.y);
  ctx.stroke();
  if (headSize > 0) {
    const angle = Math.atan2(to.y - from.y, to.x - from.x);
    ctx.beginPath();
    ctx.moveTo(to.x, to.y);
    ctx.lineTo(to.x - headSize * Math.cos(angle - Math.PI / 6), to.y - headSize * Math.sin(angle - Math.PI / 6));
    ctx.lineTo(to.x - headSize * Math.cos(angle + Math.PI / 6), to.y - headSize * Math.sin(angle + Math.PI / 6));
    ctx.closePath();
    ctx.fill();
  }
  ctx.restore();
}
function gazeEndpoint(start, yawDeg, pitchDeg, lengthPx) {
  const yaw = deg2rad(yawDeg);
  const pitch = deg2rad(pitchDeg);
  let dx = -Math.tan(yaw);
  let dy = -Math.tan(pitch);
  const norm = Math.hypot(dx, dy);
  if (norm <= 0) {
    return { ...start };
  }
  dx /= norm;
  dy /= norm;
  return { x: start.x + dx * lengthPx, y: start.y + dy * lengthPx };
}
function confidenceLabel(confidence) {
  return Number.isFinite(confidence) ? `${(confidence * 100).toFixed(1)}%` : "";
}
function positiveNumber(value) {
  return Number.isFinite(Number(value)) && Number(value) > 0 ? Number(value) : 0;
}
function nearZero(value) {
  return Number(value) < 0.1 && Number(value) > -0.1;
}
function deg2rad(value) {
  return Number(value || 0) * Math.PI / 180;
}
function clamp2(value, min, max) {
  return Math.min(max, Math.max(min, value));
}
function hexToRgba(hex, alpha) {
  const clean = hex.replace("#", "");
  const r = parseInt(clean.slice(0, 2), 16);
  const g = parseInt(clean.slice(2, 4), 16);
  const b = parseInt(clean.slice(4, 6), 16);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

// development/web/src/client-osd.js
var canvas = document.getElementById("clientOsdCanvas");
var video4 = document.getElementById("video");
var toggleButton = document.getElementById("clientOsdControlsBtn");
var controlsPanel = document.getElementById("clientOsdControlsPanel");
var latest = {
  perception: null
};
var controlIds = {
  objects: "clientOsdRenderObjects",
  faces: "clientOsdRenderFaces",
  gaze: "clientOsdRenderGaze",
  cameraContact: "clientOsdRenderCameraContact",
  trackTraces: "clientOsdRenderTrackTraces",
  classification: "clientOsdRenderClassification",
  personStatus: "clientOsdRenderPersonStatus"
};
var colorIds = {
  objects: "clientOsdColorObjects",
  faces: "clientOsdColorFaces",
  gaze: "clientOsdColorGaze",
  cameraContact: "clientOsdColorCameraContact",
  trackTraces: "clientOsdColorTrackTraces",
  classification: "clientOsdColorClassification",
  personStatus: "clientOsdColorPersonStatus"
};
if (canvas && video4) {
  window.addEventListener("metadata-message", (event) => {
    latest.perception = event.detail?.perception || null;
  });
  window.addEventListener("resize", () => resizeCanvasToDisplaySize(canvas));
  video4.addEventListener("loadedmetadata", () => resizeCanvasToDisplaySize(canvas));
  video4.addEventListener("resize", () => resizeCanvasToDisplaySize(canvas));
  requestAnimationFrame(renderFrame);
}
if (toggleButton && controlsPanel) {
  toggleButton.addEventListener("click", () => {
    const isOpen = toggleButton.getAttribute("aria-expanded") === "true";
    setControlsOpen(!isOpen);
  });
  document.addEventListener("click", (event) => {
    if (controlsPanel.hidden || controlsPanel.contains(event.target) || toggleButton.contains(event.target)) {
      return;
    }
    setControlsOpen(false);
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      setControlsOpen(false);
    }
  });
}
function renderFrame() {
  renderOsd(canvas, video4, latest.perception, readRenderOptions());
  requestAnimationFrame(renderFrame);
}
function readRenderOptions() {
  return {
    objects: isChecked(controlIds.objects),
    faces: isChecked(controlIds.faces),
    gaze: isChecked(controlIds.gaze),
    cameraContact: isChecked(controlIds.cameraContact),
    trackTraces: isChecked(controlIds.trackTraces),
    classification: isChecked(controlIds.classification),
    personStatus: isChecked(controlIds.personStatus),
    performance: false,
    colors: {
      objects: colorValue(colorIds.objects),
      faces: colorValue(colorIds.faces),
      gaze: colorValue(colorIds.gaze),
      cameraContact: colorValue(colorIds.cameraContact),
      trackTraces: colorValue(colorIds.trackTraces),
      classification: colorValue(colorIds.classification),
      personStatus: colorValue(colorIds.personStatus)
    }
  };
}
function isChecked(id) {
  const element = document.getElementById(id);
  return element ? element.checked : true;
}
function colorValue(id) {
  return document.getElementById(id)?.value;
}
function setControlsOpen(isOpen) {
  controlsPanel.hidden = !isOpen;
  toggleButton.setAttribute("aria-expanded", String(isOpen));
  toggleButton.setAttribute("aria-pressed", String(isOpen));
}

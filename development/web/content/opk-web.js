/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

// development/web/src/webrtc_client.js
var MAX_RTP_TIMESTAMP = 4294967295;
var MAX_FRAME_METADATA_ENTRIES = 64;
var FRAME_METADATA_MAX_AGE_MS = 2e3;
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
    this.onVideoFrame = config.onVideoFrame || (() => {
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
      frameWatchdogTimer: null,
      frameMetadata: /* @__PURE__ */ new Map()
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
      if (data.type === "frame-metadata") {
        const timestamp = data.rtp_timestamp;
        if (Number.isInteger(timestamp) && timestamp >= 0 && timestamp <= MAX_RTP_TIMESTAMP) {
          session.frameMetadata.set(timestamp, { message: data, receivedAt: this.now() });
          while (session.frameMetadata.size > MAX_FRAME_METADATA_ENTRIES) {
            session.frameMetadata.delete(session.frameMetadata.keys().next().value);
          }
        }
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
    if (typeof this.video.requestVideoFrameCallback === "function") {
      const watchFrames = () => {
        if (!this.isCurrent(session))
          return;
        session.frameCallbackHandle = this.video.requestVideoFrameCallback((_now, frame) => {
          if (!this.isCurrent(session))
            return;
          this.markFrameHeartbeat(session, "video frame");
          const timestamp = frame?.rtpTimestamp;
          const match = session.frameMetadata.get(timestamp);
          session.frameMetadata.delete(timestamp);
          this.onVideoFrame(match && this.now() - match.receivedAt < FRAME_METADATA_MAX_AGE_MS ? match.message : null);
          watchFrames();
        });
      };
      watchFrames();
    } else if (this.frameTimeoutMs > 0) {
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
    session.frameMetadata.clear();
    this.onVideoFrame(null);
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
function resolveWebRtcTimingConfig(opkConfig = {}) {
  const webrtcConfig = opkConfig.webrtc || {};
  return {
    frameTimeoutMs: webrtcConfig.frameTimeoutMs ?? opkConfig.webrtcFrameTimeoutMs ?? 3e4,
    reconnectDelayMs: webrtcConfig.reconnectDelayMs ?? 1e3,
    maxReconnectDelayMs: webrtcConfig.maxReconnectDelayMs ?? 15e3,
    backoffFactor: webrtcConfig.backoffFactor ?? 1.1
  };
}
function resolveWebRtcIceConfig(opkConfig = {}) {
  const webrtcConfig = opkConfig.webrtc || {};
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
function setStatusLine(text2) {
  console.log("setStatusLine: " + text2);
  const textEl = statusLineEl.querySelector(".status-line-text");
  if (textEl) {
    textEl.textContent = text2;
  } else {
    statusLineEl.textContent = text2;
  }
}
function appendLog(message, type = "info") {
  const div = document.createElement("div");
  div.className = "log-line" + (type === "error" ? " error" : "");
  const time = (/* @__PURE__ */ new Date()).toLocaleTimeString();
  div.innerHTML = `<span>[${time}]</span> <span class="log-tag">${type === "error" ? "ERR" : "LOG"}</span>`;
  div.append(String(message));
  logEl.appendChild(div);
  logEl.scrollTop = logEl.scrollHeight;
  console[type === "error" ? "error" : "log"]("[WebRTC UI]", message);
}
var WS_PROTO = location.protocol === "https:" ? "wss" : "ws";
var WS_HOST = location.hostname;
var WS_PORT = window.OPK_CONFIG && window.OPK_CONFIG.wsPort || (location.port || (location.protocol === "https:" ? 443 : 80));
var SIGNALING_URL = `${WS_PROTO}://${WS_HOST}:${WS_PORT}/ws`;
var WEBRTC_TIMING_CONFIG = resolveWebRtcTimingConfig(window.OPK_CONFIG || {});
var WEBRTC_ICE_CONFIG = resolveWebRtcIceConfig(window.OPK_CONFIG || {});
var client = createWebRtcClient({
  video,
  signalingUrl: SIGNALING_URL,
  ...WEBRTC_TIMING_CONFIG,
  ...WEBRTC_ICE_CONFIG,
  logger: appendLog,
  onStatus: setStatus,
  onStatusLine: setStatusLine,
  onVideoFrame: (message) => {
    window.dispatchEvent(new CustomEvent("video-frame-metadata", { detail: message }));
  },
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
  playBtn.setAttribute("aria-busy", String(busy));
};
var renderPlayPause = () => {
  if (!playBtn || !playIcon || !playText) return;
  const isPaused = !isPipelinePlaying;
  document.body.classList.toggle("is-feed-paused", isPaused);
  playIcon.dataset.icon = isPaused ? "play" : "pause";
  playText.textContent = isPaused ? "Resume" : "Pause";
  playBtn.setAttribute("aria-label", isPaused ? "Resume pipeline" : "Pause pipeline");
  setBusy(false);
};
var dispatchPauseState = () => {
  window.dispatchEvent(new CustomEvent("feed-pause-change", {
    detail: { paused: Boolean(window.OPK_FEED_PAUSED) }
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
    window.OPK_FEED_PAUSED = !isPipelinePlaying;
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
    muteIcon.dataset.icon = "volume-x";
    muteText.textContent = "Unmute";
    muteBtn.setAttribute("aria-label", "Unmute");
  } else {
    muteIcon.dataset.icon = "volume-2";
    muteText.textContent = "Mute";
    muteBtn.setAttribute("aria-label", "Mute");
  }
});
function enableAudioButton(enable) {
  if (muteBtn) {
    muteBtn.hidden = !enable;
  }
}

// development/web/src/microphone_client.js
var MicrophoneClient = class {
  constructor({
    getUserMedia,
    createPeer,
    createSocket,
    signalingUrl: signalingUrl2,
    iceServers,
    onStatus = () => {
    },
    timeoutMs = 15e3
  }) {
    Object.assign(this, {
      getUserMedia,
      createPeer,
      createSocket,
      signalingUrl: signalingUrl2,
      iceServers,
      onStatus,
      timeoutMs
    });
    this.session = null;
  }
  stop(message = "Microphone off") {
    const session = this.session;
    this.session = null;
    if (session) {
      clearTimeout(session.timer);
      session.stream?.getTracks().forEach((track) => track.stop());
      session.pc?.close();
      session.ws?.close();
    }
    this.onStatus(false, message);
  }
  async start() {
    if (this.session)
      return;
    const session = { localIce: [], remoteIce: [], offered: false, messages: Promise.resolve() };
    this.session = session;
    const current = () => this.session === session;
    const fail = (error) => {
      if (current())
        this.stop("Microphone: " + (error.message || String(error)));
    };
    this.onStatus(true, "Waiting for microphone permission\u2026");
    try {
      const stream = await this.getUserMedia({ audio: true, video: false });
      if (!current()) {
        stream.getTracks().forEach((track) => track.stop());
        return;
      }
      session.stream = stream;
      const tracks = stream.getAudioTracks();
      if (!tracks.length)
        throw new Error("No microphone audio track");
      this.onStatus(true, "Connecting microphone\u2026");
      const pc = session.pc = this.createPeer({ iceServers: this.iceServers });
      for (const track of tracks) {
        pc.addTransceiver(track, { direction: "sendonly", streams: [stream] });
        track.onended = () => fail(new Error("Microphone disconnected"));
      }
      const ws = session.ws = this.createSocket(this.signalingUrl);
      session.timer = setTimeout(() => fail(new Error("Connection timed out")), this.timeoutMs);
      const send = (message) => ws.send(JSON.stringify(message));
      pc.onicecandidate = (event) => {
        if (!current() || !event.candidate)
          return;
        const message = { type: "candidate", ice: event.candidate };
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
        if (pc.connectionState === "connected") {
          clearTimeout(session.timer);
          this.onStatus(true, "Microphone sharing");
        } else if (["failed", "closed", "disconnected"].includes(pc.connectionState)) {
          fail(new Error("Connection lost"));
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
          send({ type: "offer", sdp: pc.localDescription.sdp });
          session.offered = true;
          for (const message of session.localIce)
            send(message);
          session.localIce = [];
        } catch (error) {
          fail(error);
        }
      };
      ws.onmessage = (event) => {
        session.messages = session.messages.then(async () => {
          if (!current())
            return;
          const message = JSON.parse(event.data);
          if (message.type === "answer" && typeof message.sdp === "string") {
            await pc.setRemoteDescription({ type: "answer", sdp: message.sdp });
            for (const candidate of session.remoteIce) {
              if (!current())
                return;
              await pc.addIceCandidate(candidate);
            }
            session.remoteIce = [];
          } else if (message.type === "candidate" && message.ice) {
            if (pc.remoteDescription)
              await pc.addIceCandidate(message.ice);
            else
              session.remoteIce.push(message.ice);
          } else {
            throw new Error("Invalid signaling response");
          }
        }).catch(fail);
      };
      ws.onerror = () => fail(new Error("Signaling unavailable"));
      ws.onclose = () => fail(new Error("Signaling disconnected"));
    } catch (error) {
      fail(error);
    }
  }
};

// development/web/src/microphone.js
var button = document.getElementById("microphoneBtn");
var icon = document.getElementById("microphoneIcon");
var status = document.getElementById("microphoneStatus");
var microphoneAvailable = false;
var microphoneConfig = window.OPK_CONFIG || {};
var signalingUrl = new URL(location.href);
signalingUrl.protocol = location.protocol === "https:" ? "wss:" : "ws:";
signalingUrl.port = String(microphoneConfig.micWsPort ?? 8003);
signalingUrl.pathname = "/ws";
signalingUrl.search = "";
signalingUrl.hash = "";
var client2 = new MicrophoneClient({
  getUserMedia: (constraints) => navigator.mediaDevices.getUserMedia(constraints),
  createPeer: (options) => new RTCPeerConnection(options),
  createSocket: (url) => new WebSocket(url),
  signalingUrl: signalingUrl.href,
  ...resolveWebRtcIceConfig(microphoneConfig),
  onStatus: (active, message) => {
    button.setAttribute("aria-pressed", String(active));
    button.setAttribute("aria-label", active ? "Stop sharing microphone" : "Share microphone");
    button.title = message;
    icon.dataset.icon = active ? "mic" : "mic-off";
    status.textContent = message;
  }
});
function enableMicrophoneButton(enable) {
  microphoneAvailable = enable === true;
  button.style.display = microphoneAvailable ? "inline-flex" : "none";
  button.disabled = !microphoneAvailable;
  status.hidden = !microphoneAvailable;
  if (!microphoneAvailable)
    client2.stop();
}
button.addEventListener("click", () => {
  if (!microphoneAvailable)
    return;
  if (client2.session) {
    client2.stop();
  } else if (!navigator.mediaDevices?.getUserMedia) {
    status.textContent = "Microphone unavailable. Open this page through localhost.";
  } else {
    void client2.start();
  }
});
window.addEventListener("pagehide", () => client2.stop());

// development/web/src/models.js
var PREFERRED_MODEL_ORDER = [
  "Yolo26n320Int8",
  "UltraFaceRfb320Int8",
  "NitecResnet18Int8",
  "MobileGazeMobilenetV2Int8"
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
function readableText(value) {
  return String(value ?? "").trim();
}
function contentTypes(value) {
  return Array.isArray(value) ? value.map(readableText).filter(Boolean) : [];
}
function dependencyProviderTasks(model, models) {
  const required = new Set(contentTypes(model.requiredContentTypes));
  return [...new Set(models.filter((candidate) => candidate !== model && contentTypes(candidate.providedContentTypes).some((type) => required.has(type))).map((candidate) => readableText(candidate.task) || readableText(candidate.name)).filter(Boolean))];
}
function resolveModelPresentation(model) {
  const rawName = readableText(model.name) || "Unknown model";
  const displayName = readableText(model.displayName) || rawName;
  const task = readableText(model.task);
  const runtime = readableText(model.runtime);
  const primaryLabel = task || displayName;
  let secondaryLabel = "";
  if (task) {
    secondaryLabel = runtime ? `${displayName} (${runtime})` : displayName;
  } else if (runtime) {
    secondaryLabel = runtime;
  }
  const fullLabel = secondaryLabel ? `${primaryLabel} - ${secondaryLabel}` : primaryLabel;
  return { primaryLabel, secondaryLabel, fullLabel };
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
      name: model.name || "",
      displayName: model.displayName || "",
      task: model.task || "",
      runtime: model.runtime || "",
      providedContentTypes: contentTypes(model.providedContentTypes),
      requiredContentTypes: contentTypes(model.requiredContentTypes)
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
    const orderedModels = orderModels(models);
    orderedModels.forEach((model) => {
      const modelItem = this.createModelItem(model, orderedModels);
      this.container.appendChild(modelItem);
    });
  }
  createModelItem(model, models) {
    const item = document.createElement("div");
    item.className = "model-item";
    item.setAttribute("data-model-name", model.name || "");
    item.setAttribute("data-model-element-name", model.element_name || "");
    item.classList.toggle("model-active", Boolean(model.active));
    const presentation = resolveModelPresentation(model);
    const modelInfo = document.createElement("div");
    modelInfo.className = "model-info";
    const modelCopy = document.createElement("div");
    modelCopy.className = "model-copy";
    modelCopy.title = presentation.fullLabel;
    const modelTask = document.createElement("div");
    modelTask.className = "model-task wfr-txt-label";
    modelTask.textContent = presentation.primaryLabel;
    modelCopy.appendChild(modelTask);
    if (presentation.secondaryLabel) {
      const modelDetails = document.createElement("div");
      modelDetails.className = "model-details wfr-txt-caption";
      modelDetails.textContent = presentation.secondaryLabel;
      modelCopy.appendChild(modelDetails);
    }
    const requiredContentTypes = contentTypes(model.requiredContentTypes);
    modelInfo.appendChild(modelCopy);
    const modelActions = document.createElement("div");
    modelActions.className = "model-actions";
    if (requiredContentTypes.length > 0) {
      const providerTasks = dependencyProviderTasks(model, models);
      const accessibleProviders = providerTasks.length > 0 ? providerTasks.join(", ") : "No provider registered";
      const dependencyInfo = document.createElement("div");
      dependencyInfo.className = "model-dependency-info";
      dependencyInfo.setAttribute("tabindex", "0");
      dependencyInfo.setAttribute("aria-label", `Depends on: ${accessibleProviders}`);
      const dependencyIcon = document.createElement("span");
      dependencyIcon.className = "model-dependency-icon icon";
      dependencyIcon.setAttribute("data-icon", "info");
      dependencyIcon.setAttribute("aria-hidden", "true");
      const dependencyPopup = document.createElement("div");
      dependencyPopup.className = "model-dependency-popup wfr-txt-caption";
      dependencyPopup.setAttribute("role", "tooltip");
      const dependencyHeading = document.createElement("div");
      dependencyHeading.className = "model-dependency-heading";
      dependencyHeading.textContent = "Depends on:";
      dependencyPopup.appendChild(dependencyHeading);
      const dependencyList = document.createElement("ul");
      const items = providerTasks.length > 0 ? providerTasks : ["No provider registered"];
      for (const task of items) {
        const dependency = document.createElement("li");
        dependency.textContent = task;
        dependencyList.appendChild(dependency);
      }
      dependencyPopup.appendChild(dependencyList);
      dependencyInfo.append(dependencyIcon, dependencyPopup);
      modelActions.appendChild(dependencyInfo);
    }
    const toggleLabel = document.createElement("label");
    toggleLabel.className = "model-toggle-switch";
    toggleLabel.setAttribute("aria-label", `Toggle ${presentation.fullLabel}`);
    const toggle = document.createElement("input");
    toggle.type = "checkbox";
    toggle.setAttribute("role", "switch");
    toggle.checked = Boolean(model.active);
    const toggleTrack = document.createElement("span");
    toggleTrack.className = "model-toggle-track";
    toggleTrack.setAttribute("aria-hidden", "true");
    const toggleThumb = document.createElement("span");
    toggleThumb.className = "model-toggle-thumb";
    toggleTrack.appendChild(toggleThumb);
    toggleLabel.append(toggle, toggleTrack);
    modelActions.appendChild(toggleLabel);
    item.append(modelInfo, modelActions);
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
var CTRL_PORT = window.OPK_CONFIG && window.OPK_CONFIG.ctrlPort || (location.port || (location.protocol === "https:" ? 443 : 80));
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
    enableMicrophoneButton(data.pipeline_state.microphone);
    setPlayPause(data.pipeline_state.playing);
    modelsManager.render(data.models);
  };
  ctrl.onerror = () => {
    ;
  };
  ctrl.onclose = () => {
    enableMicrophoneButton(false);
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
var STORAGE_KEY = "opk-layout:output-panels:v1";
var panels = {
  inference: {
    button: document.getElementById("toggleInferenceOutput"),
    section: document.querySelector("[data-inference-output-section]"),
    label: "inference output"
  },
  metrics: {
    button: document.getElementById("togglePerformanceOutput"),
    section: document.querySelector("[data-performance-metrics-section]"),
    label: "performance metrics"
  },
  debug: {
    button: document.getElementById("toggleDebugOutput"),
    section: document.querySelector("[data-debug-log-section]"),
    label: "debug log"
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
function setPanelSectionVisibility(panel2, visible) {
  if (panel2.section) {
    panel2.section.hidden = !visible;
  }
}
function setPanelButtonState(panel2, visible) {
  if (!panel2.button) {
    return;
  }
  const icon2 = panel2.button.querySelector("[data-icon]");
  panel2.button.setAttribute("aria-pressed", visible ? "true" : "false");
  panel2.button.setAttribute("aria-label", (visible ? "Hide " : "Show ") + panel2.label);
  if (icon2) {
    icon2.dataset.icon = visible ? "eye" : "eye-off";
  }
}
function applyPanelState(key) {
  const panel2 = panels[key];
  const visible = visibility[key] !== false;
  setPanelSectionVisibility(panel2, visible);
  setPanelButtonState(panel2, visible);
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
var sidebarStorageKey = "opk-layout:sidebar-width:v1";
var dockStorageKey = "opk-layout:bottom-dock-height:v1";
var dockColumnStorageKey = "opk-layout:bottom-dock-columns:v1";
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
function setDockColumnWidths(widths, persist2 = false) {
  configureDockColumnHandles();
  if (!dockPanels || !visibleDockColumnKeys().length)
    return;
  const next = normalizeDockColumnWidths(widths);
  root.style.setProperty("--bottom-metrics-column", `${Math.round(next.metrics)}px`);
  root.style.setProperty("--bottom-inference-column", `${Math.round(next.inference)}px`);
  root.style.setProperty("--bottom-debug-column", `${Math.round(next.debug)}px`);
  if (persist2) {
    localStorage.setItem(dockColumnStorageKey, JSON.stringify({
      metrics: Math.round(next.metrics),
      inference: Math.round(next.inference),
      debug: Math.round(next.debug)
    }));
  }
}
function setEqualDockColumnWidths(persist2 = false) {
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
  if (persist2) {
    localStorage.setItem(dockColumnStorageKey, JSON.stringify({
      metrics: Math.round(next.metrics),
      inference: Math.round(next.inference),
      debug: Math.round(next.debug)
    }));
  }
}
function scheduleDockColumnFit(persist2 = false) {
  if (dockColumnFitFrame)
    cancelAnimationFrame(dockColumnFitFrame);
  dockColumnFitFrame = requestAnimationFrame(() => {
    dockColumnFitFrame = null;
    setDockColumnWidths(readDockColumnWidths(), persist2);
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
function setSidebarWidth(width, persist2 = false) {
  const { min, max } = sidebarLimits();
  const nextWidth = clamp(width, min, max);
  root.style.setProperty("--sidebar-width", `${nextWidth}px`);
  scheduleDockColumnFit(persist2);
  if (persist2) {
    localStorage.setItem(sidebarStorageKey, String(Math.round(nextWidth)));
  }
}
function setDockHeight(height, persist2 = false) {
  const { min, max } = dockLimits();
  const nextHeight = clamp(height, min, max);
  root.style.setProperty("--bottom-dock-height", `${nextHeight}px`);
  if (persist2) {
    localStorage.setItem(dockStorageKey, String(Math.round(nextHeight)));
  }
}
function dockTargetHeight() {
  const style = getComputedStyle(root);
  const expandedHeight = px(style.getPropertyValue("--bottom-dock-height")) || 244;
  const collapsedHeight = px(style.getPropertyValue("--bottom-dock-collapsed-height")) || 48;
  const outputsHidden = document.body.classList.contains("outputs-hidden");
  const outputsEmpty = document.body.classList.contains("output-panels-empty");
  if (outputsHidden)
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

// development/web/src/video-layout.js
var sidebarButton = document.getElementById("sidebarVisibilityBtn");
var outputsButton = document.getElementById("outputsVisibilityBtn");
var videoFeedButton = document.getElementById("toggleVideoFeedBtn");
var videoFeedIcon = document.getElementById("toggleVideoFeedIcon");
var sidebarVisible = localStorage.getItem("opk-layout:sidebar-visible:v1") !== "false";
var outputsVisible = localStorage.getItem("opk-video:outputs-visible:v1") !== "false";
var videoFeedHidden = localStorage.getItem("opk-video:feed-hidden:v1") === "true";
function notifyVideoLayoutChange() {
  const detail = { sidebarVisible, outputsVisible, videoFeedHidden };
  window.dispatchEvent(new CustomEvent("video-layout-change", { detail }));
  requestAnimationFrame(() => {
    window.dispatchEvent(new CustomEvent("video-layout-change", { detail }));
  });
}
function setSidebarVisible(visible) {
  sidebarVisible = visible;
  document.body.classList.toggle("sidebar-hidden", !sidebarVisible);
  localStorage.setItem("opk-layout:sidebar-visible:v1", sidebarVisible ? "true" : "false");
  sidebarButton?.setAttribute("aria-pressed", sidebarVisible ? "true" : "false");
  notifyVideoLayoutChange();
}
function setVideoFeedHidden(hidden) {
  videoFeedHidden = hidden;
  document.body.classList.toggle("video-feed-hidden", videoFeedHidden);
  localStorage.setItem("opk-video:feed-hidden:v1", videoFeedHidden ? "true" : "false");
  if (videoFeedButton) {
    videoFeedButton.setAttribute("aria-label", videoFeedHidden ? "Show video feed" : "Hide video feed");
    videoFeedButton.setAttribute("aria-pressed", videoFeedHidden ? "true" : "false");
    videoFeedButton.dataset.tooltip = videoFeedHidden ? "Show video feed" : "Hide video feed";
  }
  if (videoFeedIcon) {
    videoFeedIcon.dataset.icon = videoFeedHidden ? "eye" : "eye-off";
  }
  notifyVideoLayoutChange();
}
function setOutputsVisible(visible, { animate = true } = {}) {
  const update = () => {
    outputsVisible = visible;
    document.body.classList.toggle("outputs-hidden", !outputsVisible);
    localStorage.setItem("opk-video:outputs-visible:v1", outputsVisible ? "true" : "false");
    outputsButton?.setAttribute("aria-pressed", outputsVisible ? "true" : "false");
  };
  if (animate && window.animateBottomDockHeightChange) {
    window.animateBottomDockHeightChange(update);
  } else {
    update();
  }
  notifyVideoLayoutChange();
}
setSidebarVisible(sidebarVisible);
setVideoFeedHidden(videoFeedHidden);
setOutputsVisible(outputsVisible, { animate: false });
sidebarButton?.addEventListener("click", () => {
  setSidebarVisible(!sidebarVisible);
});
outputsButton?.addEventListener("click", () => {
  setOutputsVisible(!outputsVisible);
});
videoFeedButton?.addEventListener("click", () => {
  setVideoFeedHidden(!videoFeedHidden);
});

// development/web/src/copy-utils.js?v=icon-copy-buttons-20260608
async function writeClipboard(text2) {
  if (navigator.clipboard?.writeText) {
    try {
      await navigator.clipboard.writeText(text2);
      return;
    } catch {
    }
  }
  const buffer = document.createElement("textarea");
  buffer.value = text2;
  buffer.readOnly = true;
  buffer.style.position = "fixed";
  buffer.style.opacity = "0";
  document.body.appendChild(buffer);
  const activeElement = document.activeElement;
  let copied = false;
  try {
    buffer.focus();
    buffer.select();
    copied = document.execCommand("copy");
  } finally {
    buffer.remove();
    activeElement?.focus?.();
  }
  if (!copied) throw new Error("Clipboard write failed");
}
function setButtonIcon(button2, iconName) {
  const icon2 = button2?.querySelector("[data-icon]");
  if (!icon2)
    return;
  icon2.dataset.icon = iconName;
}
function setButtonFeedback(button2, state, label) {
  button2.dataset.copyState = state;
  button2.setAttribute("aria-label", label);
  button2.title = label;
  setButtonIcon(button2, state === "copied" ? "check" : "copy");
}
async function copyTextWithFeedback(button2, text2, emptyText = "Empty") {
  if (!button2) return;
  if (!String(text2 || "").trim()) return;
  const originalLabel = button2.getAttribute("aria-label") || button2.title || "Copy";
  if (button2.copyFeedbackTimer) {
    clearTimeout(button2.copyFeedbackTimer);
    button2.copyFeedbackTimer = null;
  }
  button2.dataset.copyState = "copying";
  try {
    await writeClipboard(text2);
    setButtonFeedback(button2, text2 ? "copied" : "empty", text2 ? "Copied" : emptyText);
  } catch (error) {
    console.debug("Clipboard copy failed", error);
    setButtonFeedback(button2, "failed", "Copy failed");
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

// development/web/src/performance-low-pass.js
var DISPLAY_INTERVAL_MS = 1e3;
var SMOOTHING_WEIGHT = 0.5;
var METRIC_VALUE = /^(\d+(?:\.\d+)?)(ms| FPS)$/;
function smoothValue(value, previous) {
  const currentMatch = METRIC_VALUE.exec(value);
  const previousMatch = METRIC_VALUE.exec(previous || "");
  if (!currentMatch || !previousMatch || currentMatch[2] !== previousMatch[2]) return value;
  const current = Number(currentMatch[1]);
  const old = Number(previousMatch[1]);
  const decimals = currentMatch[1].split(".")[1]?.length || 0;
  if (!Number.isFinite(current) || !Number.isFinite(old) || decimals > 10) return value;
  return `${(old * (1 - SMOOTHING_WEIGHT) + current * SMOOTHING_WEIGHT).toFixed(decimals)}${currentMatch[2]}`;
}
function createPerformanceLowPass() {
  let lastDisplayAt = -Infinity;
  let previousRows = /* @__PURE__ */ new Map();
  return (rows, now = performance.now()) => {
    if (!rows.length) {
      lastDisplayAt = -Infinity;
      previousRows.clear();
      return [];
    }
    if (now - lastDisplayAt < DISPLAY_INTERVAL_MS) return null;
    const filtered = rows.map((row) => {
      const previous = previousRows.get(row.stage);
      return {
        ...row,
        current: smoothValue(row.current, previous?.current),
        p95: smoothValue(row.p95, previous?.p95)
      };
    });
    previousRows = new Map(filtered.map((row) => [row.stage, row]));
    lastDisplayAt = now;
    return filtered;
  };
}

// development/web/src/performance-metrics.js
var body = document.getElementById("performanceMetricsBody");
var copyButton = document.getElementById("copyPerformanceMetricsBtn");
var MAX_METRIC_LINE_LENGTH = 240;
var currentRows = [];
var filterMetrics = createPerformanceLowPass();
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
function parsePipelineFps(text2) {
  if (!text2.startsWith("Pipeline")) {
    return null;
  }
  const rest = text2.slice("Pipeline".length).trim();
  const valueText = (rest.startsWith(":") ? rest.slice(1) : rest).trim();
  if (!valueText.endsWith(" FPS")) {
    return null;
  }
  const fps = valueText.slice(0, -" FPS".length).trim();
  return decimalText(fps) ? { stage: "Pipeline", current: `${fps} FPS`, p95: "" } : null;
}
function parseP95Text(text2) {
  if (!text2) {
    return "";
  }
  if (!text2.startsWith("(p95:") || !text2.endsWith(")")) {
    return "";
  }
  return parseMillisToken(text2.slice(5, -1).trim());
}
function parseTimedMetric(text2) {
  const separator = text2.indexOf(":");
  if (separator <= 0) {
    return null;
  }
  const stage = text2.slice(0, separator).trim();
  const rest = text2.slice(separator + 1).trim();
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
  const text2 = String(line || "").slice(0, MAX_METRIC_LINE_LENGTH).replaceAll("\u2550", "").trim();
  if (!text2) return null;
  return parsePipelineFps(text2) || parseTimedMetric(text2);
}
function createCell(text2, className) {
  const cell = document.createElement("td");
  if (className) {
    cell.className = className;
  }
  cell.textContent = String(text2 ?? "");
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
  const rows = filterMetrics(lines.map(parseMetricLine).filter(Boolean));
  if (rows !== null) renderRows(rows);
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
function layerTitle(layer2) {
  return layer2.model || layer2.contentType || layer2.engine || "Layer";
}
function createNode(tag, className, text2) {
  const node2 = document.createElement(tag);
  if (className) {
    node2.className = className;
  }
  if (text2 !== void 0) {
    node2.textContent = String(text2);
  }
  return node2;
}
function renderEmpty(message) {
  return createNode("div", "inference-output-empty wfr-txt-empty-state", message);
}
function renderDetection(row) {
  const item = createNode("div", "inference-detection");
  const main = createNode("div", "inference-detection-main");
  main.appendChild(createNode("span", "inference-detection-title wfr-txt-label--sm", row.title));
  if (row.meta) {
    main.appendChild(createNode("span", "inference-detection-meta wfr-txt-caption", row.meta));
  }
  item.appendChild(main);
  item.appendChild(createNode("div", "inference-detection-detail wfr-txt-caption", row.detail));
  return item;
}
function renderLayer(layer2) {
  const detections = Array.isArray(layer2.detections) ? layer2.detections : [];
  const rows = detections.map(detectionSummary);
  const hiddenCount = Math.max(0, (layer2.count || 0) - rows.length);
  const container = createNode("div", "inference-layer");
  const header = createNode("div", "inference-layer-header");
  header.appendChild(createNode("span", "inference-layer-title wfr-txt-label", layerTitle(layer2)));
  header.appendChild(createNode("span", "inference-layer-count wfr-txt-caption", layer2.count || 0));
  container.appendChild(header);
  container.appendChild(createNode("div", "inference-layer-kind wfr-txt-caption", layer2.contentType || layer2.labelFamily || "output"));
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
  if (window.OPK_FEED_PAUSED) {
    currentLayers = [];
    updateCopyButtonState2();
    body2.replaceChildren(renderEmpty("Inference paused."));
    return;
  }
  const layers = Array.isArray(output?.layers) ? output.layers.filter((layer2) => layer2.model || layer2.contentType || layer2.engine) : [];
  currentLayers = layers;
  updateCopyButtonState2();
  if (!layers.length) {
    body2.replaceChildren(renderEmpty("No inference output yet, enable a model to see inference here."));
    return;
  }
  body2.replaceChildren(...layers.map(renderLayer));
}
function copyableLayers() {
  return currentLayers.filter((layer2) => {
    const detections = Array.isArray(layer2.detections) ? layer2.detections : [];
    return detections.length > 0 || Number(layer2.count || 0) > 0;
  });
}
function updateCopyButtonState2() {
  setCopyButtonAvailable(copyButton2, copyableLayers().length > 0);
}
function getInferenceText() {
  const layers = copyableLayers();
  if (!layers.length) return "";
  return layers.map((layer2) => {
    const detections = Array.isArray(layer2.detections) ? layer2.detections : [];
    const rows = detections.map(detectionSummary);
    const lines = [
      `${layerTitle(layer2)} (${layer2.contentType || layer2.labelFamily || "output"}): ${layer2.count || 0}`,
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
function getLogText() {
  if (!logEl2) return "";
  return Array.from(logEl2.querySelectorAll(".log-line")).map((line) => line.textContent.trim()).filter(Boolean).join("\n");
}
async function copyLog() {
  if (!copyButton3) return;
  const logText = getLogText();
  await copyTextWithFeedback(copyButton3, logText, "Copy");
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

// development/web/.opk-web-build/flatbuffers/mjs/constants.js
var SIZEOF_SHORT = 2;
var SIZEOF_INT = 4;
var FILE_IDENTIFIER_LENGTH = 4;
var SIZE_PREFIX_LENGTH = 4;

// development/web/.opk-web-build/flatbuffers/mjs/utils.js
var int32 = new Int32Array(2);
var float32 = new Float32Array(int32.buffer);
var float64 = new Float64Array(int32.buffer);
var isLittleEndian = new Uint16Array(new Uint8Array([1, 0]).buffer)[0] === 1;

// development/web/.opk-web-build/flatbuffers/mjs/encoding.js
var Encoding;
(function(Encoding2) {
  Encoding2[Encoding2["UTF8_BYTES"] = 1] = "UTF8_BYTES";
  Encoding2[Encoding2["UTF16_STRING"] = 2] = "UTF16_STRING";
})(Encoding || (Encoding = {}));

// development/web/.opk-web-build/flatbuffers/mjs/byte-buffer.js
var ByteBuffer = class _ByteBuffer {
  /**
   * Create a new ByteBuffer with a given array of bytes (`Uint8Array`)
   */
  constructor(bytes_) {
    this.bytes_ = bytes_;
    this.position_ = 0;
    this.text_decoder_ = new TextDecoder();
  }
  /**
   * Create and allocate a new ByteBuffer with a given size.
   */
  static allocate(byte_size) {
    return new _ByteBuffer(new Uint8Array(byte_size));
  }
  clear() {
    this.position_ = 0;
  }
  /**
   * Get the underlying `Uint8Array`.
   */
  bytes() {
    return this.bytes_;
  }
  /**
   * Get the buffer's position.
   */
  position() {
    return this.position_;
  }
  /**
   * Set the buffer's position.
   */
  setPosition(position) {
    this.position_ = position;
  }
  /**
   * Get the buffer's capacity.
   */
  capacity() {
    return this.bytes_.length;
  }
  readInt8(offset) {
    return this.readUint8(offset) << 24 >> 24;
  }
  readUint8(offset) {
    return this.bytes_[offset];
  }
  readInt16(offset) {
    return this.readUint16(offset) << 16 >> 16;
  }
  readUint16(offset) {
    return this.bytes_[offset] | this.bytes_[offset + 1] << 8;
  }
  readInt32(offset) {
    return this.bytes_[offset] | this.bytes_[offset + 1] << 8 | this.bytes_[offset + 2] << 16 | this.bytes_[offset + 3] << 24;
  }
  readUint32(offset) {
    return this.readInt32(offset) >>> 0;
  }
  readInt64(offset) {
    return BigInt.asIntN(64, BigInt(this.readUint32(offset)) + (BigInt(this.readUint32(offset + 4)) << BigInt(32)));
  }
  readUint64(offset) {
    return BigInt.asUintN(64, BigInt(this.readUint32(offset)) + (BigInt(this.readUint32(offset + 4)) << BigInt(32)));
  }
  readFloat32(offset) {
    int32[0] = this.readInt32(offset);
    return float32[0];
  }
  readFloat64(offset) {
    int32[isLittleEndian ? 0 : 1] = this.readInt32(offset);
    int32[isLittleEndian ? 1 : 0] = this.readInt32(offset + 4);
    return float64[0];
  }
  writeInt8(offset, value) {
    this.bytes_[offset] = value;
  }
  writeUint8(offset, value) {
    this.bytes_[offset] = value;
  }
  writeInt16(offset, value) {
    this.bytes_[offset] = value;
    this.bytes_[offset + 1] = value >> 8;
  }
  writeUint16(offset, value) {
    this.bytes_[offset] = value;
    this.bytes_[offset + 1] = value >> 8;
  }
  writeInt32(offset, value) {
    this.bytes_[offset] = value;
    this.bytes_[offset + 1] = value >> 8;
    this.bytes_[offset + 2] = value >> 16;
    this.bytes_[offset + 3] = value >> 24;
  }
  writeUint32(offset, value) {
    this.bytes_[offset] = value;
    this.bytes_[offset + 1] = value >> 8;
    this.bytes_[offset + 2] = value >> 16;
    this.bytes_[offset + 3] = value >> 24;
  }
  writeInt64(offset, value) {
    this.writeInt32(offset, Number(BigInt.asIntN(32, value)));
    this.writeInt32(offset + 4, Number(BigInt.asIntN(32, value >> BigInt(32))));
  }
  writeUint64(offset, value) {
    this.writeUint32(offset, Number(BigInt.asUintN(32, value)));
    this.writeUint32(offset + 4, Number(BigInt.asUintN(32, value >> BigInt(32))));
  }
  writeFloat32(offset, value) {
    float32[0] = value;
    this.writeInt32(offset, int32[0]);
  }
  writeFloat64(offset, value) {
    float64[0] = value;
    this.writeInt32(offset, int32[isLittleEndian ? 0 : 1]);
    this.writeInt32(offset + 4, int32[isLittleEndian ? 1 : 0]);
  }
  /**
   * Return the file identifier.   Behavior is undefined for FlatBuffers whose
   * schema does not include a file_identifier (likely points at padding or the
   * start of a the root vtable).
   */
  getBufferIdentifier() {
    if (this.bytes_.length < this.position_ + SIZEOF_INT + FILE_IDENTIFIER_LENGTH) {
      throw new Error("FlatBuffers: ByteBuffer is too short to contain an identifier.");
    }
    let result = "";
    for (let i = 0; i < FILE_IDENTIFIER_LENGTH; i++) {
      result += String.fromCharCode(this.readInt8(this.position_ + SIZEOF_INT + i));
    }
    return result;
  }
  /**
   * Look up a field in the vtable, return an offset into the object, or 0 if the
   * field is not present.
   */
  __offset(bb_pos, vtable_offset) {
    const vtable = bb_pos - this.readInt32(bb_pos);
    return vtable_offset < this.readInt16(vtable) ? this.readInt16(vtable + vtable_offset) : 0;
  }
  /**
   * Initialize any Table-derived type to point to the union at the given offset.
   */
  __union(t, offset) {
    t.bb_pos = offset + this.readInt32(offset);
    t.bb = this;
    return t;
  }
  /**
   * Create a JavaScript string from UTF-8 data stored inside the FlatBuffer.
   * This allocates a new string and converts to wide chars upon each access.
   *
   * To avoid the conversion to string, pass Encoding.UTF8_BYTES as the
   * "optionalEncoding" argument. This is useful for avoiding conversion when
   * the data will just be packaged back up in another FlatBuffer later on.
   *
   * @param offset
   * @param opt_encoding Defaults to UTF16_STRING
   */
  __string(offset, opt_encoding) {
    offset += this.readInt32(offset);
    const length = this.readInt32(offset);
    offset += SIZEOF_INT;
    const utf8bytes = this.bytes_.subarray(offset, offset + length);
    if (opt_encoding === Encoding.UTF8_BYTES)
      return utf8bytes;
    else
      return this.text_decoder_.decode(utf8bytes);
  }
  /**
   * Handle unions that can contain string as its member, if a Table-derived type then initialize it,
   * if a string then return a new one
   *
   * WARNING: strings are immutable in JS so we can't change the string that the user gave us, this
   * makes the behaviour of __union_with_string different compared to __union
   */
  __union_with_string(o, offset) {
    if (typeof o === "string") {
      return this.__string(offset);
    }
    return this.__union(o, offset);
  }
  /**
   * Retrieve the relative offset stored at "offset"
   */
  __indirect(offset) {
    return offset + this.readInt32(offset);
  }
  /**
   * Get the start of data of a vector whose offset is stored at "offset" in this object.
   */
  __vector(offset) {
    return offset + this.readInt32(offset) + SIZEOF_INT;
  }
  /**
   * Get the length of a vector whose offset is stored at "offset" in this object.
   */
  __vector_len(offset) {
    return this.readInt32(offset + this.readInt32(offset));
  }
  __has_identifier(ident) {
    if (ident.length != FILE_IDENTIFIER_LENGTH) {
      throw new Error("FlatBuffers: file identifier must be length " + FILE_IDENTIFIER_LENGTH);
    }
    for (let i = 0; i < FILE_IDENTIFIER_LENGTH; i++) {
      if (ident.charCodeAt(i) != this.readInt8(this.position() + SIZEOF_INT + i)) {
        return false;
      }
    }
    return true;
  }
  /**
   * A helper function for generating list for obj api
   */
  createScalarList(listAccessor, listLength) {
    const ret = [];
    for (let i = 0; i < listLength; ++i) {
      const val = listAccessor(i);
      if (val !== null) {
        ret.push(val);
      }
    }
    return ret;
  }
  /**
   * A helper function for generating list for obj api
   * @param listAccessor function that accepts an index and return data at that index
   * @param listLength listLength
   * @param res result list
   */
  createObjList(listAccessor, listLength) {
    const ret = [];
    for (let i = 0; i < listLength; ++i) {
      const val = listAccessor(i);
      if (val !== null) {
        ret.push(val.unpack());
      }
    }
    return ret;
  }
};

// development/web/.opk-web-build/flatbuffers/mjs/builder.js
var Builder = class _Builder {
  /**
   * Create a FlatBufferBuilder.
   */
  constructor(opt_initial_size) {
    this.minalign = 1;
    this.vtable = null;
    this.vtable_in_use = 0;
    this.isNested = false;
    this.object_start = 0;
    this.vtables = [];
    this.vector_num_elems = 0;
    this.force_defaults = false;
    this.string_maps = null;
    this.text_encoder = new TextEncoder();
    let initial_size;
    if (!opt_initial_size) {
      initial_size = 1024;
    } else {
      initial_size = opt_initial_size;
    }
    this.bb = ByteBuffer.allocate(initial_size);
    this.space = initial_size;
  }
  clear() {
    this.bb.clear();
    this.space = this.bb.capacity();
    this.minalign = 1;
    this.vtable = null;
    this.vtable_in_use = 0;
    this.isNested = false;
    this.object_start = 0;
    this.vtables = [];
    this.vector_num_elems = 0;
    this.force_defaults = false;
    this.string_maps = null;
  }
  /**
   * In order to save space, fields that are set to their default value
   * don't get serialized into the buffer. Forcing defaults provides a
   * way to manually disable this optimization.
   *
   * @param forceDefaults true always serializes default values
   */
  forceDefaults(forceDefaults) {
    this.force_defaults = forceDefaults;
  }
  /**
   * Get the ByteBuffer representing the FlatBuffer. Only call this after you've
   * called finish(). The actual data starts at the ByteBuffer's current position,
   * not necessarily at 0.
   */
  dataBuffer() {
    return this.bb;
  }
  /**
   * Get the bytes representing the FlatBuffer. Only call this after you've
   * called finish().
   */
  asUint8Array() {
    return this.bb.bytes().subarray(this.bb.position(), this.bb.position() + this.offset());
  }
  /**
   * Prepare to write an element of `size` after `additional_bytes` have been
   * written, e.g. if you write a string, you need to align such the int length
   * field is aligned to 4 bytes, and the string data follows it directly. If all
   * you need to do is alignment, `additional_bytes` will be 0.
   *
   * @param size This is the of the new element to write
   * @param additional_bytes The padding size
   */
  prep(size, additional_bytes) {
    if (size > this.minalign) {
      this.minalign = size;
    }
    const align_size = ~(this.bb.capacity() - this.space + additional_bytes) + 1 & size - 1;
    while (this.space < align_size + size + additional_bytes) {
      const old_buf_size = this.bb.capacity();
      this.bb = _Builder.growByteBuffer(this.bb);
      this.space += this.bb.capacity() - old_buf_size;
    }
    this.pad(align_size);
  }
  pad(byte_size) {
    for (let i = 0; i < byte_size; i++) {
      this.bb.writeInt8(--this.space, 0);
    }
  }
  writeInt8(value) {
    this.bb.writeInt8(this.space -= 1, value);
  }
  writeInt16(value) {
    this.bb.writeInt16(this.space -= 2, value);
  }
  writeInt32(value) {
    this.bb.writeInt32(this.space -= 4, value);
  }
  writeInt64(value) {
    this.bb.writeInt64(this.space -= 8, value);
  }
  writeFloat32(value) {
    this.bb.writeFloat32(this.space -= 4, value);
  }
  writeFloat64(value) {
    this.bb.writeFloat64(this.space -= 8, value);
  }
  /**
   * Add an `int8` to the buffer, properly aligned, and grows the buffer (if necessary).
   * @param value The `int8` to add the buffer.
   */
  addInt8(value) {
    this.prep(1, 0);
    this.writeInt8(value);
  }
  /**
   * Add an `int16` to the buffer, properly aligned, and grows the buffer (if necessary).
   * @param value The `int16` to add the buffer.
   */
  addInt16(value) {
    this.prep(2, 0);
    this.writeInt16(value);
  }
  /**
   * Add an `int32` to the buffer, properly aligned, and grows the buffer (if necessary).
   * @param value The `int32` to add the buffer.
   */
  addInt32(value) {
    this.prep(4, 0);
    this.writeInt32(value);
  }
  /**
   * Add an `int64` to the buffer, properly aligned, and grows the buffer (if necessary).
   * @param value The `int64` to add the buffer.
   */
  addInt64(value) {
    this.prep(8, 0);
    this.writeInt64(value);
  }
  /**
   * Add a `float32` to the buffer, properly aligned, and grows the buffer (if necessary).
   * @param value The `float32` to add the buffer.
   */
  addFloat32(value) {
    this.prep(4, 0);
    this.writeFloat32(value);
  }
  /**
   * Add a `float64` to the buffer, properly aligned, and grows the buffer (if necessary).
   * @param value The `float64` to add the buffer.
   */
  addFloat64(value) {
    this.prep(8, 0);
    this.writeFloat64(value);
  }
  addFieldInt8(voffset, value, defaultValue) {
    if (this.force_defaults || value != defaultValue) {
      this.addInt8(value);
      this.slot(voffset);
    }
  }
  addFieldInt16(voffset, value, defaultValue) {
    if (this.force_defaults || value != defaultValue) {
      this.addInt16(value);
      this.slot(voffset);
    }
  }
  addFieldInt32(voffset, value, defaultValue) {
    if (this.force_defaults || value != defaultValue) {
      this.addInt32(value);
      this.slot(voffset);
    }
  }
  addFieldInt64(voffset, value, defaultValue) {
    if (this.force_defaults || value !== defaultValue) {
      this.addInt64(value);
      this.slot(voffset);
    }
  }
  addFieldFloat32(voffset, value, defaultValue) {
    if (this.force_defaults || value != defaultValue) {
      this.addFloat32(value);
      this.slot(voffset);
    }
  }
  addFieldFloat64(voffset, value, defaultValue) {
    if (this.force_defaults || value != defaultValue) {
      this.addFloat64(value);
      this.slot(voffset);
    }
  }
  addFieldOffset(voffset, value, defaultValue) {
    if (this.force_defaults || value != defaultValue) {
      this.addOffset(value);
      this.slot(voffset);
    }
  }
  /**
   * Structs are stored inline, so nothing additional is being added. `d` is always 0.
   */
  addFieldStruct(voffset, value, defaultValue) {
    if (value != defaultValue) {
      this.nested(value);
      this.slot(voffset);
    }
  }
  /**
   * Structures are always stored inline, they need to be created right
   * where they're used.  You'll get this assertion failure if you
   * created it elsewhere.
   */
  nested(obj) {
    if (obj != this.offset()) {
      throw new TypeError("FlatBuffers: struct must be serialized inline.");
    }
  }
  /**
   * Should not be creating any other object, string or vector
   * while an object is being constructed
   */
  notNested() {
    if (this.isNested) {
      throw new TypeError("FlatBuffers: object serialization must not be nested.");
    }
  }
  /**
   * Set the current vtable at `voffset` to the current location in the buffer.
   */
  slot(voffset) {
    if (this.vtable !== null)
      this.vtable[voffset] = this.offset();
  }
  /**
   * @returns Offset relative to the end of the buffer.
   */
  offset() {
    return this.bb.capacity() - this.space;
  }
  /**
   * Doubles the size of the backing ByteBuffer and copies the old data towards
   * the end of the new buffer (since we build the buffer backwards).
   *
   * @param bb The current buffer with the existing data
   * @returns A new byte buffer with the old data copied
   * to it. The data is located at the end of the buffer.
   *
   * uint8Array.set() formally takes {Array<number>|ArrayBufferView}, so to pass
   * it a uint8Array we need to suppress the type check:
   * @suppress {checkTypes}
   */
  static growByteBuffer(bb) {
    const old_buf_size = bb.capacity();
    if (old_buf_size & 3221225472) {
      throw new Error("FlatBuffers: cannot grow buffer beyond 2 gigabytes.");
    }
    const new_buf_size = old_buf_size << 1;
    const nbb = ByteBuffer.allocate(new_buf_size);
    nbb.setPosition(new_buf_size - old_buf_size);
    nbb.bytes().set(bb.bytes(), new_buf_size - old_buf_size);
    return nbb;
  }
  /**
   * Adds on offset, relative to where it will be written.
   *
   * @param offset The offset to add.
   */
  addOffset(offset) {
    this.prep(SIZEOF_INT, 0);
    this.writeInt32(this.offset() - offset + SIZEOF_INT);
  }
  /**
   * Start encoding a new object in the buffer.  Users will not usually need to
   * call this directly. The FlatBuffers compiler will generate helper methods
   * that call this method internally.
   */
  startObject(numfields) {
    this.notNested();
    if (this.vtable == null) {
      this.vtable = [];
    }
    this.vtable_in_use = numfields;
    for (let i = 0; i < numfields; i++) {
      this.vtable[i] = 0;
    }
    this.isNested = true;
    this.object_start = this.offset();
  }
  /**
   * Finish off writing the object that is under construction.
   *
   * @returns The offset to the object inside `dataBuffer`
   */
  endObject() {
    if (this.vtable == null || !this.isNested) {
      throw new Error("FlatBuffers: endObject called without startObject");
    }
    this.addInt32(0);
    const vtableloc = this.offset();
    let i = this.vtable_in_use - 1;
    for (; i >= 0 && this.vtable[i] == 0; i--) {
    }
    const trimmed_size = i + 1;
    for (; i >= 0; i--) {
      this.addInt16(this.vtable[i] != 0 ? vtableloc - this.vtable[i] : 0);
    }
    const standard_fields = 2;
    this.addInt16(vtableloc - this.object_start);
    const len = (trimmed_size + standard_fields) * SIZEOF_SHORT;
    this.addInt16(len);
    let existing_vtable = 0;
    const vt1 = this.space;
    outer_loop: for (i = 0; i < this.vtables.length; i++) {
      const vt2 = this.bb.capacity() - this.vtables[i];
      if (len == this.bb.readInt16(vt2)) {
        for (let j = SIZEOF_SHORT; j < len; j += SIZEOF_SHORT) {
          if (this.bb.readInt16(vt1 + j) != this.bb.readInt16(vt2 + j)) {
            continue outer_loop;
          }
        }
        existing_vtable = this.vtables[i];
        break;
      }
    }
    if (existing_vtable) {
      this.space = this.bb.capacity() - vtableloc;
      this.bb.writeInt32(this.space, existing_vtable - vtableloc);
    } else {
      this.vtables.push(this.offset());
      this.bb.writeInt32(this.bb.capacity() - vtableloc, this.offset() - vtableloc);
    }
    this.isNested = false;
    return vtableloc;
  }
  /**
   * Finalize a buffer, poiting to the given `root_table`.
   */
  finish(root_table, opt_file_identifier, opt_size_prefix) {
    const size_prefix = opt_size_prefix ? SIZE_PREFIX_LENGTH : 0;
    if (opt_file_identifier) {
      const file_identifier = opt_file_identifier;
      this.prep(this.minalign, SIZEOF_INT + FILE_IDENTIFIER_LENGTH + size_prefix);
      if (file_identifier.length != FILE_IDENTIFIER_LENGTH) {
        throw new TypeError("FlatBuffers: file identifier must be length " + FILE_IDENTIFIER_LENGTH);
      }
      for (let i = FILE_IDENTIFIER_LENGTH - 1; i >= 0; i--) {
        this.writeInt8(file_identifier.charCodeAt(i));
      }
    }
    this.prep(this.minalign, SIZEOF_INT + size_prefix);
    this.addOffset(root_table);
    if (size_prefix) {
      this.addInt32(this.bb.capacity() - this.space);
    }
    this.bb.setPosition(this.space);
  }
  /**
   * Finalize a size prefixed buffer, pointing to the given `root_table`.
   */
  finishSizePrefixed(root_table, opt_file_identifier) {
    this.finish(root_table, opt_file_identifier, true);
  }
  /**
   * This checks a required field has been set in a given table that has
   * just been constructed.
   */
  requiredField(table, field) {
    const table_start = this.bb.capacity() - table;
    const vtable_start = table_start - this.bb.readInt32(table_start);
    const ok = field < this.bb.readInt16(vtable_start) && this.bb.readInt16(vtable_start + field) != 0;
    if (!ok) {
      throw new TypeError("FlatBuffers: field " + field + " must be set");
    }
  }
  /**
   * Start a new array/vector of objects.  Users usually will not call
   * this directly. The FlatBuffers compiler will create a start/end
   * method for vector types in generated code.
   *
   * @param elem_size The size of each element in the array
   * @param num_elems The number of elements in the array
   * @param alignment The alignment of the array
   */
  startVector(elem_size, num_elems, alignment) {
    this.notNested();
    this.vector_num_elems = num_elems;
    this.prep(SIZEOF_INT, elem_size * num_elems);
    this.prep(alignment, elem_size * num_elems);
  }
  /**
   * Finish off the creation of an array and all its elements. The array must be
   * created with `startVector`.
   *
   * @returns The offset at which the newly created array
   * starts.
   */
  endVector() {
    this.writeInt32(this.vector_num_elems);
    return this.offset();
  }
  /**
   * Encode the string `s` in the buffer using UTF-8. If the string passed has
   * already been seen, we return the offset of the already written string
   *
   * @param s The string to encode
   * @return The offset in the buffer where the encoded string starts
   */
  createSharedString(s) {
    if (!s) {
      return 0;
    }
    if (!this.string_maps) {
      this.string_maps = /* @__PURE__ */ new Map();
    }
    if (this.string_maps.has(s)) {
      return this.string_maps.get(s);
    }
    const offset = this.createString(s);
    this.string_maps.set(s, offset);
    return offset;
  }
  /**
   * Encode the string `s` in the buffer using UTF-8. If a Uint8Array is passed
   * instead of a string, it is assumed to contain valid UTF-8 encoded data.
   *
   * @param s The string to encode
   * @return The offset in the buffer where the encoded string starts
   */
  createString(s) {
    if (s === null || s === void 0) {
      return 0;
    }
    let utf8;
    if (s instanceof Uint8Array) {
      utf8 = s;
    } else {
      utf8 = this.text_encoder.encode(s);
    }
    this.addInt8(0);
    this.startVector(1, utf8.length, 1);
    this.bb.setPosition(this.space -= utf8.length);
    this.bb.bytes().set(utf8, this.space);
    return this.endVector();
  }
  /**
   * Create a byte vector.
   *
   * @param v The bytes to add
   * @returns The offset in the buffer where the byte vector starts
   */
  createByteVector(v) {
    if (v === null || v === void 0) {
      return 0;
    }
    this.startVector(1, v.length, 1);
    this.bb.setPosition(this.space -= v.length);
    this.bb.bytes().set(v, this.space);
    return this.endVector();
  }
  /**
   * A helper function to pack an object
   *
   * @returns offset of obj
   */
  createObjectOffset(obj) {
    if (obj === null) {
      return 0;
    }
    if (typeof obj === "string") {
      return this.createString(obj);
    } else {
      return obj.pack(this);
    }
  }
  /**
   * A helper function to pack a list of object
   *
   * @returns list of offsets of each non null object
   */
  createObjectOffsetList(list2) {
    const ret = [];
    for (let i = 0; i < list2.length; ++i) {
      const val = list2[i];
      if (val !== null) {
        ret.push(this.createObjectOffset(val));
      } else {
        throw new TypeError("FlatBuffers: Argument for createObjectOffsetList cannot contain null.");
      }
    }
    return ret;
  }
  createStructOffsetList(list2, startFunc) {
    startFunc(this, list2.length);
    this.createObjectOffsetList(list2.slice().reverse());
    return this.endVector();
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/internalfb/wire-payload.js
var WirePayload = class _WirePayload {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsWirePayload(bb, obj) {
    return (obj || new _WirePayload()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsWirePayload(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _WirePayload()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  id() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  blob(index) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint8(this.bb.__vector(this.bb_pos + offset) + index) : 0;
  }
  blobLength() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  blobArray() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? new Uint8Array(this.bb.bytes().buffer, this.bb.bytes().byteOffset + this.bb.__vector(this.bb_pos + offset), this.bb.__vector_len(this.bb_pos + offset)) : null;
  }
  static startWirePayload(builder) {
    builder.startObject(2);
  }
  static addId(builder, id) {
    builder.addFieldInt64(0, id, BigInt("0"));
  }
  static addBlob(builder, blobOffset) {
    builder.addFieldOffset(1, blobOffset, 0);
  }
  static createBlobVector(builder, data) {
    builder.startVector(1, data.length, 1);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addInt8(data[i]);
    }
    return builder.endVector();
  }
  static startBlobVector(builder, numElems) {
    builder.startVector(1, numElems, 1);
  }
  static endWirePayload(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createWirePayload(builder, id, blobOffset) {
    _WirePayload.startWirePayload(builder);
    _WirePayload.addId(builder, id);
    _WirePayload.addBlob(builder, blobOffset);
    return _WirePayload.endWirePayload(builder);
  }
  unpack() {
    return new WirePayloadT(this.id(), this.bb.createScalarList(this.blob.bind(this), this.blobLength()));
  }
  unpackTo(_o) {
    _o.id = this.id();
    _o.blob = this.bb.createScalarList(this.blob.bind(this), this.blobLength());
  }
};
var WirePayloadT = class {
  constructor(id = BigInt("0"), blob = []) {
    this.id = id;
    this.blob = blob;
  }
  pack(builder) {
    const blob = WirePayload.createBlobVector(builder, this.blob);
    return WirePayload.createWirePayload(builder, this.id, blob);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/internalfb/wire-envelope.js
var WireEnvelope = class _WireEnvelope {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsWireEnvelope(bb, obj) {
    return (obj || new _WireEnvelope()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsWireEnvelope(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _WireEnvelope()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("FLWD");
  }
  payloads(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new WirePayload()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  payloadsLength() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  producerSdkName(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  producerSdkVersion(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  producerSchemaSetSha256(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  static startWireEnvelope(builder) {
    builder.startObject(4);
  }
  static addPayloads(builder, payloadsOffset) {
    builder.addFieldOffset(0, payloadsOffset, 0);
  }
  static createPayloadsVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startPayloadsVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static addProducerSdkName(builder, producerSdkNameOffset) {
    builder.addFieldOffset(1, producerSdkNameOffset, 0);
  }
  static addProducerSdkVersion(builder, producerSdkVersionOffset) {
    builder.addFieldOffset(2, producerSdkVersionOffset, 0);
  }
  static addProducerSchemaSetSha256(builder, producerSchemaSetSha256Offset) {
    builder.addFieldOffset(3, producerSchemaSetSha256Offset, 0);
  }
  static endWireEnvelope(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishWireEnvelopeBuffer(builder, offset) {
    builder.finish(offset, "FLWD");
  }
  static finishSizePrefixedWireEnvelopeBuffer(builder, offset) {
    builder.finish(offset, "FLWD", true);
  }
  static createWireEnvelope(builder, payloadsOffset, producerSdkNameOffset, producerSdkVersionOffset, producerSchemaSetSha256Offset) {
    _WireEnvelope.startWireEnvelope(builder);
    _WireEnvelope.addPayloads(builder, payloadsOffset);
    _WireEnvelope.addProducerSdkName(builder, producerSdkNameOffset);
    _WireEnvelope.addProducerSdkVersion(builder, producerSdkVersionOffset);
    _WireEnvelope.addProducerSchemaSetSha256(builder, producerSchemaSetSha256Offset);
    return _WireEnvelope.endWireEnvelope(builder);
  }
  unpack() {
    return new WireEnvelopeT(this.bb.createObjList(this.payloads.bind(this), this.payloadsLength()), this.producerSdkName(), this.producerSdkVersion(), this.producerSchemaSetSha256());
  }
  unpackTo(_o) {
    _o.payloads = this.bb.createObjList(this.payloads.bind(this), this.payloadsLength());
    _o.producerSdkName = this.producerSdkName();
    _o.producerSdkVersion = this.producerSdkVersion();
    _o.producerSchemaSetSha256 = this.producerSchemaSetSha256();
  }
};
var WireEnvelopeT = class {
  constructor(payloads = [], producerSdkName = null, producerSdkVersion = null, producerSchemaSetSha256 = null) {
    this.payloads = payloads;
    this.producerSdkName = producerSdkName;
    this.producerSdkVersion = producerSdkVersion;
    this.producerSchemaSetSha256 = producerSchemaSetSha256;
  }
  pack(builder) {
    const payloads = WireEnvelope.createPayloadsVector(builder, builder.createObjectOffsetList(this.payloads));
    const producerSdkName = this.producerSdkName !== null ? builder.createString(this.producerSdkName) : 0;
    const producerSdkVersion = this.producerSdkVersion !== null ? builder.createString(this.producerSdkVersion) : 0;
    const producerSchemaSetSha256 = this.producerSchemaSetSha256 !== null ? builder.createString(this.producerSchemaSetSha256) : 0;
    return WireEnvelope.createWireEnvelope(builder, payloads, producerSdkName, producerSdkVersion, producerSchemaSetSha256);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/bounding-box.js
var BoundingBox = class _BoundingBox {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsBoundingBox(bb, obj) {
    return (obj || new _BoundingBox()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsBoundingBox(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _BoundingBox()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  x() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  y() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  width() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  height() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  static startBoundingBox(builder) {
    builder.startObject(4);
  }
  static addX(builder, x) {
    builder.addFieldFloat32(0, x, 0);
  }
  static addY(builder, y) {
    builder.addFieldFloat32(1, y, 0);
  }
  static addWidth(builder, width) {
    builder.addFieldFloat32(2, width, 0);
  }
  static addHeight(builder, height) {
    builder.addFieldFloat32(3, height, 0);
  }
  static endBoundingBox(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createBoundingBox(builder, x, y, width, height) {
    _BoundingBox.startBoundingBox(builder);
    _BoundingBox.addX(builder, x);
    _BoundingBox.addY(builder, y);
    _BoundingBox.addWidth(builder, width);
    _BoundingBox.addHeight(builder, height);
    return _BoundingBox.endBoundingBox(builder);
  }
  unpack() {
    return new BoundingBoxT(this.x(), this.y(), this.width(), this.height());
  }
  unpackTo(_o) {
    _o.x = this.x();
    _o.y = this.y();
    _o.width = this.width();
    _o.height = this.height();
  }
};
var BoundingBoxT = class {
  constructor(x = 0, y = 0, width = 0, height = 0) {
    this.x = x;
    this.y = y;
    this.width = width;
    this.height = height;
  }
  pack(builder) {
    return BoundingBox.createBoundingBox(builder, this.x, this.y, this.width, this.height);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-meta.js
var ObjectMeta = class _ObjectMeta {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsObjectMeta(bb, obj) {
    return (obj || new _ObjectMeta()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsObjectMeta(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _ObjectMeta()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  id() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  parentId() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  creationTsNs() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  static startObjectMeta(builder) {
    builder.startObject(3);
  }
  static addId(builder, id) {
    builder.addFieldInt64(0, id, BigInt("0"));
  }
  static addParentId(builder, parentId) {
    builder.addFieldInt64(1, parentId, BigInt("0"));
  }
  static addCreationTsNs(builder, creationTsNs) {
    builder.addFieldInt64(2, creationTsNs, BigInt("0"));
  }
  static endObjectMeta(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createObjectMeta(builder, id, parentId, creationTsNs) {
    _ObjectMeta.startObjectMeta(builder);
    _ObjectMeta.addId(builder, id);
    _ObjectMeta.addParentId(builder, parentId);
    _ObjectMeta.addCreationTsNs(builder, creationTsNs);
    return _ObjectMeta.endObjectMeta(builder);
  }
  unpack() {
    return new ObjectMetaT(this.id(), this.parentId(), this.creationTsNs());
  }
  unpackTo(_o) {
    _o.id = this.id();
    _o.parentId = this.parentId();
    _o.creationTsNs = this.creationTsNs();
  }
};
var ObjectMetaT = class {
  constructor(id = BigInt("0"), parentId = BigInt("0"), creationTsNs = BigInt("0")) {
    this.id = id;
    this.parentId = parentId;
    this.creationTsNs = creationTsNs;
  }
  pack(builder) {
    return ObjectMeta.createObjectMeta(builder, this.id, this.parentId, this.creationTsNs);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detection.js
var BoxDetection = class _BoxDetection {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsBoxDetection(bb, obj) {
    return (obj || new _BoxDetection()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsBoxDetection(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _BoxDetection()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  box(obj) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? (obj || new BoundingBox()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  confidence() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  classId() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readInt32(this.bb_pos + offset) : -1;
  }
  text(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  static startBoxDetection(builder) {
    builder.startObject(5);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addBox(builder, boxOffset) {
    builder.addFieldOffset(1, boxOffset, 0);
  }
  static addConfidence(builder, confidence) {
    builder.addFieldFloat32(2, confidence, 0);
  }
  static addClassId(builder, classId) {
    builder.addFieldInt32(3, classId, -1);
  }
  static addText(builder, textOffset) {
    builder.addFieldOffset(4, textOffset, 0);
  }
  static endBoxDetection(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new BoxDetectionT(this.object() !== null ? this.object().unpack() : null, this.box() !== null ? this.box().unpack() : null, this.confidence(), this.classId(), this.text());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.box = this.box() !== null ? this.box().unpack() : null;
    _o.confidence = this.confidence();
    _o.classId = this.classId();
    _o.text = this.text();
  }
};
var BoxDetectionT = class {
  constructor(object = null, box = null, confidence = 0, classId = -1, text2 = null) {
    this.object = object;
    this.box = box;
    this.confidence = confidence;
    this.classId = classId;
    this.text = text2;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const box = this.box !== null ? this.box.pack(builder) : 0;
    const text2 = this.text !== null ? builder.createString(this.text) : 0;
    BoxDetection.startBoxDetection(builder);
    BoxDetection.addObject(builder, object);
    BoxDetection.addBox(builder, box);
    BoxDetection.addConfidence(builder, this.confidence);
    BoxDetection.addClassId(builder, this.classId);
    BoxDetection.addText(builder, text2);
    return BoxDetection.endBoxDetection(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/producer-info.js
var ProducerInfo = class _ProducerInfo {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsProducerInfo(bb, obj) {
    return (obj || new _ProducerInfo()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsProducerInfo(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _ProducerInfo()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  instanceId(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  component(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  implementation(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  static startProducerInfo(builder) {
    builder.startObject(3);
  }
  static addInstanceId(builder, instanceIdOffset) {
    builder.addFieldOffset(0, instanceIdOffset, 0);
  }
  static addComponent(builder, componentOffset) {
    builder.addFieldOffset(1, componentOffset, 0);
  }
  static addImplementation(builder, implementationOffset) {
    builder.addFieldOffset(2, implementationOffset, 0);
  }
  static endProducerInfo(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createProducerInfo(builder, instanceIdOffset, componentOffset, implementationOffset) {
    _ProducerInfo.startProducerInfo(builder);
    _ProducerInfo.addInstanceId(builder, instanceIdOffset);
    _ProducerInfo.addComponent(builder, componentOffset);
    _ProducerInfo.addImplementation(builder, implementationOffset);
    return _ProducerInfo.endProducerInfo(builder);
  }
  unpack() {
    return new ProducerInfoT(this.instanceId(), this.component(), this.implementation());
  }
  unpackTo(_o) {
    _o.instanceId = this.instanceId();
    _o.component = this.component();
    _o.implementation = this.implementation();
  }
};
var ProducerInfoT = class {
  constructor(instanceId = null, component = null, implementation = null) {
    this.instanceId = instanceId;
    this.component = component;
    this.implementation = implementation;
  }
  pack(builder) {
    const instanceId = this.instanceId !== null ? builder.createString(this.instanceId) : 0;
    const component = this.component !== null ? builder.createString(this.component) : 0;
    const implementation = this.implementation !== null ? builder.createString(this.implementation) : 0;
    return ProducerInfo.createProducerInfo(builder, instanceId, component, implementation);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/layer-info.js
var LayerInfo = class _LayerInfo {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsLayerInfo(bb, obj) {
    return (obj || new _LayerInfo()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsLayerInfo(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _LayerInfo()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  engine(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  model(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  tags(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  inferElementId(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  labelFamily(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  contentType(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  compositingMode(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 16);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  producer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 18);
    return offset ? (obj || new ProducerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  static startLayerInfo(builder) {
    builder.startObject(8);
  }
  static addEngine(builder, engineOffset) {
    builder.addFieldOffset(0, engineOffset, 0);
  }
  static addModel(builder, modelOffset) {
    builder.addFieldOffset(1, modelOffset, 0);
  }
  static addTags(builder, tagsOffset) {
    builder.addFieldOffset(2, tagsOffset, 0);
  }
  static addInferElementId(builder, inferElementIdOffset) {
    builder.addFieldOffset(3, inferElementIdOffset, 0);
  }
  static addLabelFamily(builder, labelFamilyOffset) {
    builder.addFieldOffset(4, labelFamilyOffset, 0);
  }
  static addContentType(builder, contentTypeOffset) {
    builder.addFieldOffset(5, contentTypeOffset, 0);
  }
  static addCompositingMode(builder, compositingModeOffset) {
    builder.addFieldOffset(6, compositingModeOffset, 0);
  }
  static addProducer(builder, producerOffset) {
    builder.addFieldOffset(7, producerOffset, 0);
  }
  static endLayerInfo(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new LayerInfoT(this.engine(), this.model(), this.tags(), this.inferElementId(), this.labelFamily(), this.contentType(), this.compositingMode(), this.producer() !== null ? this.producer().unpack() : null);
  }
  unpackTo(_o) {
    _o.engine = this.engine();
    _o.model = this.model();
    _o.tags = this.tags();
    _o.inferElementId = this.inferElementId();
    _o.labelFamily = this.labelFamily();
    _o.contentType = this.contentType();
    _o.compositingMode = this.compositingMode();
    _o.producer = this.producer() !== null ? this.producer().unpack() : null;
  }
};
var LayerInfoT = class {
  constructor(engine = null, model = null, tags = null, inferElementId = null, labelFamily = null, contentType = null, compositingMode = null, producer = null) {
    this.engine = engine;
    this.model = model;
    this.tags = tags;
    this.inferElementId = inferElementId;
    this.labelFamily = labelFamily;
    this.contentType = contentType;
    this.compositingMode = compositingMode;
    this.producer = producer;
  }
  pack(builder) {
    const engine = this.engine !== null ? builder.createString(this.engine) : 0;
    const model = this.model !== null ? builder.createString(this.model) : 0;
    const tags = this.tags !== null ? builder.createString(this.tags) : 0;
    const inferElementId = this.inferElementId !== null ? builder.createString(this.inferElementId) : 0;
    const labelFamily = this.labelFamily !== null ? builder.createString(this.labelFamily) : 0;
    const contentType = this.contentType !== null ? builder.createString(this.contentType) : 0;
    const compositingMode = this.compositingMode !== null ? builder.createString(this.compositingMode) : 0;
    const producer = this.producer !== null ? this.producer.pack(builder) : 0;
    LayerInfo.startLayerInfo(builder);
    LayerInfo.addEngine(builder, engine);
    LayerInfo.addModel(builder, model);
    LayerInfo.addTags(builder, tags);
    LayerInfo.addInferElementId(builder, inferElementId);
    LayerInfo.addLabelFamily(builder, labelFamily);
    LayerInfo.addContentType(builder, contentType);
    LayerInfo.addCompositingMode(builder, compositingMode);
    LayerInfo.addProducer(builder, producer);
    return LayerInfo.endLayerInfo(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detections.js
var BoxDetections = class _BoxDetections {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsBoxDetections(bb, obj) {
    return (obj || new _BoxDetections()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsBoxDetections(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _BoxDetections()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("BDET");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  detections(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new BoxDetection()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  detectionsLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startBoxDetections(builder) {
    builder.startObject(4);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addDetections(builder, detectionsOffset) {
    builder.addFieldOffset(3, detectionsOffset, 0);
  }
  static createDetectionsVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startDetectionsVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endBoxDetections(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishBoxDetectionsBuffer(builder, offset) {
    builder.finish(offset, "BDET");
  }
  static finishSizePrefixedBoxDetectionsBuffer(builder, offset) {
    builder.finish(offset, "BDET", true);
  }
  unpack() {
    return new BoxDetectionsT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.detections.bind(this), this.detectionsLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.detections = this.bb.createObjList(this.detections.bind(this), this.detectionsLength());
  }
};
var BoxDetectionsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, detections = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.detections = detections;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const detections = BoxDetections.createDetectionsVector(builder, builder.createObjectOffsetList(this.detections));
    BoxDetections.startBoxDetections(builder);
    BoxDetections.addSchemaMajor(builder, this.schemaMajor);
    BoxDetections.addSchemaMinor(builder, this.schemaMinor);
    BoxDetections.addLayer(builder, layer2);
    BoxDetections.addDetections(builder, detections);
    return BoxDetections.endBoxDetections(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification-candidate.js
var ClassificationCandidate = class _ClassificationCandidate {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsClassificationCandidate(bb, obj) {
    return (obj || new _ClassificationCandidate()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsClassificationCandidate(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _ClassificationCandidate()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  confidence() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  classId() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readInt32(this.bb_pos + offset) : -1;
  }
  text(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  x() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  y() {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  w() {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  h() {
    const offset = this.bb.__offset(this.bb_pos, 16);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  static startClassificationCandidate(builder) {
    builder.startObject(7);
  }
  static addConfidence(builder, confidence) {
    builder.addFieldFloat32(0, confidence, 0);
  }
  static addClassId(builder, classId) {
    builder.addFieldInt32(1, classId, -1);
  }
  static addText(builder, textOffset) {
    builder.addFieldOffset(2, textOffset, 0);
  }
  static addX(builder, x) {
    builder.addFieldFloat32(3, x, 0);
  }
  static addY(builder, y) {
    builder.addFieldFloat32(4, y, 0);
  }
  static addW(builder, w) {
    builder.addFieldFloat32(5, w, 0);
  }
  static addH(builder, h) {
    builder.addFieldFloat32(6, h, 0);
  }
  static endClassificationCandidate(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createClassificationCandidate(builder, confidence, classId, textOffset, x, y, w, h) {
    _ClassificationCandidate.startClassificationCandidate(builder);
    _ClassificationCandidate.addConfidence(builder, confidence);
    _ClassificationCandidate.addClassId(builder, classId);
    _ClassificationCandidate.addText(builder, textOffset);
    _ClassificationCandidate.addX(builder, x);
    _ClassificationCandidate.addY(builder, y);
    _ClassificationCandidate.addW(builder, w);
    _ClassificationCandidate.addH(builder, h);
    return _ClassificationCandidate.endClassificationCandidate(builder);
  }
  unpack() {
    return new ClassificationCandidateT(this.confidence(), this.classId(), this.text(), this.x(), this.y(), this.w(), this.h());
  }
  unpackTo(_o) {
    _o.confidence = this.confidence();
    _o.classId = this.classId();
    _o.text = this.text();
    _o.x = this.x();
    _o.y = this.y();
    _o.w = this.w();
    _o.h = this.h();
  }
};
var ClassificationCandidateT = class {
  constructor(confidence = 0, classId = -1, text2 = null, x = 0, y = 0, w = 0, h = 0) {
    this.confidence = confidence;
    this.classId = classId;
    this.text = text2;
    this.x = x;
    this.y = y;
    this.w = w;
    this.h = h;
  }
  pack(builder) {
    const text2 = this.text !== null ? builder.createString(this.text) : 0;
    return ClassificationCandidate.createClassificationCandidate(builder, this.confidence, this.classId, text2, this.x, this.y, this.w, this.h);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification.js
var Classification = class _Classification {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsClassification(bb, obj) {
    return (obj || new _Classification()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsClassification(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _Classification()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  candidates(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? (obj || new ClassificationCandidate()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  candidatesLength() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startClassification(builder) {
    builder.startObject(2);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addCandidates(builder, candidatesOffset) {
    builder.addFieldOffset(1, candidatesOffset, 0);
  }
  static createCandidatesVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startCandidatesVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endClassification(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createClassification(builder, objectOffset, candidatesOffset) {
    _Classification.startClassification(builder);
    _Classification.addObject(builder, objectOffset);
    _Classification.addCandidates(builder, candidatesOffset);
    return _Classification.endClassification(builder);
  }
  unpack() {
    return new ClassificationT(this.object() !== null ? this.object().unpack() : null, this.bb.createObjList(this.candidates.bind(this), this.candidatesLength()));
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.candidates = this.bb.createObjList(this.candidates.bind(this), this.candidatesLength());
  }
};
var ClassificationT = class {
  constructor(object = null, candidates = []) {
    this.object = object;
    this.candidates = candidates;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const candidates = Classification.createCandidatesVector(builder, builder.createObjectOffsetList(this.candidates));
    return Classification.createClassification(builder, object, candidates);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/person-presence.js
var PersonPresence = class _PersonPresence {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsPersonPresence(bb, obj) {
    return (obj || new _PersonPresence()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsPersonPresence(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _PersonPresence()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  yesConfidence() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  noConfidence() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  static startPersonPresence(builder) {
    builder.startObject(3);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addYesConfidence(builder, yesConfidence) {
    builder.addFieldFloat32(1, yesConfidence, 0);
  }
  static addNoConfidence(builder, noConfidence) {
    builder.addFieldFloat32(2, noConfidence, 0);
  }
  static endPersonPresence(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createPersonPresence(builder, objectOffset, yesConfidence, noConfidence) {
    _PersonPresence.startPersonPresence(builder);
    _PersonPresence.addObject(builder, objectOffset);
    _PersonPresence.addYesConfidence(builder, yesConfidence);
    _PersonPresence.addNoConfidence(builder, noConfidence);
    return _PersonPresence.endPersonPresence(builder);
  }
  unpack() {
    return new PersonPresenceT(this.object() !== null ? this.object().unpack() : null, this.yesConfidence(), this.noConfidence());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.yesConfidence = this.yesConfidence();
    _o.noConfidence = this.noConfidence();
  }
};
var PersonPresenceT = class {
  constructor(object = null, yesConfidence = 0, noConfidence = 0) {
    this.object = object;
    this.yesConfidence = yesConfidence;
    this.noConfidence = noConfidence;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    return PersonPresence.createPersonPresence(builder, object, this.yesConfidence, this.noConfidence);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classifications.js
var Classifications = class _Classifications {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsClassifications(bb, obj) {
    return (obj || new _Classifications()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsClassifications(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _Classifications()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("CLSF");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  classifications(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new Classification()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  classificationsLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  personPresence(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? (obj || new PersonPresence()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  personPresenceLength() {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startClassifications(builder) {
    builder.startObject(5);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addClassifications(builder, classificationsOffset) {
    builder.addFieldOffset(3, classificationsOffset, 0);
  }
  static createClassificationsVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startClassificationsVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static addPersonPresence(builder, personPresenceOffset) {
    builder.addFieldOffset(4, personPresenceOffset, 0);
  }
  static createPersonPresenceVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startPersonPresenceVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endClassifications(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishClassificationsBuffer(builder, offset) {
    builder.finish(offset, "CLSF");
  }
  static finishSizePrefixedClassificationsBuffer(builder, offset) {
    builder.finish(offset, "CLSF", true);
  }
  unpack() {
    return new ClassificationsT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.classifications.bind(this), this.classificationsLength()), this.bb.createObjList(this.personPresence.bind(this), this.personPresenceLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.classifications = this.bb.createObjList(this.classifications.bind(this), this.classificationsLength());
    _o.personPresence = this.bb.createObjList(this.personPresence.bind(this), this.personPresenceLength());
  }
};
var ClassificationsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, classifications = [], personPresence = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.classifications = classifications;
    this.personPresence = personPresence;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const classifications = Classifications.createClassificationsVector(builder, builder.createObjectOffsetList(this.classifications));
    const personPresence = Classifications.createPersonPresenceVector(builder, builder.createObjectOffsetList(this.personPresence));
    Classifications.startClassifications(builder);
    Classifications.addSchemaMajor(builder, this.schemaMajor);
    Classifications.addSchemaMinor(builder, this.schemaMinor);
    Classifications.addLayer(builder, layer2);
    Classifications.addClassifications(builder, classifications);
    Classifications.addPersonPresence(builder, personPresence);
    return Classifications.endClassifications(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/audio-frame-context.js
var AudioFrameContext = class _AudioFrameContext {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsAudioFrameContext(bb, obj) {
    return (obj || new _AudioFrameContext()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsAudioFrameContext(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _AudioFrameContext()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  originalChannels() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  originalFrequency() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  originalSampleCount() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  cutLeftSampleCount() {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  cutRightSampleCount() {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  static startAudioFrameContext(builder) {
    builder.startObject(6);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addOriginalChannels(builder, originalChannels) {
    builder.addFieldInt64(1, originalChannels, BigInt("0"));
  }
  static addOriginalFrequency(builder, originalFrequency) {
    builder.addFieldInt64(2, originalFrequency, BigInt("0"));
  }
  static addOriginalSampleCount(builder, originalSampleCount) {
    builder.addFieldInt64(3, originalSampleCount, BigInt("0"));
  }
  static addCutLeftSampleCount(builder, cutLeftSampleCount) {
    builder.addFieldInt64(4, cutLeftSampleCount, BigInt("0"));
  }
  static addCutRightSampleCount(builder, cutRightSampleCount) {
    builder.addFieldInt64(5, cutRightSampleCount, BigInt("0"));
  }
  static endAudioFrameContext(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createAudioFrameContext(builder, objectOffset, originalChannels, originalFrequency, originalSampleCount, cutLeftSampleCount, cutRightSampleCount) {
    _AudioFrameContext.startAudioFrameContext(builder);
    _AudioFrameContext.addObject(builder, objectOffset);
    _AudioFrameContext.addOriginalChannels(builder, originalChannels);
    _AudioFrameContext.addOriginalFrequency(builder, originalFrequency);
    _AudioFrameContext.addOriginalSampleCount(builder, originalSampleCount);
    _AudioFrameContext.addCutLeftSampleCount(builder, cutLeftSampleCount);
    _AudioFrameContext.addCutRightSampleCount(builder, cutRightSampleCount);
    return _AudioFrameContext.endAudioFrameContext(builder);
  }
  unpack() {
    return new AudioFrameContextT(this.object() !== null ? this.object().unpack() : null, this.originalChannels(), this.originalFrequency(), this.originalSampleCount(), this.cutLeftSampleCount(), this.cutRightSampleCount());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.originalChannels = this.originalChannels();
    _o.originalFrequency = this.originalFrequency();
    _o.originalSampleCount = this.originalSampleCount();
    _o.cutLeftSampleCount = this.cutLeftSampleCount();
    _o.cutRightSampleCount = this.cutRightSampleCount();
  }
};
var AudioFrameContextT = class {
  constructor(object = null, originalChannels = BigInt("0"), originalFrequency = BigInt("0"), originalSampleCount = BigInt("0"), cutLeftSampleCount = BigInt("0"), cutRightSampleCount = BigInt("0")) {
    this.object = object;
    this.originalChannels = originalChannels;
    this.originalFrequency = originalFrequency;
    this.originalSampleCount = originalSampleCount;
    this.cutLeftSampleCount = cutLeftSampleCount;
    this.cutRightSampleCount = cutRightSampleCount;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    return AudioFrameContext.createAudioFrameContext(builder, object, this.originalChannels, this.originalFrequency, this.originalSampleCount, this.cutLeftSampleCount, this.cutRightSampleCount);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/video-frame-context.js
var VideoFrameContext = class _VideoFrameContext {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsVideoFrameContext(bb, obj) {
    return (obj || new _VideoFrameContext()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsVideoFrameContext(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _VideoFrameContext()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  originalWidth() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  originalHeight() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  sourceCropLeft() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  sourceCropRight() {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  sourceCropTop() {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  sourceCropBottom() {
    const offset = this.bb.__offset(this.bb_pos, 16);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  letterboxLeft() {
    const offset = this.bb.__offset(this.bb_pos, 18);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  letterboxRight() {
    const offset = this.bb.__offset(this.bb_pos, 20);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  letterboxTop() {
    const offset = this.bb.__offset(this.bb_pos, 22);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  letterboxBottom() {
    const offset = this.bb.__offset(this.bb_pos, 24);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  static startVideoFrameContext(builder) {
    builder.startObject(11);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addOriginalWidth(builder, originalWidth) {
    builder.addFieldInt64(1, originalWidth, BigInt("0"));
  }
  static addOriginalHeight(builder, originalHeight) {
    builder.addFieldInt64(2, originalHeight, BigInt("0"));
  }
  static addSourceCropLeft(builder, sourceCropLeft) {
    builder.addFieldInt64(3, sourceCropLeft, BigInt("0"));
  }
  static addSourceCropRight(builder, sourceCropRight) {
    builder.addFieldInt64(4, sourceCropRight, BigInt("0"));
  }
  static addSourceCropTop(builder, sourceCropTop) {
    builder.addFieldInt64(5, sourceCropTop, BigInt("0"));
  }
  static addSourceCropBottom(builder, sourceCropBottom) {
    builder.addFieldInt64(6, sourceCropBottom, BigInt("0"));
  }
  static addLetterboxLeft(builder, letterboxLeft) {
    builder.addFieldInt64(7, letterboxLeft, BigInt("0"));
  }
  static addLetterboxRight(builder, letterboxRight) {
    builder.addFieldInt64(8, letterboxRight, BigInt("0"));
  }
  static addLetterboxTop(builder, letterboxTop) {
    builder.addFieldInt64(9, letterboxTop, BigInt("0"));
  }
  static addLetterboxBottom(builder, letterboxBottom) {
    builder.addFieldInt64(10, letterboxBottom, BigInt("0"));
  }
  static endVideoFrameContext(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createVideoFrameContext(builder, objectOffset, originalWidth, originalHeight, sourceCropLeft, sourceCropRight, sourceCropTop, sourceCropBottom, letterboxLeft, letterboxRight, letterboxTop, letterboxBottom) {
    _VideoFrameContext.startVideoFrameContext(builder);
    _VideoFrameContext.addObject(builder, objectOffset);
    _VideoFrameContext.addOriginalWidth(builder, originalWidth);
    _VideoFrameContext.addOriginalHeight(builder, originalHeight);
    _VideoFrameContext.addSourceCropLeft(builder, sourceCropLeft);
    _VideoFrameContext.addSourceCropRight(builder, sourceCropRight);
    _VideoFrameContext.addSourceCropTop(builder, sourceCropTop);
    _VideoFrameContext.addSourceCropBottom(builder, sourceCropBottom);
    _VideoFrameContext.addLetterboxLeft(builder, letterboxLeft);
    _VideoFrameContext.addLetterboxRight(builder, letterboxRight);
    _VideoFrameContext.addLetterboxTop(builder, letterboxTop);
    _VideoFrameContext.addLetterboxBottom(builder, letterboxBottom);
    return _VideoFrameContext.endVideoFrameContext(builder);
  }
  unpack() {
    return new VideoFrameContextT(this.object() !== null ? this.object().unpack() : null, this.originalWidth(), this.originalHeight(), this.sourceCropLeft(), this.sourceCropRight(), this.sourceCropTop(), this.sourceCropBottom(), this.letterboxLeft(), this.letterboxRight(), this.letterboxTop(), this.letterboxBottom());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.originalWidth = this.originalWidth();
    _o.originalHeight = this.originalHeight();
    _o.sourceCropLeft = this.sourceCropLeft();
    _o.sourceCropRight = this.sourceCropRight();
    _o.sourceCropTop = this.sourceCropTop();
    _o.sourceCropBottom = this.sourceCropBottom();
    _o.letterboxLeft = this.letterboxLeft();
    _o.letterboxRight = this.letterboxRight();
    _o.letterboxTop = this.letterboxTop();
    _o.letterboxBottom = this.letterboxBottom();
  }
};
var VideoFrameContextT = class {
  constructor(object = null, originalWidth = BigInt("0"), originalHeight = BigInt("0"), sourceCropLeft = BigInt("0"), sourceCropRight = BigInt("0"), sourceCropTop = BigInt("0"), sourceCropBottom = BigInt("0"), letterboxLeft = BigInt("0"), letterboxRight = BigInt("0"), letterboxTop = BigInt("0"), letterboxBottom = BigInt("0")) {
    this.object = object;
    this.originalWidth = originalWidth;
    this.originalHeight = originalHeight;
    this.sourceCropLeft = sourceCropLeft;
    this.sourceCropRight = sourceCropRight;
    this.sourceCropTop = sourceCropTop;
    this.sourceCropBottom = sourceCropBottom;
    this.letterboxLeft = letterboxLeft;
    this.letterboxRight = letterboxRight;
    this.letterboxTop = letterboxTop;
    this.letterboxBottom = letterboxBottom;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    return VideoFrameContext.createVideoFrameContext(builder, object, this.originalWidth, this.originalHeight, this.sourceCropLeft, this.sourceCropRight, this.sourceCropTop, this.sourceCropBottom, this.letterboxLeft, this.letterboxRight, this.letterboxTop, this.letterboxBottom);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/frame-context.js
var FrameContext = class _FrameContext {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsFrameContext(bb, obj) {
    return (obj || new _FrameContext()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsFrameContext(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _FrameContext()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("FCTX");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  video(obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new VideoFrameContext()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  audio(obj) {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? (obj || new AudioFrameContext()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  static startFrameContext(builder) {
    builder.startObject(5);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addVideo(builder, videoOffset) {
    builder.addFieldOffset(3, videoOffset, 0);
  }
  static addAudio(builder, audioOffset) {
    builder.addFieldOffset(4, audioOffset, 0);
  }
  static endFrameContext(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishFrameContextBuffer(builder, offset) {
    builder.finish(offset, "FCTX");
  }
  static finishSizePrefixedFrameContextBuffer(builder, offset) {
    builder.finish(offset, "FCTX", true);
  }
  unpack() {
    return new FrameContextT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.video() !== null ? this.video().unpack() : null, this.audio() !== null ? this.audio().unpack() : null);
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.video = this.video() !== null ? this.video().unpack() : null;
    _o.audio = this.audio() !== null ? this.audio().unpack() : null;
  }
};
var FrameContextT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, video5 = null, audio = null) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.video = video5;
    this.audio = audio;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const video5 = this.video !== null ? this.video.pack(builder) : 0;
    const audio = this.audio !== null ? this.audio.pack(builder) : 0;
    FrameContext.startFrameContext(builder);
    FrameContext.addSchemaMajor(builder, this.schemaMajor);
    FrameContext.addSchemaMinor(builder, this.schemaMinor);
    FrameContext.addLayer(builder, layer2);
    FrameContext.addVideo(builder, video5);
    FrameContext.addAudio(builder, audio);
    return FrameContext.endFrameContext(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/bitmap-data.js
var BitmapData = class _BitmapData {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsBitmapData(bb, obj) {
    return (obj || new _BitmapData()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsBitmapData(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _BitmapData()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  width() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint32(this.bb_pos + offset) : 0;
  }
  height() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint32(this.bb_pos + offset) : 0;
  }
  valueType(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  pixels(index) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readUint8(this.bb.__vector(this.bb_pos + offset) + index) : 0;
  }
  pixelsLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  pixelsArray() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? new Uint8Array(this.bb.bytes().buffer, this.bb.bytes().byteOffset + this.bb.__vector(this.bb_pos + offset), this.bb.__vector_len(this.bb_pos + offset)) : null;
  }
  static startBitmapData(builder) {
    builder.startObject(4);
  }
  static addWidth(builder, width) {
    builder.addFieldInt32(0, width, 0);
  }
  static addHeight(builder, height) {
    builder.addFieldInt32(1, height, 0);
  }
  static addValueType(builder, valueTypeOffset) {
    builder.addFieldOffset(2, valueTypeOffset, 0);
  }
  static addPixels(builder, pixelsOffset) {
    builder.addFieldOffset(3, pixelsOffset, 0);
  }
  static createPixelsVector(builder, data) {
    builder.startVector(1, data.length, 1);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addInt8(data[i]);
    }
    return builder.endVector();
  }
  static startPixelsVector(builder, numElems) {
    builder.startVector(1, numElems, 1);
  }
  static endBitmapData(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createBitmapData(builder, width, height, valueTypeOffset, pixelsOffset) {
    _BitmapData.startBitmapData(builder);
    _BitmapData.addWidth(builder, width);
    _BitmapData.addHeight(builder, height);
    _BitmapData.addValueType(builder, valueTypeOffset);
    _BitmapData.addPixels(builder, pixelsOffset);
    return _BitmapData.endBitmapData(builder);
  }
  unpack() {
    return new BitmapDataT(this.width(), this.height(), this.valueType(), this.bb.createScalarList(this.pixels.bind(this), this.pixelsLength()));
  }
  unpackTo(_o) {
    _o.width = this.width();
    _o.height = this.height();
    _o.valueType = this.valueType();
    _o.pixels = this.bb.createScalarList(this.pixels.bind(this), this.pixelsLength());
  }
};
var BitmapDataT = class {
  constructor(width = 0, height = 0, valueType = null, pixels = []) {
    this.width = width;
    this.height = height;
    this.valueType = valueType;
    this.pixels = pixels;
  }
  pack(builder) {
    const valueType = this.valueType !== null ? builder.createString(this.valueType) : 0;
    const pixels = BitmapData.createPixelsVector(builder, this.pixels);
    return BitmapData.createBitmapData(builder, this.width, this.height, valueType, pixels);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/instance-segmentation-mask.js
var InstanceSegmentationMask = class _InstanceSegmentationMask {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsInstanceSegmentationMask(bb, obj) {
    return (obj || new _InstanceSegmentationMask()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsInstanceSegmentationMask(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _InstanceSegmentationMask()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  bitmap(obj) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? (obj || new BitmapData()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  classId() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readInt32(this.bb_pos + offset) : -1;
  }
  confidence() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  box(obj) {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? (obj || new BoundingBox()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  static startInstanceSegmentationMask(builder) {
    builder.startObject(5);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addBitmap(builder, bitmapOffset) {
    builder.addFieldOffset(1, bitmapOffset, 0);
  }
  static addClassId(builder, classId) {
    builder.addFieldInt32(2, classId, -1);
  }
  static addConfidence(builder, confidence) {
    builder.addFieldFloat32(3, confidence, 0);
  }
  static addBox(builder, boxOffset) {
    builder.addFieldOffset(4, boxOffset, 0);
  }
  static endInstanceSegmentationMask(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new InstanceSegmentationMaskT(this.object() !== null ? this.object().unpack() : null, this.bitmap() !== null ? this.bitmap().unpack() : null, this.classId(), this.confidence(), this.box() !== null ? this.box().unpack() : null);
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.bitmap = this.bitmap() !== null ? this.bitmap().unpack() : null;
    _o.classId = this.classId();
    _o.confidence = this.confidence();
    _o.box = this.box() !== null ? this.box().unpack() : null;
  }
};
var InstanceSegmentationMaskT = class {
  constructor(object = null, bitmap = null, classId = -1, confidence = 0, box = null) {
    this.object = object;
    this.bitmap = bitmap;
    this.classId = classId;
    this.confidence = confidence;
    this.box = box;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const bitmap = this.bitmap !== null ? this.bitmap.pack(builder) : 0;
    const box = this.box !== null ? this.box.pack(builder) : 0;
    InstanceSegmentationMask.startInstanceSegmentationMask(builder);
    InstanceSegmentationMask.addObject(builder, object);
    InstanceSegmentationMask.addBitmap(builder, bitmap);
    InstanceSegmentationMask.addClassId(builder, this.classId);
    InstanceSegmentationMask.addConfidence(builder, this.confidence);
    InstanceSegmentationMask.addBox(builder, box);
    return InstanceSegmentationMask.endInstanceSegmentationMask(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/instance-segmentation-masks.js
var InstanceSegmentationMasks = class _InstanceSegmentationMasks {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsInstanceSegmentationMasks(bb, obj) {
    return (obj || new _InstanceSegmentationMasks()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsInstanceSegmentationMasks(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _InstanceSegmentationMasks()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("ISGM");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  masks(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new InstanceSegmentationMask()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  masksLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startInstanceSegmentationMasks(builder) {
    builder.startObject(4);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addMasks(builder, masksOffset) {
    builder.addFieldOffset(3, masksOffset, 0);
  }
  static createMasksVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startMasksVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endInstanceSegmentationMasks(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishInstanceSegmentationMasksBuffer(builder, offset) {
    builder.finish(offset, "ISGM");
  }
  static finishSizePrefixedInstanceSegmentationMasksBuffer(builder, offset) {
    builder.finish(offset, "ISGM", true);
  }
  unpack() {
    return new InstanceSegmentationMasksT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.masks.bind(this), this.masksLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.masks = this.bb.createObjList(this.masks.bind(this), this.masksLength());
  }
};
var InstanceSegmentationMasksT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, masks = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.masks = masks;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const masks = InstanceSegmentationMasks.createMasksVector(builder, builder.createObjectOffsetList(this.masks));
    InstanceSegmentationMasks.startInstanceSegmentationMasks(builder);
    InstanceSegmentationMasks.addSchemaMajor(builder, this.schemaMajor);
    InstanceSegmentationMasks.addSchemaMinor(builder, this.schemaMinor);
    InstanceSegmentationMasks.addLayer(builder, layer2);
    InstanceSegmentationMasks.addMasks(builder, masks);
    return InstanceSegmentationMasks.endInstanceSegmentationMasks(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-embedding.js
var ObjectEmbedding = class _ObjectEmbedding {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsObjectEmbedding(bb, obj) {
    return (obj || new _ObjectEmbedding()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsObjectEmbedding(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _ObjectEmbedding()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  values(index) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb.__vector(this.bb_pos + offset) + index * 4) : 0;
  }
  valuesLength() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  valuesArray() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? new Float32Array(this.bb.bytes().buffer, this.bb.bytes().byteOffset + this.bb.__vector(this.bb_pos + offset), this.bb.__vector_len(this.bb_pos + offset)) : null;
  }
  static startObjectEmbedding(builder) {
    builder.startObject(2);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addValues(builder, valuesOffset) {
    builder.addFieldOffset(1, valuesOffset, 0);
  }
  static createValuesVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addFloat32(data[i]);
    }
    return builder.endVector();
  }
  static startValuesVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endObjectEmbedding(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createObjectEmbedding(builder, objectOffset, valuesOffset) {
    _ObjectEmbedding.startObjectEmbedding(builder);
    _ObjectEmbedding.addObject(builder, objectOffset);
    _ObjectEmbedding.addValues(builder, valuesOffset);
    return _ObjectEmbedding.endObjectEmbedding(builder);
  }
  unpack() {
    return new ObjectEmbeddingT(this.object() !== null ? this.object().unpack() : null, this.bb.createScalarList(this.values.bind(this), this.valuesLength()));
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.values = this.bb.createScalarList(this.values.bind(this), this.valuesLength());
  }
};
var ObjectEmbeddingT = class {
  constructor(object = null, values = []) {
    this.object = object;
    this.values = values;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const values = ObjectEmbedding.createValuesVector(builder, this.values);
    return ObjectEmbedding.createObjectEmbedding(builder, object, values);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-embeddings.js
var ObjectEmbeddings = class _ObjectEmbeddings {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsObjectEmbeddings(bb, obj) {
    return (obj || new _ObjectEmbeddings()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsObjectEmbeddings(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _ObjectEmbeddings()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("EMBE");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  embeddings(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new ObjectEmbedding()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  embeddingsLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startObjectEmbeddings(builder) {
    builder.startObject(4);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addEmbeddings(builder, embeddingsOffset) {
    builder.addFieldOffset(3, embeddingsOffset, 0);
  }
  static createEmbeddingsVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startEmbeddingsVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endObjectEmbeddings(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishObjectEmbeddingsBuffer(builder, offset) {
    builder.finish(offset, "EMBE");
  }
  static finishSizePrefixedObjectEmbeddingsBuffer(builder, offset) {
    builder.finish(offset, "EMBE", true);
  }
  unpack() {
    return new ObjectEmbeddingsT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.embeddings.bind(this), this.embeddingsLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.embeddings = this.bb.createObjList(this.embeddings.bind(this), this.embeddingsLength());
  }
};
var ObjectEmbeddingsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, embeddings = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.embeddings = embeddings;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const embeddings = ObjectEmbeddings.createEmbeddingsVector(builder, builder.createObjectOffsetList(this.embeddings));
    ObjectEmbeddings.startObjectEmbeddings(builder);
    ObjectEmbeddings.addSchemaMajor(builder, this.schemaMajor);
    ObjectEmbeddings.addSchemaMinor(builder, this.schemaMinor);
    ObjectEmbeddings.addLayer(builder, layer2);
    ObjectEmbeddings.addEmbeddings(builder, embeddings);
    return ObjectEmbeddings.endObjectEmbeddings(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-track.js
var ObjectTrack = class _ObjectTrack {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsObjectTrack(bb, obj) {
    return (obj || new _ObjectTrack()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsObjectTrack(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _ObjectTrack()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  sourceId() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  trackId() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  box(obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new BoundingBox()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  confidence() {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  classId() {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.readInt32(this.bb_pos + offset) : -1;
  }
  text(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 16);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  diagnostic(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 18);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  predictedOnly() {
    const offset = this.bb.__offset(this.bb_pos, 20);
    return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
  }
  static startObjectTrack(builder) {
    builder.startObject(9);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addSourceId(builder, sourceId) {
    builder.addFieldInt64(1, sourceId, BigInt("0"));
  }
  static addTrackId(builder, trackId) {
    builder.addFieldInt64(2, trackId, BigInt("0"));
  }
  static addBox(builder, boxOffset) {
    builder.addFieldOffset(3, boxOffset, 0);
  }
  static addConfidence(builder, confidence) {
    builder.addFieldFloat32(4, confidence, 0);
  }
  static addClassId(builder, classId) {
    builder.addFieldInt32(5, classId, -1);
  }
  static addText(builder, textOffset) {
    builder.addFieldOffset(6, textOffset, 0);
  }
  static addDiagnostic(builder, diagnosticOffset) {
    builder.addFieldOffset(7, diagnosticOffset, 0);
  }
  static addPredictedOnly(builder, predictedOnly) {
    builder.addFieldInt8(8, +predictedOnly, 0);
  }
  static endObjectTrack(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new ObjectTrackT(this.object() !== null ? this.object().unpack() : null, this.sourceId(), this.trackId(), this.box() !== null ? this.box().unpack() : null, this.confidence(), this.classId(), this.text(), this.diagnostic(), this.predictedOnly());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.sourceId = this.sourceId();
    _o.trackId = this.trackId();
    _o.box = this.box() !== null ? this.box().unpack() : null;
    _o.confidence = this.confidence();
    _o.classId = this.classId();
    _o.text = this.text();
    _o.diagnostic = this.diagnostic();
    _o.predictedOnly = this.predictedOnly();
  }
};
var ObjectTrackT = class {
  constructor(object = null, sourceId = BigInt("0"), trackId = BigInt("0"), box = null, confidence = 0, classId = -1, text2 = null, diagnostic = null, predictedOnly = false) {
    this.object = object;
    this.sourceId = sourceId;
    this.trackId = trackId;
    this.box = box;
    this.confidence = confidence;
    this.classId = classId;
    this.text = text2;
    this.diagnostic = diagnostic;
    this.predictedOnly = predictedOnly;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const box = this.box !== null ? this.box.pack(builder) : 0;
    const text2 = this.text !== null ? builder.createString(this.text) : 0;
    const diagnostic = this.diagnostic !== null ? builder.createString(this.diagnostic) : 0;
    ObjectTrack.startObjectTrack(builder);
    ObjectTrack.addObject(builder, object);
    ObjectTrack.addSourceId(builder, this.sourceId);
    ObjectTrack.addTrackId(builder, this.trackId);
    ObjectTrack.addBox(builder, box);
    ObjectTrack.addConfidence(builder, this.confidence);
    ObjectTrack.addClassId(builder, this.classId);
    ObjectTrack.addText(builder, text2);
    ObjectTrack.addDiagnostic(builder, diagnostic);
    ObjectTrack.addPredictedOnly(builder, this.predictedOnly);
    return ObjectTrack.endObjectTrack(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-tracks.js
var ObjectTracks = class _ObjectTracks {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsObjectTracks(bb, obj) {
    return (obj || new _ObjectTracks()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsObjectTracks(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _ObjectTracks()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("TRKS");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  tracks(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new ObjectTrack()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  tracksLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startObjectTracks(builder) {
    builder.startObject(4);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addTracks(builder, tracksOffset) {
    builder.addFieldOffset(3, tracksOffset, 0);
  }
  static createTracksVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startTracksVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endObjectTracks(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishObjectTracksBuffer(builder, offset) {
    builder.finish(offset, "TRKS");
  }
  static finishSizePrefixedObjectTracksBuffer(builder, offset) {
    builder.finish(offset, "TRKS", true);
  }
  unpack() {
    return new ObjectTracksT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.tracks.bind(this), this.tracksLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.tracks = this.bb.createObjList(this.tracks.bind(this), this.tracksLength());
  }
};
var ObjectTracksT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, tracks = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.tracks = tracks;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const tracks = ObjectTracks.createTracksVector(builder, builder.createObjectOffsetList(this.tracks));
    ObjectTracks.startObjectTracks(builder);
    ObjectTracks.addSchemaMajor(builder, this.schemaMajor);
    ObjectTracks.addSchemaMinor(builder, this.schemaMinor);
    ObjectTracks.addLayer(builder, layer2);
    ObjectTracks.addTracks(builder, tracks);
    return ObjectTracks.endObjectTracks(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/performance-overlay.js
var PerformanceOverlay = class _PerformanceOverlay {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsPerformanceOverlay(bb, obj) {
    return (obj || new _PerformanceOverlay()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsPerformanceOverlay(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _PerformanceOverlay()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("PERF");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  lines(index, optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb.__vector(this.bb_pos + offset) + index * 4, optionalEncoding) : null;
  }
  linesLength() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startPerformanceOverlay(builder) {
    builder.startObject(3);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLines(builder, linesOffset) {
    builder.addFieldOffset(2, linesOffset, 0);
  }
  static createLinesVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startLinesVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endPerformanceOverlay(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishPerformanceOverlayBuffer(builder, offset) {
    builder.finish(offset, "PERF");
  }
  static finishSizePrefixedPerformanceOverlayBuffer(builder, offset) {
    builder.finish(offset, "PERF", true);
  }
  static createPerformanceOverlay(builder, schemaMajor, schemaMinor, linesOffset) {
    _PerformanceOverlay.startPerformanceOverlay(builder);
    _PerformanceOverlay.addSchemaMajor(builder, schemaMajor);
    _PerformanceOverlay.addSchemaMinor(builder, schemaMinor);
    _PerformanceOverlay.addLines(builder, linesOffset);
    return _PerformanceOverlay.endPerformanceOverlay(builder);
  }
  unpack() {
    return new PerformanceOverlayT(this.schemaMajor(), this.schemaMinor(), this.bb.createScalarList(this.lines.bind(this), this.linesLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.lines = this.bb.createScalarList(this.lines.bind(this), this.linesLength());
  }
};
var PerformanceOverlayT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, lines = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.lines = lines;
  }
  pack(builder) {
    const lines = PerformanceOverlay.createLinesVector(builder, builder.createObjectOffsetList(this.lines));
    return PerformanceOverlay.createPerformanceOverlay(builder, this.schemaMajor, this.schemaMinor, lines);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/pose-estimation.js
var PoseEstimation = class _PoseEstimation {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsPoseEstimation(bb, obj) {
    return (obj || new _PoseEstimation()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsPoseEstimation(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _PoseEstimation()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  confidence() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  yaw() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  pitch() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  static startPoseEstimation(builder) {
    builder.startObject(4);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addConfidence(builder, confidence) {
    builder.addFieldFloat32(1, confidence, 0);
  }
  static addYaw(builder, yaw) {
    builder.addFieldFloat32(2, yaw, 0);
  }
  static addPitch(builder, pitch) {
    builder.addFieldFloat32(3, pitch, 0);
  }
  static endPoseEstimation(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createPoseEstimation(builder, objectOffset, confidence, yaw, pitch) {
    _PoseEstimation.startPoseEstimation(builder);
    _PoseEstimation.addObject(builder, objectOffset);
    _PoseEstimation.addConfidence(builder, confidence);
    _PoseEstimation.addYaw(builder, yaw);
    _PoseEstimation.addPitch(builder, pitch);
    return _PoseEstimation.endPoseEstimation(builder);
  }
  unpack() {
    return new PoseEstimationT(this.object() !== null ? this.object().unpack() : null, this.confidence(), this.yaw(), this.pitch());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.confidence = this.confidence();
    _o.yaw = this.yaw();
    _o.pitch = this.pitch();
  }
};
var PoseEstimationT = class {
  constructor(object = null, confidence = 0, yaw = 0, pitch = 0) {
    this.object = object;
    this.confidence = confidence;
    this.yaw = yaw;
    this.pitch = pitch;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    return PoseEstimation.createPoseEstimation(builder, object, this.confidence, this.yaw, this.pitch);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/pose-estimations.js
var PoseEstimations = class _PoseEstimations {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsPoseEstimations(bb, obj) {
    return (obj || new _PoseEstimations()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsPoseEstimations(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _PoseEstimations()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("POSE");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  poses(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new PoseEstimation()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  posesLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startPoseEstimations(builder) {
    builder.startObject(4);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addPoses(builder, posesOffset) {
    builder.addFieldOffset(3, posesOffset, 0);
  }
  static createPosesVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startPosesVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endPoseEstimations(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishPoseEstimationsBuffer(builder, offset) {
    builder.finish(offset, "POSE");
  }
  static finishSizePrefixedPoseEstimationsBuffer(builder, offset) {
    builder.finish(offset, "POSE", true);
  }
  unpack() {
    return new PoseEstimationsT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.poses.bind(this), this.posesLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.poses = this.bb.createObjList(this.poses.bind(this), this.posesLength());
  }
};
var PoseEstimationsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, poses = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.poses = poses;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const poses = PoseEstimations.createPosesVector(builder, builder.createObjectOffsetList(this.poses));
    PoseEstimations.startPoseEstimations(builder);
    PoseEstimations.addSchemaMajor(builder, this.schemaMajor);
    PoseEstimations.addSchemaMinor(builder, this.schemaMinor);
    PoseEstimations.addLayer(builder, layer2);
    PoseEstimations.addPoses(builder, poses);
    return PoseEstimations.endPoseEstimations(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-mask.js
var SegmentationMask = class _SegmentationMask {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsSegmentationMask(bb, obj) {
    return (obj || new _SegmentationMask()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsSegmentationMask(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _SegmentationMask()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  bitmap(obj) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? (obj || new BitmapData()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  static startSegmentationMask(builder) {
    builder.startObject(2);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addBitmap(builder, bitmapOffset) {
    builder.addFieldOffset(1, bitmapOffset, 0);
  }
  static endSegmentationMask(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new SegmentationMaskT(this.object() !== null ? this.object().unpack() : null, this.bitmap() !== null ? this.bitmap().unpack() : null);
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.bitmap = this.bitmap() !== null ? this.bitmap().unpack() : null;
  }
};
var SegmentationMaskT = class {
  constructor(object = null, bitmap = null) {
    this.object = object;
    this.bitmap = bitmap;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const bitmap = this.bitmap !== null ? this.bitmap.pack(builder) : 0;
    SegmentationMask.startSegmentationMask(builder);
    SegmentationMask.addObject(builder, object);
    SegmentationMask.addBitmap(builder, bitmap);
    return SegmentationMask.endSegmentationMask(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-masks.js
var SegmentationMasks = class _SegmentationMasks {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsSegmentationMasks(bb, obj) {
    return (obj || new _SegmentationMasks()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsSegmentationMasks(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _SegmentationMasks()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("SGMS");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  masks(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new SegmentationMask()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  masksLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startSegmentationMasks(builder) {
    builder.startObject(4);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addMasks(builder, masksOffset) {
    builder.addFieldOffset(3, masksOffset, 0);
  }
  static createMasksVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startMasksVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endSegmentationMasks(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishSegmentationMasksBuffer(builder, offset) {
    builder.finish(offset, "SGMS");
  }
  static finishSizePrefixedSegmentationMasksBuffer(builder, offset) {
    builder.finish(offset, "SGMS", true);
  }
  unpack() {
    return new SegmentationMasksT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.masks.bind(this), this.masksLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.masks = this.bb.createObjList(this.masks.bind(this), this.masksLength());
  }
};
var SegmentationMasksT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, masks = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.masks = masks;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const masks = SegmentationMasks.createMasksVector(builder, builder.createObjectOffsetList(this.masks));
    SegmentationMasks.startSegmentationMasks(builder);
    SegmentationMasks.addSchemaMajor(builder, this.schemaMajor);
    SegmentationMasks.addSchemaMinor(builder, this.schemaMinor);
    SegmentationMasks.addLayer(builder, layer2);
    SegmentationMasks.addMasks(builder, masks);
    return SegmentationMasks.endSegmentationMasks(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/semantic-segmentation-mask.js
var SemanticSegmentationMask = class _SemanticSegmentationMask {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsSemanticSegmentationMask(bb, obj) {
    return (obj || new _SemanticSegmentationMask()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsSemanticSegmentationMask(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _SemanticSegmentationMask()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  bitmap(obj) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? (obj || new BitmapData()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  static startSemanticSegmentationMask(builder) {
    builder.startObject(2);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addBitmap(builder, bitmapOffset) {
    builder.addFieldOffset(1, bitmapOffset, 0);
  }
  static endSemanticSegmentationMask(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new SemanticSegmentationMaskT(this.object() !== null ? this.object().unpack() : null, this.bitmap() !== null ? this.bitmap().unpack() : null);
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.bitmap = this.bitmap() !== null ? this.bitmap().unpack() : null;
  }
};
var SemanticSegmentationMaskT = class {
  constructor(object = null, bitmap = null) {
    this.object = object;
    this.bitmap = bitmap;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const bitmap = this.bitmap !== null ? this.bitmap.pack(builder) : 0;
    SemanticSegmentationMask.startSemanticSegmentationMask(builder);
    SemanticSegmentationMask.addObject(builder, object);
    SemanticSegmentationMask.addBitmap(builder, bitmap);
    return SemanticSegmentationMask.endSemanticSegmentationMask(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/semantic-segmentation-masks.js
var SemanticSegmentationMasks = class _SemanticSegmentationMasks {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsSemanticSegmentationMasks(bb, obj) {
    return (obj || new _SemanticSegmentationMasks()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsSemanticSegmentationMasks(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _SemanticSegmentationMasks()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("SSGM");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  masks(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new SemanticSegmentationMask()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  masksLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startSemanticSegmentationMasks(builder) {
    builder.startObject(4);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addMasks(builder, masksOffset) {
    builder.addFieldOffset(3, masksOffset, 0);
  }
  static createMasksVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startMasksVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endSemanticSegmentationMasks(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishSemanticSegmentationMasksBuffer(builder, offset) {
    builder.finish(offset, "SSGM");
  }
  static finishSizePrefixedSemanticSegmentationMasksBuffer(builder, offset) {
    builder.finish(offset, "SSGM", true);
  }
  unpack() {
    return new SemanticSegmentationMasksT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.masks.bind(this), this.masksLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.masks = this.bb.createObjList(this.masks.bind(this), this.masksLength());
  }
};
var SemanticSegmentationMasksT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, masks = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.masks = masks;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const masks = SemanticSegmentationMasks.createMasksVector(builder, builder.createObjectOffsetList(this.masks));
    SemanticSegmentationMasks.startSemanticSegmentationMasks(builder);
    SemanticSegmentationMasks.addSchemaMajor(builder, this.schemaMajor);
    SemanticSegmentationMasks.addSchemaMinor(builder, this.schemaMinor);
    SemanticSegmentationMasks.addLayer(builder, layer2);
    SemanticSegmentationMasks.addMasks(builder, masks);
    return SemanticSegmentationMasks.endSemanticSegmentationMasks(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/point2f.js
var Point2f = class _Point2f {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsPoint2f(bb, obj) {
    return (obj || new _Point2f()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsPoint2f(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _Point2f()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  x() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  y() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  static startPoint2f(builder) {
    builder.startObject(2);
  }
  static addX(builder, x) {
    builder.addFieldFloat32(0, x, 0);
  }
  static addY(builder, y) {
    builder.addFieldFloat32(1, y, 0);
  }
  static endPoint2f(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createPoint2f(builder, x, y) {
    _Point2f.startPoint2f(builder);
    _Point2f.addX(builder, x);
    _Point2f.addY(builder, y);
    return _Point2f.endPoint2f(builder);
  }
  unpack() {
    return new Point2fT(this.x(), this.y());
  }
  unpackTo(_o) {
    _o.x = this.x();
    _o.y = this.y();
  }
};
var Point2fT = class {
  constructor(x = 0, y = 0) {
    this.x = x;
    this.y = y;
  }
  pack(builder) {
    return Point2f.createPoint2f(builder, this.x, this.y);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-trace.js
var TrackTrace = class _TrackTrace {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsTrackTrace(bb, obj) {
    return (obj || new _TrackTrace()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsTrackTrace(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _TrackTrace()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  trackId() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  points(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new Point2f()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  pointsLength() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startTrackTrace(builder) {
    builder.startObject(3);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addTrackId(builder, trackId) {
    builder.addFieldInt64(1, trackId, BigInt("0"));
  }
  static addPoints(builder, pointsOffset) {
    builder.addFieldOffset(2, pointsOffset, 0);
  }
  static createPointsVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startPointsVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endTrackTrace(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createTrackTrace(builder, objectOffset, trackId, pointsOffset) {
    _TrackTrace.startTrackTrace(builder);
    _TrackTrace.addObject(builder, objectOffset);
    _TrackTrace.addTrackId(builder, trackId);
    _TrackTrace.addPoints(builder, pointsOffset);
    return _TrackTrace.endTrackTrace(builder);
  }
  unpack() {
    return new TrackTraceT(this.object() !== null ? this.object().unpack() : null, this.trackId(), this.bb.createObjList(this.points.bind(this), this.pointsLength()));
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.trackId = this.trackId();
    _o.points = this.bb.createObjList(this.points.bind(this), this.pointsLength());
  }
};
var TrackTraceT = class {
  constructor(object = null, trackId = BigInt("0"), points = []) {
    this.object = object;
    this.trackId = trackId;
    this.points = points;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const points = TrackTrace.createPointsVector(builder, builder.createObjectOffsetList(this.points));
    return TrackTrace.createTrackTrace(builder, object, this.trackId, points);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-traces.js
var TrackTraces = class _TrackTraces {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsTrackTraces(bb, obj) {
    return (obj || new _TrackTraces()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsTrackTraces(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _TrackTraces()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("TRCE");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  layer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new LayerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  traces(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new TrackTrace()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  tracesLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startTrackTraces(builder) {
    builder.startObject(4);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addLayer(builder, layerOffset) {
    builder.addFieldOffset(2, layerOffset, 0);
  }
  static addTraces(builder, tracesOffset) {
    builder.addFieldOffset(3, tracesOffset, 0);
  }
  static createTracesVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startTracesVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endTrackTraces(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishTrackTracesBuffer(builder, offset) {
    builder.finish(offset, "TRCE");
  }
  static finishSizePrefixedTrackTracesBuffer(builder, offset) {
    builder.finish(offset, "TRCE", true);
  }
  unpack() {
    return new TrackTracesT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.traces.bind(this), this.tracesLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.traces = this.bb.createObjList(this.traces.bind(this), this.tracesLength());
  }
};
var TrackTracesT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, traces = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.traces = traces;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const traces = TrackTraces.createTracesVector(builder, builder.createObjectOffsetList(this.traces));
    TrackTraces.startTrackTraces(builder);
    TrackTraces.addSchemaMajor(builder, this.schemaMajor);
    TrackTraces.addSchemaMinor(builder, this.schemaMinor);
    TrackTraces.addLayer(builder, layer2);
    TrackTraces.addTraces(builder, traces);
    return TrackTraces.endTrackTraces(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/registry.js
var decode_556103652012567315 = (blob) => BoxDetections.getRootAsBoxDetections(new ByteBuffer(blob)).unpack();
var verify_556103652012567315 = (blob) => BoxDetections.bufferHasIdentifier(new ByteBuffer(blob));
var decode_2852697023809600655 = (blob) => Classifications.getRootAsClassifications(new ByteBuffer(blob)).unpack();
var verify_2852697023809600655 = (blob) => Classifications.bufferHasIdentifier(new ByteBuffer(blob));
var decode_2065478860695108412 = (blob) => FrameContext.getRootAsFrameContext(new ByteBuffer(blob)).unpack();
var verify_2065478860695108412 = (blob) => FrameContext.bufferHasIdentifier(new ByteBuffer(blob));
var decode_3149752457626985059 = (blob) => InstanceSegmentationMasks.getRootAsInstanceSegmentationMasks(new ByteBuffer(blob)).unpack();
var verify_3149752457626985059 = (blob) => InstanceSegmentationMasks.bufferHasIdentifier(new ByteBuffer(blob));
var decode_5972661533224817501 = (blob) => ObjectEmbeddings.getRootAsObjectEmbeddings(new ByteBuffer(blob)).unpack();
var verify_5972661533224817501 = (blob) => ObjectEmbeddings.bufferHasIdentifier(new ByteBuffer(blob));
var decode_2960585987463094496 = (blob) => ObjectTracks.getRootAsObjectTracks(new ByteBuffer(blob)).unpack();
var verify_2960585987463094496 = (blob) => ObjectTracks.bufferHasIdentifier(new ByteBuffer(blob));
var decode_7749401259036278028 = (blob) => PerformanceOverlay.getRootAsPerformanceOverlay(new ByteBuffer(blob)).unpack();
var verify_7749401259036278028 = (blob) => PerformanceOverlay.bufferHasIdentifier(new ByteBuffer(blob));
var decode_9114952555105892553 = (blob) => PoseEstimations.getRootAsPoseEstimations(new ByteBuffer(blob)).unpack();
var verify_9114952555105892553 = (blob) => PoseEstimations.bufferHasIdentifier(new ByteBuffer(blob));
var decode_1998909987238011535 = (blob) => SegmentationMasks.getRootAsSegmentationMasks(new ByteBuffer(blob)).unpack();
var verify_1998909987238011535 = (blob) => SegmentationMasks.bufferHasIdentifier(new ByteBuffer(blob));
var decode_1602571589628393389 = (blob) => SemanticSegmentationMasks.getRootAsSemanticSegmentationMasks(new ByteBuffer(blob)).unpack();
var verify_1602571589628393389 = (blob) => SemanticSegmentationMasks.bufferHasIdentifier(new ByteBuffer(blob));
var decode_238211229389337861 = (blob) => TrackTraces.getRootAsTrackTraces(new ByteBuffer(blob)).unpack();
var verify_238211229389337861 = (blob) => TrackTraces.bufferHasIdentifier(new ByteBuffer(blob));
var _TYPE_REGISTRY = /* @__PURE__ */ new Map([
  [556103652012567315n, {
    name: "open_perception_kit::metadata::BoxDetections",
    root_type: "BoxDetections",
    qualified_root_type: "open_perception_kit.metadata.BoxDetections",
    file_identifier: "BDET",
    decode: decode_556103652012567315,
    verify: verify_556103652012567315
  }],
  [2852697023809600655n, {
    name: "open_perception_kit::metadata::Classifications",
    root_type: "Classifications",
    qualified_root_type: "open_perception_kit.metadata.Classifications",
    file_identifier: "CLSF",
    decode: decode_2852697023809600655,
    verify: verify_2852697023809600655
  }],
  [2065478860695108412n, {
    name: "open_perception_kit::metadata::FrameContext",
    root_type: "FrameContext",
    qualified_root_type: "open_perception_kit.metadata.FrameContext",
    file_identifier: "FCTX",
    decode: decode_2065478860695108412,
    verify: verify_2065478860695108412
  }],
  [3149752457626985059n, {
    name: "open_perception_kit::metadata::InstanceSegmentationMasks",
    root_type: "InstanceSegmentationMasks",
    qualified_root_type: "open_perception_kit.metadata.InstanceSegmentationMasks",
    file_identifier: "ISGM",
    decode: decode_3149752457626985059,
    verify: verify_3149752457626985059
  }],
  [5972661533224817501n, {
    name: "open_perception_kit::metadata::ObjectEmbeddings",
    root_type: "ObjectEmbeddings",
    qualified_root_type: "open_perception_kit.metadata.ObjectEmbeddings",
    file_identifier: "EMBE",
    decode: decode_5972661533224817501,
    verify: verify_5972661533224817501
  }],
  [2960585987463094496n, {
    name: "open_perception_kit::metadata::ObjectTracks",
    root_type: "ObjectTracks",
    qualified_root_type: "open_perception_kit.metadata.ObjectTracks",
    file_identifier: "TRKS",
    decode: decode_2960585987463094496,
    verify: verify_2960585987463094496
  }],
  [7749401259036278028n, {
    name: "open_perception_kit::metadata::PerformanceOverlay",
    root_type: "PerformanceOverlay",
    qualified_root_type: "open_perception_kit.metadata.PerformanceOverlay",
    file_identifier: "PERF",
    decode: decode_7749401259036278028,
    verify: verify_7749401259036278028
  }],
  [9114952555105892553n, {
    name: "open_perception_kit::metadata::PoseEstimations",
    root_type: "PoseEstimations",
    qualified_root_type: "open_perception_kit.metadata.PoseEstimations",
    file_identifier: "POSE",
    decode: decode_9114952555105892553,
    verify: verify_9114952555105892553
  }],
  [1998909987238011535n, {
    name: "open_perception_kit::metadata::SegmentationMasks",
    root_type: "SegmentationMasks",
    qualified_root_type: "open_perception_kit.metadata.SegmentationMasks",
    file_identifier: "SGMS",
    decode: decode_1998909987238011535,
    verify: verify_1998909987238011535
  }],
  [1602571589628393389n, {
    name: "open_perception_kit::metadata::SemanticSegmentationMasks",
    root_type: "SemanticSegmentationMasks",
    qualified_root_type: "open_perception_kit.metadata.SemanticSegmentationMasks",
    file_identifier: "SSGM",
    decode: decode_1602571589628393389,
    verify: verify_1602571589628393389
  }],
  [238211229389337861n, {
    name: "open_perception_kit::metadata::TrackTraces",
    root_type: "TrackTraces",
    qualified_root_type: "open_perception_kit.metadata.TrackTraces",
    file_identifier: "TRCE",
    decode: decode_238211229389337861,
    verify: verify_238211229389337861
  }]
]);
var _CLASS_TO_ID = /* @__PURE__ */ new Map([
  [BoxDetectionsT, 556103652012567315n],
  [ClassificationsT, 2852697023809600655n],
  [FrameContextT, 2065478860695108412n],
  [InstanceSegmentationMasksT, 3149752457626985059n],
  [ObjectEmbeddingsT, 5972661533224817501n],
  [ObjectTracksT, 2960585987463094496n],
  [PerformanceOverlayT, 7749401259036278028n],
  [PoseEstimationsT, 9114952555105892553n],
  [SegmentationMasksT, 1998909987238011535n],
  [SemanticSegmentationMasksT, 1602571589628393389n],
  [TrackTracesT, 238211229389337861n]
]);

// generated/open_perception_kit/ts/dist/open_perception_kit/envelope.js
var __classPrivateFieldSet = function(receiver, state, value, kind, f) {
  if (kind === "m") throw new TypeError("Private method is not writable");
  if (kind === "a" && !f) throw new TypeError("Private accessor was defined without a setter");
  if (typeof state === "function" ? receiver !== state || !f : !state.has(receiver)) throw new TypeError("Cannot write private member to an object whose class did not declare it");
  return kind === "a" ? f.call(receiver, value) : f ? f.value = value : state.set(receiver, value), value;
};
var __classPrivateFieldGet = function(receiver, state, kind, f) {
  if (kind === "a" && !f) throw new TypeError("Private accessor was defined without a getter");
  if (typeof state === "function" ? receiver !== state || !f : !state.has(receiver)) throw new TypeError("Cannot read private member from an object whose class did not declare it");
  return kind === "m" ? f : kind === "a" ? f.call(receiver) : f ? f.value : state.get(receiver);
};
var _ExternalKey_value;
var SDK_NAME = "open_perception_kit";
var SDK_VERSION = "0.1.2";
var SCHEMA_SET_SHA256 = "798c1d2579c38da1e0fdbbaa25392205a7c8df0f39e30a0a28ff0a1d2a484e07";
var EXTERNAL_KEY_MIN = BigInt("9223372036854775808");
var EXTERNAL_KEY_MASK = EXTERNAL_KEY_MIN - BigInt(1);
var EXTERNAL_HASH_OFFSET = BigInt("14695981039346656037");
var EXTERNAL_HASH_PRIME = BigInt("1099511628211");
var UINT64_MASK = BigInt("0xffffffffffffffff");
var textEncoder = new TextEncoder();
var externalKeyToken = /* @__PURE__ */ Symbol("external-key");
var SDK_NAME_PATTERN = /^[a-z][a-z0-9_]*$/;
var SEMANTIC_VERSION_PATTERN = /^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/;
var SHA256_PATTERN = /^[0-9a-f]{64}$/;
var ProducerIdentityStatus;
(function(ProducerIdentityStatus2) {
  ProducerIdentityStatus2["ExactMatch"] = "exact_match";
  ProducerIdentityStatus2["Missing"] = "missing";
  ProducerIdentityStatus2["Malformed"] = "malformed";
  ProducerIdentityStatus2["SdkNameMismatch"] = "sdk_name_mismatch";
  ProducerIdentityStatus2["SdkVersionMismatch"] = "sdk_version_mismatch";
  ProducerIdentityStatus2["SchemaSetMismatch"] = "schema_set_mismatch";
})(ProducerIdentityStatus || (ProducerIdentityStatus = {}));
function isExternalKeyValue(value) {
  return value >= EXTERNAL_KEY_MIN && value <= UINT64_MASK;
}
var ExternalKey = class _ExternalKey {
  constructor(value, token) {
    _ExternalKey_value.set(this, void 0);
    if (token !== externalKeyToken || !isExternalKeyValue(value)) {
      throw new Error(`${SDK_NAME} external keys must be created with external_key()`);
    }
    __classPrivateFieldSet(this, _ExternalKey_value, value, "f");
  }
  static fromString(key) {
    if (typeof key !== "string") {
      throw new Error(`${SDK_NAME} external key must be a string`);
    }
    let value = EXTERNAL_HASH_OFFSET;
    for (const byte of textEncoder.encode(key)) {
      value ^= BigInt(byte);
      value = value * EXTERNAL_HASH_PRIME & UINT64_MASK;
    }
    return new _ExternalKey(value & EXTERNAL_KEY_MASK | EXTERNAL_KEY_MIN, externalKeyToken);
  }
  get value() {
    return __classPrivateFieldGet(this, _ExternalKey_value, "f");
  }
  toString() {
    return __classPrivateFieldGet(this, _ExternalKey_value, "f").toString();
  }
};
_ExternalKey_value = /* @__PURE__ */ new WeakMap();
function is_external_key(key) {
  return key instanceof ExternalKey && isExternalKeyValue(key.value);
}
function resolveExternalKey(key) {
  if (!is_external_key(key)) {
    if (typeof key === "string") {
      throw new Error(`${SDK_NAME} external envelope operations require ExternalKey; call external_key(...) first`);
    }
    throw new Error(`${SDK_NAME} external key must be a value returned by external_key()`);
  }
  return key.value;
}
function resolvePayloadClass(payloadType) {
  if (typeof payloadType !== "function") {
    throw new Error(`${SDK_NAME} selector must be a generated payload class or ExternalKey`);
  }
  const id = _CLASS_TO_ID.get(payloadType);
  if (id === void 0) {
    throw new Error(`unknown ${SDK_NAME} payload type: ${payloadType.name}`);
  }
  return id;
}
function resolveNativePayload(value) {
  const id = _CLASS_TO_ID.get(value.constructor);
  if (id === void 0) {
    throw new Error(`unknown ${SDK_NAME} payload type: ${value.constructor.name}`);
  }
  return id;
}
function copyBlob(payload) {
  const blobArray = payload.blobArray();
  if (blobArray) {
    return new Uint8Array(blobArray);
  }
  const len = payload.blobLength();
  const out = new Uint8Array(len);
  for (let i = 0; i < len; i += 1) {
    out[i] = payload.blob(i) ?? 0;
  }
  return out;
}
function copyBytes(data) {
  const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
  return new Uint8Array(bytes);
}
function packNativePayload(id, value) {
  const info = _TYPE_REGISTRY.get(id);
  if (!info) {
    throw new Error(`unknown ${SDK_NAME} payload id: ${id}`);
  }
  const builder = new Builder(1024);
  const offset = value.pack(builder);
  builder.finish(offset, info.file_identifier);
  return builder.asUint8Array();
}
var Envelope = class {
  constructor(packet) {
    this.payloadEntries = [];
    this.validEnvelope = true;
    this.errorMessage = null;
    this.producerName = SDK_NAME;
    this.producerVersion = SDK_VERSION;
    this.producerSchemaDigest = SCHEMA_SET_SHA256;
    if (packet !== void 0) {
      this.load(packet);
    }
  }
  load(packet) {
    this.payloadEntries = [];
    this.validEnvelope = false;
    this.errorMessage = null;
    this.producerName = "";
    this.producerVersion = "";
    this.producerSchemaDigest = "";
    const bytes = packet instanceof Uint8Array ? packet : new Uint8Array(packet);
    try {
      const bb = new ByteBuffer(bytes);
      if (!WireEnvelope.bufferHasIdentifier(bb)) {
        this.errorMessage = `invalid ${SDK_NAME} envelope file_identifier`;
        return;
      }
      const envelope = WireEnvelope.getRootAsWireEnvelope(bb);
      this.producerName = envelope.producerSdkName() ?? "";
      this.producerVersion = envelope.producerSdkVersion() ?? "";
      this.producerSchemaDigest = envelope.producerSchemaSetSha256() ?? "";
      const count = envelope.payloadsLength();
      for (let i = 0; i < count; i += 1) {
        const payload = envelope.payloads(i, new WirePayload());
        if (payload === null) {
          continue;
        }
        const id = payload.id();
        this.payloadEntries.push({
          id,
          blob: copyBlob(payload)
        });
      }
      this.validEnvelope = true;
    } catch (error) {
      this.errorMessage = `invalid ${SDK_NAME} envelope: ${String(error)}`;
    }
  }
  // state
  valid() {
    return this.validEnvelope;
  }
  error() {
    return this.errorMessage;
  }
  producerSdkName() {
    return this.producerName;
  }
  producerSdkVersion() {
    return this.producerVersion;
  }
  producerSchemaSetSha256() {
    return this.producerSchemaDigest;
  }
  producerIdentity() {
    if (!this.producerName || !this.producerVersion || !this.producerSchemaDigest) {
      return ProducerIdentityStatus.Missing;
    }
    if (!SDK_NAME_PATTERN.test(this.producerName) || !SEMANTIC_VERSION_PATTERN.test(this.producerVersion) || !SHA256_PATTERN.test(this.producerSchemaDigest)) {
      return ProducerIdentityStatus.Malformed;
    }
    if (this.producerName !== SDK_NAME)
      return ProducerIdentityStatus.SdkNameMismatch;
    if (this.producerVersion !== SDK_VERSION)
      return ProducerIdentityStatus.SdkVersionMismatch;
    if (this.producerSchemaDigest !== SCHEMA_SET_SHA256) {
      return ProducerIdentityStatus.SchemaSetMismatch;
    }
    return ProducerIdentityStatus.ExactMatch;
  }
  empty() {
    return this.size() === 0;
  }
  size() {
    if (!this.validEnvelope)
      return 0;
    return this.payloadEntries.length;
  }
  count(selector) {
    if (!this.validEnvelope)
      return 0;
    if (is_external_key(selector)) {
      const id2 = resolveExternalKey(selector);
      return this.payloadEntries.filter((entry) => entry.id === id2).length;
    }
    const id = resolvePayloadClass(selector);
    return this.payloadEntries.filter((entry) => {
      if (entry.id !== id)
        return false;
      return this.decodePayloadEntry(id, entry.blob) !== null;
    }).length;
  }
  // query
  contains(selector) {
    return this.count(selector) > 0;
  }
  // access
  decodePayloadEntry(id, blob) {
    const info = _TYPE_REGISTRY.get(id);
    if (!info)
      return null;
    const blobCopy = new Uint8Array(blob);
    if (info.verify && !info.verify(blobCopy))
      return null;
    try {
      return info.decode(blobCopy);
    } catch {
      return null;
    }
  }
  valueAtId(id, index) {
    if (!this.validEnvelope)
      return null;
    let seen = 0;
    for (const entry of this.payloadEntries) {
      if (entry.id !== id)
        continue;
      const value = this.decodePayloadEntry(id, entry.blob);
      if (value === null)
        continue;
      if (seen === index)
        return value;
      seen += 1;
    }
    return null;
  }
  externalValueAtId(id, index) {
    if (!this.validEnvelope)
      return null;
    let seen = 0;
    for (const entry of this.payloadEntries) {
      if (entry.id !== id)
        continue;
      if (seen === index)
        return new Uint8Array(entry.blob);
      seen += 1;
    }
    return null;
  }
  get(selector, index = 0) {
    if (is_external_key(selector)) {
      return this.externalValueAtId(resolveExternalKey(selector), index);
    }
    const id = resolvePayloadClass(selector);
    return this.valueAtId(id, index);
  }
  *for_each(selector) {
    if (is_external_key(selector)) {
      const id2 = resolveExternalKey(selector);
      if (!this.validEnvelope)
        return;
      for (const entry of this.payloadEntries) {
        if (entry.id === id2)
          yield new Uint8Array(entry.blob);
      }
      return;
    }
    const id = resolvePayloadClass(selector);
    for (const entry of this.payloadEntries) {
      if (entry.id !== id)
        continue;
      const value = this.decodePayloadEntry(id, entry.blob);
      if (value !== null)
        yield value;
    }
  }
  add(value, blob) {
    if (!this.validEnvelope) {
      throw new Error(`cannot add to invalid ${SDK_NAME} envelope: ${this.errorMessage ?? "unknown error"}`);
    }
    if (is_external_key(value)) {
      if (blob === void 0) {
        throw new Error(`${SDK_NAME} external add requires a byte buffer`);
      }
      this.payloadEntries.push({
        id: resolveExternalKey(value),
        blob: copyBytes(blob)
      });
      return;
    }
    if (blob !== void 0) {
      throw new Error(`${SDK_NAME} external add requires ExternalKey; call external_key(...) first`);
    }
    const id = resolveNativePayload(value);
    this.payloadEntries.push({
      id,
      blob: packNativePayload(id, value)
    });
  }
  serialize() {
    if (!this.validEnvelope) {
      throw new Error(`cannot serialize invalid ${SDK_NAME} envelope: ${this.errorMessage ?? "unknown error"}`);
    }
    const builder = new Builder(1024);
    const payloadOffsets = this.payloadEntries.map((entry) => {
      const blobOffset = WirePayload.createBlobVector(builder, entry.blob);
      return WirePayload.createWirePayload(builder, entry.id, blobOffset);
    });
    const payloadsOffset = WireEnvelope.createPayloadsVector(builder, payloadOffsets);
    const producerNameOffset = builder.createString(SDK_NAME);
    const producerVersionOffset = builder.createString(SDK_VERSION);
    const producerSchemaDigestOffset = builder.createString(SCHEMA_SET_SHA256);
    const envelopeOffset = WireEnvelope.createWireEnvelope(builder, payloadsOffset, producerNameOffset, producerVersionOffset, producerSchemaDigestOffset);
    WireEnvelope.finishWireEnvelopeBuffer(builder, envelopeOffset);
    return builder.asUint8Array();
  }
};

// development/web/src/frame-results.js
var FRAME_RESULTS_ENCODING = "perception-frame-results+base64";
var FrameResultsDecodeError = class extends Error {
};
function text(value) {
  if (value === null || value === void 0) return "";
  if (value instanceof Uint8Array) return new TextDecoder().decode(value);
  return String(value);
}
function identifier(value) {
  return typeof value === "bigint" ? value.toString() : String(value || 0);
}
function objectData(object) {
  return {
    uuid: identifier(object?.id),
    parentUuid: identifier(object?.parentId),
    creationTsNs: identifier(object?.creationTsNs)
  };
}
function layerData(layer2) {
  const producer = layer2?.producer;
  return {
    engine: text(layer2?.engine),
    model: text(layer2?.model),
    tags: text(layer2?.tags),
    inferElementId: text(layer2?.inferElementId),
    labelFamily: text(layer2?.labelFamily),
    contentType: text(layer2?.contentType),
    compositingMode: text(layer2?.compositingMode),
    producer: producer ? {
      instanceId: text(producer.instanceId),
      component: text(producer.component),
      implementation: text(producer.implementation)
    } : null
  };
}
function layer(payload, payloadType, detections) {
  return {
    ...layerData(payload.layer),
    payloadType,
    payload,
    count: detections.length,
    detections
  };
}
function boxData(item) {
  return {
    ...objectData(item.object),
    x: Number(item.box?.x || 0),
    y: Number(item.box?.y || 0),
    width: Number(item.box?.width || 0),
    height: Number(item.box?.height || 0),
    confidence: Number(item.confidence || 0),
    classId: Number(item.classId ?? -1),
    text: text(item.text)
  };
}
function candidateData(candidate) {
  return {
    confidence: Number(candidate.confidence || 0),
    classId: Number(candidate.classId ?? -1),
    text: text(candidate.text),
    x: Number(candidate.x || 0),
    y: Number(candidate.y || 0),
    w: Number(candidate.w || 0),
    h: Number(candidate.h || 0)
  };
}
function addPayloadLayers(envelope, payloadType, name, mapper, layers) {
  for (const payload of envelope.for_each(payloadType)) {
    layers.push(layer(payload, name, mapper(payload)));
  }
}
function decodeBase64(value) {
  if (typeof value !== "string" || !value || value.length % 4 !== 0) {
    throw new FrameResultsDecodeError("missing or invalid frame_results_packet_b64");
  }
  if (!/^[A-Za-z0-9+/]*={0,2}$/.test(value)) {
    throw new FrameResultsDecodeError("invalid frame_results_packet_b64");
  }
  let decoded;
  try {
    decoded = atob(value);
  } catch (error) {
    throw new FrameResultsDecodeError("invalid frame_results_packet_b64", { cause: error });
  }
  return Uint8Array.from(decoded, (character) => character.charCodeAt(0));
}
function decodeFrameResultsMessage(message) {
  if (message?.frame_results_encoding !== FRAME_RESULTS_ENCODING) {
    throw new FrameResultsDecodeError(
      `unsupported frame_results_encoding: ${String(message?.frame_results_encoding)}`
    );
  }
  const envelope = new Envelope(decodeBase64(message.frame_results_packet_b64));
  if (!envelope.valid()) {
    throw new FrameResultsDecodeError(
      `invalid frame results packet: ${envelope.error() || "unknown error"}`
    );
  }
  const producerIdentity = envelope.producerIdentity();
  if (producerIdentity !== ProducerIdentityStatus.ExactMatch) {
    throw new FrameResultsDecodeError(
      `incompatible frame results producer identity: ${producerIdentity}`
    );
  }
  const layers = [];
  const perfdata = [];
  addPayloadLayers(envelope, FrameContextT, "FrameContext", (payload) => {
    const detections = [];
    if (payload.video) {
      detections.push({
        type: "VideoFrame",
        data: {
          ...objectData(payload.video.object),
          originalWidth: Number(payload.video.originalWidth),
          originalHeight: Number(payload.video.originalHeight),
          sourceCropLeft: Number(payload.video.sourceCropLeft),
          sourceCropRight: Number(payload.video.sourceCropRight),
          sourceCropTop: Number(payload.video.sourceCropTop),
          sourceCropBottom: Number(payload.video.sourceCropBottom),
          letterboxLeft: Number(payload.video.letterboxLeft),
          letterboxRight: Number(payload.video.letterboxRight),
          letterboxTop: Number(payload.video.letterboxTop),
          letterboxBottom: Number(payload.video.letterboxBottom)
        }
      });
    }
    return detections;
  }, layers);
  addPayloadLayers(envelope, BoxDetectionsT, "BoxDetections", (payload) => payload.detections.map((item) => ({ type: "Rect", data: boxData(item) })), layers);
  addPayloadLayers(envelope, ObjectTracksT, "ObjectTracks", (payload) => payload.tracks.map((item) => ({
    type: "Rect",
    data: {
      ...boxData(item),
      sourceId: identifier(item.sourceId),
      trackId: identifier(item.trackId),
      diagnostic: text(item.diagnostic),
      predictedOnly: Boolean(item.predictedOnly)
    }
  })), layers);
  addPayloadLayers(envelope, ClassificationsT, "Classifications", (payload) => [
    ...payload.classifications.map((item) => ({
      type: "Classification",
      data: {
        ...objectData(item.object),
        candidates: item.candidates.map(candidateData)
      }
    })),
    ...payload.personPresence.map((item) => ({
      type: "PersonClassification",
      data: {
        ...objectData(item.object),
        yesConfidence: Number(item.yesConfidence || 0),
        noConfidence: Number(item.noConfidence || 0)
      }
    }))
  ], layers);
  addPayloadLayers(envelope, PoseEstimationsT, "PoseEstimations", (payload) => payload.poses.map((item) => ({
    type: "YawPitch",
    data: {
      ...objectData(item.object),
      confidence: Number(item.confidence || 0),
      yaw: Number(item.yaw || 0),
      pitch: Number(item.pitch || 0)
    }
  })), layers);
  addPayloadLayers(envelope, TrackTracesT, "TrackTraces", (payload) => payload.traces.map((item) => ({
    type: "TrackTrace",
    data: {
      ...objectData(item.object),
      trackId: identifier(item.trackId),
      points: item.points.map((point) => ({ x: Number(point.x), y: Number(point.y) }))
    }
  })), layers);
  addPayloadLayers(envelope, SegmentationMasksT, "SegmentationMasks", (payload) => payload.masks.map((item) => ({
    type: "SegmentationMask",
    data: {
      ...objectData(item.object),
      width: Number(item.bitmap?.width || 0),
      height: Number(item.bitmap?.height || 0),
      valueType: text(item.bitmap?.valueType),
      pixels: item.bitmap ? Uint8Array.from(item.bitmap.pixels) : void 0
    }
  })), layers);
  addPayloadLayers(envelope, ObjectEmbeddingsT, "ObjectEmbeddings", (payload) => payload.embeddings.map((item) => ({
    type: "ObjectEmbedding",
    data: { ...objectData(item.object), values: [...item.values] }
  })), layers);
  for (const payload of envelope.for_each(PerformanceOverlayT)) {
    perfdata.push(...payload.lines.map(text));
    layers.push(layer(payload, "PerformanceOverlay", payload.lines.map((line) => ({
      type: "PerformanceLine",
      data: { text: text(line) }
    }))));
  }
  return {
    frame_counter: Number.isInteger(message.frame_counter) ? message.frame_counter : void 0,
    frame_results: { layers, perfdata }
  };
}

// development/web/src/metadata-ws.js
var WS_PROTO2 = location.protocol === "https:" ? "wss" : "ws";
var WS_HOST2 = location.hostname;
var DEFAULT_METADATA_PORT = 8002;
var RECONNECT_DELAY_MS = 1500;
var RECONNECT_DELAY_MAX_MS = 15e3;
var BACKOFF_FACTOR = 1.2;
function metadataUrl() {
  const config = window.OPK_CONFIG || {};
  if (config.metadataUrl) {
    return config.metadataUrl;
  }
  const port = config.metadataPort || DEFAULT_METADATA_PORT;
  const endpoint = config.metadataEndpoint || "/ws";
  return `${WS_PROTO2}://${WS_HOST2}:${port}${endpoint}`;
}
function normaliseInferenceOutput(frameResults) {
  const layers = Array.isArray(frameResults?.layers) ? frameResults.layers : [];
  return {
    layers: layers.map((layer2) => {
      const detections = Array.isArray(layer2?.detections) ? layer2.detections : [];
      return {
        ...layer2,
        count: Number.isFinite(layer2?.count) ? layer2.count : detections.length
      };
    })
  };
}
function normalisePerformance(frameResults) {
  return {
    lines: Array.isArray(frameResults?.perfdata) ? frameResults.perfdata : []
  };
}
function publishMetadata(frameCounter, frameResults, decodeError = null) {
  window.dispatchEvent(new CustomEvent("metadata-message", {
    detail: {
      frame_counter: Number.isInteger(frameCounter) ? frameCounter : void 0,
      frame_results: frameResults,
      inference_output: normaliseInferenceOutput(frameResults),
      performance: normalisePerformance(frameResults),
      decode_error: decodeError
    }
  }));
}
function handleMetadataMessage(raw) {
  let message;
  try {
    message = JSON.parse(String(raw).trim());
  } catch (error) {
    console.warn("Invalid metadata WebSocket message", error);
    publishMetadata(void 0, null, "Invalid metadata JSON");
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
    console.warn("Invalid FrameResults metadata message", error);
    publishMetadata(message?.frame_counter, null, error instanceof Error ? error.message : String(error));
    return;
  }
  publishMetadata(decoded.frame_counter, decoded.frame_results);
}
window.addEventListener("video-frame-metadata", (event) => {
  let frame = null;
  if (event.detail) {
    try {
      frame = decodeFrameResultsMessage(event.detail).frame_results;
    } catch (error) {
      console.warn("Invalid video frame metadata", error);
    }
  }
  window.dispatchEvent(new CustomEvent("video-frame-results", { detail: frame }));
});
var socket = null;
var reconnectDelay = RECONNECT_DELAY_MS;
function connect() {
  if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
    return;
  }
  socket = new WebSocket(metadataUrl());
  socket.onopen = () => {
    reconnectDelay = RECONNECT_DELAY_MS;
    window.dispatchEvent(new CustomEvent("metadata-connection", { detail: { connected: true } }));
  };
  socket.onmessage = (event) => {
    handleMetadataMessage(event.data);
  };
  socket.onerror = () => {
    ;
  };
  socket.onclose = () => {
    window.dispatchEvent(new CustomEvent("metadata-connection", { detail: { connected: false } }));
    window.setTimeout(() => {
      reconnectDelay = Math.min(reconnectDelay * BACKOFF_FACTOR, RECONNECT_DELAY_MAX_MS);
      connect();
    }, reconnectDelay);
  };
}
connect();

// development/web/src/overlay-drawing.js
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
  for (const layer2 of perception?.layers || []) {
    for (const detection of layer2.detections || []) {
      if (detection?.type === "VideoFrame" && detection.data) {
        return detection.data;
      }
    }
  }
  return null;
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
function drawFace(ctx, rect, mapper, color) {
  const box = mapper.rect(rect);
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
  const radius = Math.abs(box.width) / 2;
  ctx.save();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, Math.PI * 2);
  ctx.stroke();
  ctx.restore();
}
function drawClassification(ctx, classification, display, color, heading, alignRight = false, bottomOffset = 0) {
  const candidates = Array.isArray(classification?.candidates) ? classification.candidates : [];
  if (candidates.length === 0) {
    return 0;
  }
  const fontSize = 14;
  const lineHeight = fontSize * 1.5;
  const padding = 10;
  const startY = display.y + display.height - bottomOffset - candidates.length * lineHeight - 2 * padding;
  const textX = classificationTextX(display, padding, alignRight);
  if (heading) {
    drawTextChip(
      ctx,
      heading,
      textX,
      Math.max(display.y + padding, startY - lineHeight),
      fontSize,
      color,
      alignRight
    );
  }
  candidates.forEach((candidate, index) => {
    const text2 = `#${index + 1}: ${candidate.text || candidate.classId} (${((candidate.confidence || 0) * 100).toFixed(1)}%)`;
    drawTextChip(
      ctx,
      text2,
      textX,
      startY + index * lineHeight,
      fontSize,
      color,
      alignRight
    );
  });
  return classificationPanelHeight(candidates.length, heading);
}
function classificationHeading(layer2) {
  return layer2?.producer?.implementation || "";
}
function classificationTextX(display, padding, alignRight) {
  if (!alignRight) {
    return display.x + padding;
  }
  return display.x + display.width - padding;
}
function classificationPanelHeight(candidateCount, heading) {
  const fontSize = 14;
  const lineHeight = fontSize * 1.5;
  const padding = 10;
  return candidateCount * lineHeight + (heading ? lineHeight : 0) + 2 * padding;
}
function drawPersonClassification(ctx, personClassification, display, now, color) {
  if (now % 1e3 >= 800) {
    return;
  }
  const isPerson = Number(personClassification?.yesConfidence || 0) > Number(personClassification?.noConfidence || 0);
  const label = isPerson ? "PERSON" : "NON-PERSON";
  const fontSize = Math.max(28, Math.min(64, display.width / 12));
  ctx.save();
  ctx.font = `700 ${fontSize}px monospace`;
  const metrics = ctx.measureText(label);
  const x = display.x + (display.width - metrics.width) / 2;
  const y = display.y + display.height / 2;
  drawTextChip(ctx, label, x, y, fontSize, color);
  ctx.restore();
}
function drawTrackTrace(ctx, trace, mapper, color) {
  const points = Array.isArray(trace?.points) ? trace.points : [];
  if (points.length < 2) {
    return;
  }
  const segments = points.length - 1;
  for (let i = 0; i < segments; i += 1) {
    const from = mapper.point(points[i].x, points[i].y);
    const to = mapper.point(points[i + 1].x, points[i + 1].y);
    const alpha = 0.35 + 0.65 * ((i + 1) / segments);
    drawArrow(ctx, from, to, hexToRgba(color, alpha), 4, 0);
  }
}
function drawGazeVector(ctx, yp, parent, mapper, color) {
  if (nearZero(yp.yaw) && nearZero(yp.pitch)) return;
  const parentBox = mapper.rect(parent);
  const start = { x: parentBox.x + parentBox.width / 2, y: parentBox.y + parentBox.height / 2 };
  drawArrow(ctx, start, gazeEndpoint(start, yp.yaw, yp.pitch, 120), color, 3, 10);
}
function drawCameraContactMarker(ctx, classification, face, mapper, color) {
  const candidate = classification?.candidates?.[0];
  if (!candidate || !face) {
    return;
  }
  const box = mapper.rect(face);
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
  const hasContact = candidate.classId === 1;
  const radiusBase = Math.min(Math.abs(box.width), Math.abs(box.height)) * 0.5;
  const radius = hasContact ? clamp2(radiusBase * 0.65, 18, 80) : clamp2(radiusBase * 1.15, 28, 140);
  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = hasContact ? 5 : 8;
  ctx.setLineDash(hasContact ? [] : [6, 4]);
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, Math.PI * 2);
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(cx, cy, hasContact ? 5 : 7, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}
function drawTextChip(ctx, text2, x, y, fontSize, color = "#ffffff", alignRight = false) {
  ctx.save();
  ctx.font = `${fontSize}px monospace`;
  ctx.textBaseline = "top";
  const paddingX = 4;
  const paddingY = 2;
  const metrics = ctx.measureText(text2);
  const width = metrics.width + paddingX * 2;
  const height = fontSize + paddingY * 2;
  const drawX = alignRight ? x - width : x;
  ctx.fillStyle = "rgba(0, 0, 0, 0.78)";
  ctx.fillRect(drawX, y, width, height);
  ctx.fillStyle = color;
  ctx.fillText(text2, drawX + paddingX, y + paddingY);
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
  return {
    x: start.x - Math.sin(yaw) * Math.cos(pitch) * lengthPx,
    y: start.y - Math.sin(pitch) * lengthPx
  };
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

// development/web/src/overlay-sources.js
var SOURCE_FIELDS = [
  "payloadType",
  "contentType",
  "engine",
  "model",
  "inferElementId",
  "labelFamily",
  "producerInstanceId",
  "producerComponent",
  "producerImplementation"
];
var SOURCE_FIELD_LABELS = {
  payloadType: "Payload type",
  contentType: "Content type",
  engine: "Engine",
  model: "Model",
  inferElementId: "Inference element",
  labelFamily: "Label family",
  producerInstanceId: "Producer instance",
  producerComponent: "Component",
  producerImplementation: "Implementation"
};
function outputSource(layer2) {
  return {
    payloadType: layer2.payloadType || "",
    contentType: layer2.contentType || "",
    engine: layer2.engine || "",
    model: layer2.model || "",
    inferElementId: layer2.inferElementId || "",
    labelFamily: layer2.labelFamily || "",
    producerInstanceId: layer2.producer?.instanceId || "",
    producerComponent: layer2.producer?.component || "",
    producerImplementation: layer2.producer?.implementation || ""
  };
}
function sourceKey(source) {
  return JSON.stringify(SOURCE_FIELDS.map((field) => source[field]));
}
function validSource(source) {
  return SOURCE_FIELDS.every((field) => typeof source?.[field] === "string") && source.payloadType.length > 0;
}
function producerKey(source) {
  return source.producerInstanceId ? JSON.stringify([source.producerComponent, source.producerInstanceId]) : null;
}
function objectId(value) {
  return value == null || String(value) === "0" ? "" : String(value);
}
function trackKey(source, id) {
  const producer = producerKey(source);
  return producer && objectId(id) ? JSON.stringify([producer, objectId(id)]) : null;
}
function sourceLabel(source) {
  if (source.payloadType === "PerformanceOverlay") return "Pipeline performance";
  return source.model || source.producerImplementation || source.producerInstanceId || source.inferElementId || source.producerComponent || "Unspecified source";
}
function sourceDetails(source) {
  return SOURCE_FIELDS.map((field) => [SOURCE_FIELD_LABELS[field], source[field] || "Not provided"]);
}
function sourceLabels(sources, groupByPayload = true) {
  const groups = /* @__PURE__ */ new Map();
  for (const source of new Map([...sources].map((source2) => [sourceKey(source2), source2])).values()) {
    const groupKey = JSON.stringify([groupByPayload ? source.payloadType : "", sourceLabel(source)]);
    if (!groups.has(groupKey)) groups.set(groupKey, []);
    groups.get(groupKey).push(source);
  }
  const labels = /* @__PURE__ */ new Map();
  for (const group of groups.values()) {
    if (group.length === 1) {
      labels.set(sourceKey(group[0]), sourceLabel(group[0]));
      continue;
    }
    const fields = SOURCE_FIELDS.filter((field) => new Set(group.map((source) => source[field])).size > 1);
    const qualify = (source, selected) => selected.map((field) => source[field] || `${SOURCE_FIELD_LABELS[field]} not provided`).join(" \xB7 ");
    const choices = [[]];
    for (const field of fields) choices.push(...choices.map((choice2) => [...choice2, field]));
    const length = (choice2) => group.reduce((total, source) => total + qualify(source, choice2).length, 0);
    choices.sort((a, b) => a.length - b.length || length(a) - length(b));
    const choice = choices.find((candidate) => new Set(group.map((source) => qualify(source, candidate))).size === group.length);
    for (const source of group) {
      const qualifier = choice ? qualify(source, choice) : JSON.stringify(Object.fromEntries(fields.map((field) => [SOURCE_FIELD_LABELS[field], source[field]])));
      labels.set(sourceKey(source), `${sourceLabel(source)} (${qualifier})`);
    }
  }
  return labels;
}
function collectOutputs(frame) {
  const outputs = /* @__PURE__ */ new Map();
  for (const layer2 of frame?.layers || []) {
    if (!layer2.payloadType || layer2.payloadType === "FrameContext") continue;
    const source = outputSource(layer2);
    const key = sourceKey(source);
    if (!outputs.has(key)) outputs.set(key, { key, source, layers: [] });
    outputs.get(key).layers.push(layer2);
  }
  return outputs;
}
function append(index, key, value) {
  if (!key) return;
  if (!index.has(key)) index.set(key, []);
  index.get(key).push(value);
}
function indexFrame(frame) {
  const outputs = collectOutputs(frame);
  const index = { frame, outputs, byPayload: /* @__PURE__ */ new Map(), byProducer: /* @__PURE__ */ new Map(), objects: /* @__PURE__ */ new Map(), byTrack: /* @__PURE__ */ new Map() };
  for (const output of outputs.values()) {
    append(index.byPayload, output.source.payloadType, output);
    append(index.byProducer, producerKey(output.source), output);
    output.items = [];
    for (const layer2 of output.layers) {
      for (const item of layer2.detections || []) {
        if (!item?.data) continue;
        const entry = { ...item, layer: layer2, source: output.source };
        output.items.push(entry);
        append(index.objects, objectId(item.data.uuid), entry);
        append(index.byTrack, trackKey(output.source, item.data.trackId), entry);
      }
    }
  }
  return index;
}

// development/web/src/overlay-image.js
function validLocalImage(value) {
  return typeof value === "string" && (value === "" || value.length <= 2e6 && /^data:image\/(png|jpeg|webp);base64,[A-Za-z0-9+/]+={0,2}$/.test(value));
}
async function loadLocalImage(file) {
  if (!["image/png", "image/jpeg", "image/webp"].includes(file.type) || file.size > 10 * 1024 * 1024) {
    throw new Error("Choose a PNG, JPEG or WebP image up to 10 MiB.");
  }
  let image;
  try {
    image = await createImageBitmap(file);
    if (!image.width || !image.height || image.width * image.height > 4e7) {
      throw new Error("Invalid image dimensions");
    }
    const scale = Math.min(1, 1920 / Math.max(image.width, image.height));
    const canvas2 = document.createElement("canvas");
    canvas2.width = Math.max(1, Math.round(image.width * scale));
    canvas2.height = Math.max(1, Math.round(image.height * scale));
    canvas2.getContext("2d").drawImage(image, 0, 0, canvas2.width, canvas2.height);
    const data = canvas2.toDataURL("image/webp", 0.85);
    if (!validLocalImage(data)) throw new Error("Image is too large to save");
    return data;
  } catch {
    throw new Error("Could not prepare this image. Choose a smaller, valid raster image.");
  } finally {
    image?.close();
  }
}

// development/web/src/overlay-feature.js
var STYLE_CONTROLS = [
  { key: "color", label: "Color", type: "color", value: "#ffffff" },
  { key: "opacity", label: "Opacity", type: "range", min: 0, max: 1, step: 0.01, value: 1 }
];
var CONTENT_COLORS = {
  humanFace: "#2600ff",
  genericObject: "#ff3030",
  eyeYawPitch: "#fafad2",
  cameraContact: "#66ff00",
  personClassification: "#66ff00",
  trackTrace: "#ffff00"
};
function defineFeature({ controls = STYLE_CONTROLS, defaults = {}, resolve: resolve2 = (result) => result, reason, ...definition }) {
  return {
    ...definition,
    controls,
    defaults(source) {
      const settings = Object.fromEntries(controls.map((control) => [control.key, control.value]));
      if (Object.hasOwn(settings, "color")) settings.color = CONTENT_COLORS[source.contentType] || settings.color;
      return {
        ...settings,
        ...typeof defaults === "function" ? defaults(source) : defaults
      };
    },
    discover(catalog) {
      return [...catalog.values()].filter(definition.accepts);
    },
    validateSettings(settings) {
      if (!settings || typeof settings !== "object" || Array.isArray(settings)) return null;
      const result = {};
      for (const control of controls) {
        const value = Object.hasOwn(settings, control.key) ? settings[control.key] : control.value;
        const valid = control.type === "checkbox" ? typeof value === "boolean" : control.type === "color" ? typeof value === "string" && /^#[0-9a-f]{6}$/i.test(value) : control.type === "select" ? control.options.some((option) => option.value === value) : control.type === "image" ? validLocalImage(value) : Number.isFinite(value) && value >= control.min && value <= control.max;
        if (!valid) return null;
        result[control.key] = value;
      }
      return result;
    },
    resolve(index, selection, settings) {
      if (!selection?.primary || !definition.accepts(selection.primary)) {
        return { state: "unsupported", message: "This source cannot provide the selected feature." };
      }
      if (reason) return { state: "unsupported", message: reason };
      const output = index.outputs.get(sourceKey(selection.primary));
      if (!output) return { state: "waiting", message: "Waiting for source" };
      return resolve2({ state: "ready", message: "", output, items: output.items }, index, settings);
    }
  };
}
function resolveFaceParents(result, index, type) {
  const items = result.items.filter((item) => item.type === type);
  const resolved = { ...result, items, attached: [] };
  let missing = false;
  let ambiguous = false;
  for (const item of items) {
    const candidates = (index.objects.get(objectId(item.data.parentUuid)) || []).filter((candidate) => ["BoxDetections", "ObjectTracks"].includes(candidate.source.payloadType) && candidate.source.contentType === "humanFace" && candidate.type === "Rect");
    if (candidates.length > 1) ambiguous = true;
    else if (candidates.length === 1) resolved.attached.push({ data: item.data, parent: candidates[0].data });
    else missing = true;
  }
  if (ambiguous) return { ...resolved, state: "ambiguous", message: "Multiple parent faces match; ambiguous markers are omitted." };
  if (missing) return { ...resolved, state: "partial", message: "Waiting for parent face geometry." };
  return resolved;
}

// development/web/src/features/bounding-boxes.js
var bounding_boxes_default = defineFeature({
  id: "bounding-boxes",
  label: "Bounding boxes",
  accepts: (source) => ["BoxDetections", "ObjectTracks"].includes(source.payloadType) && source.contentType !== "humanFace",
  render({ ctx, mapper }, resolved, settings) {
    for (const { type, data } of resolved.items) {
      if (type === "Rect") drawObjectBox(ctx, data, mapper, settings.color);
    }
  }
});

// development/web/src/features/face-circles.js
var face_circles_default = defineFeature({
  id: "face-circles",
  label: "Face circles",
  accepts: (source) => ["BoxDetections", "ObjectTracks"].includes(source.payloadType) && source.contentType === "humanFace",
  render({ ctx, mapper }, resolved, settings) {
    for (const { type, data } of resolved.items) {
      if (type === "Rect") drawFace(ctx, data, mapper, settings.color);
    }
  }
});

// development/web/src/features/tracked-objects.js
var tracked_objects_default = defineFeature({
  id: "tracked-objects",
  label: "Tracked objects",
  accepts: (source) => source.payloadType === "ObjectTracks",
  controls: [
    { key: "boxes", label: "Boxes", type: "checkbox", value: true },
    { key: "trails", label: "Trails", type: "checkbox", value: true },
    ...STYLE_CONTROLS
  ],
  resolve(result, index, settings) {
    const items = result.items.filter((item) => item.type === "Rect");
    const resolved = { ...result, items, trails: [] };
    if (!settings.trails || !items.length) return resolved;
    const source = result.output.source;
    if (!producerKey(source)) return { ...resolved, state: "partial", message: "Trail producer identity is missing." };
    let missing = false;
    let ambiguous = false;
    for (const id of new Set(items.map((item) => objectId(item.data.trackId)))) {
      const candidates = (index.byTrack.get(trackKey(source, id)) || []).filter((item) => item.source.payloadType === "TrackTraces" && item.source.contentType === "trackTrace" && item.type === "TrackTrace");
      if (candidates.length > 1) ambiguous = true;
      else if (candidates.length === 1 && candidates[0].data.points?.length >= 2) resolved.trails.push(candidates[0].data);
      else missing = true;
    }
    if (ambiguous) return { ...resolved, state: "ambiguous", message: "Multiple trails match a track; ambiguous trails are omitted." };
    if (missing) return { ...resolved, state: "partial", message: "Waiting for matching trail history." };
    return resolved;
  },
  render({ ctx, mapper }, resolved, settings) {
    if (settings.trails) {
      for (const trace of resolved.trails) drawTrackTrace(ctx, trace, mapper, settings.color);
    }
    if (settings.boxes) {
      const draw = resolved.output.source.contentType === "humanFace" ? drawFace : drawObjectBox;
      for (const { data } of resolved.items) draw(ctx, data, mapper, settings.color);
    }
  }
});

// development/web/src/features/movement-trails.js
var movement_trails_default = defineFeature({
  id: "movement-trails",
  label: "Movement trails",
  accepts: (source) => source.payloadType === "TrackTraces",
  render({ ctx, mapper }, resolved, settings) {
    for (const item of resolved.items) {
      if (item.type === "TrackTrace") drawTrackTrace(ctx, item.data, mapper, settings.color);
    }
  }
});

// development/web/src/features/classifications.js
var classifications_default = defineFeature({
  id: "classifications",
  label: "Classifications",
  accepts: (source) => source.payloadType === "Classifications" && source.contentType !== "cameraContact",
  render({ ctx, mapper, layout, now }, resolved, settings) {
    for (const { type, data, layer: layer2 } of resolved.items) {
      if (type === "PersonClassification") drawPersonClassification(ctx, data, mapper.display, now, settings.color);
      if (type !== "Classification") continue;
      const right = layer2.compositingMode === "bottomRight";
      const side = right ? "right" : "left";
      layout[side] += drawClassification(
        ctx,
        data,
        mapper.display,
        settings.color,
        classificationHeading(layer2),
        right,
        layout[side]
      );
    }
  }
});

// development/web/src/features/gaze.js
var gaze_default = defineFeature({
  id: "gaze",
  label: "Gaze",
  accepts: (source) => source.payloadType === "PoseEstimations" && source.contentType === "eyeYawPitch",
  resolve: (result, index) => resolveFaceParents(result, index, "YawPitch"),
  render({ ctx, mapper }, resolved, settings) {
    for (const { data, parent } of resolved.attached) drawGazeVector(ctx, data, parent, mapper, settings.color);
  }
});

// development/web/src/features/camera-contact.js
var camera_contact_default = defineFeature({
  id: "camera-contact",
  label: "Camera contact",
  accepts: (source) => source.payloadType === "Classifications" && source.contentType === "cameraContact",
  resolve: (result, index) => resolveFaceParents(result, index, "Classification"),
  render({ ctx, mapper }, resolved, settings) {
    for (const { data, parent } of resolved.attached) drawCameraContactMarker(ctx, data, parent, mapper, settings.color);
  }
});

// development/web/src/features/performance.js
var performance_default = defineFeature({
  id: "performance",
  label: "Performance text",
  accepts: (source) => source.payloadType === "PerformanceOverlay",
  render({ ctx, layout }, resolved, settings) {
    for (const item of resolved.items) {
      if (item.type !== "PerformanceLine") continue;
      drawTextChip(ctx, item.data.text, 10, layout.performanceY, 16, settings.color);
      layout.performanceY += 20;
    }
  }
});

// development/web/src/features/background-replacement.js
var resources = /* @__PURE__ */ new WeakMap();
function resolve(result, index) {
  const masks = result.items.filter((item) => item.type === "SegmentationMask");
  if (result.output.layers.some((layer2) => layer2.compositingMode !== "backgroundReplacement")) {
    return { ...result, state: "unsupported", message: "Requires a background-opacity mask." };
  }
  if (masks.length > 1) return { ...result, items: [], state: "ambiguous", message: "Expected one full-frame mask." };
  if (!masks.length) return { ...result, items: [] };
  const { width, height, valueType, pixels, parentUuid } = masks[0].data;
  if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0 || width > 8192 || height > 8192 || width * height > 16777216 || valueType !== "Uint8" || !(pixels instanceof Uint8Array) || pixels.length !== width * height) {
    return { ...result, items: [], state: "unsupported", message: "Invalid or oversized Uint8 mask." };
  }
  if (objectId(parentUuid)) {
    const parents = index.frame.layers.flatMap((layer2) => layer2.detections || []).filter((item) => objectId(item.data?.uuid) === objectId(parentUuid));
    if (parents.length !== 1 || parents[0].type !== "VideoFrame") {
      return { ...result, items: [], state: "partial", message: "Waiting for unambiguous full-frame mask geometry." };
    }
  }
  return { ...result, items: masks };
}
function render({ ctx, canvas: canvas2, mapper, overlay: overlay2 }, resolved, settings) {
  if (!resolved.items.length) return;
  const mask = resolved.items[0].data;
  let cache = resources.get(overlay2);
  if (!cache) {
    cache = { mask: document.createElement("canvas"), composite: document.createElement("canvas") };
    resources.set(overlay2, cache);
  }
  if (cache.pixels !== mask.pixels) {
    cache.mask.width = mask.width;
    cache.mask.height = mask.height;
    const maskContext = cache.mask.getContext("2d");
    const data = maskContext.createImageData(mask.width, mask.height);
    for (let i = 0; i < mask.pixels.length; i++) data.data[i * 4 + 3] = mask.pixels[i];
    maskContext.putImageData(data, 0, 0);
    cache.pixels = mask.pixels;
  }
  const imageSource = settings.background === "image" ? settings.image : "";
  if (cache.imageSource !== imageSource) {
    cache.imageSource = imageSource;
    cache.image = imageSource ? new Image() : null;
    if (cache.image) cache.image.src = imageSource;
  }
  const { x, y, width, height } = mapper.display;
  const dpr = canvas2.width / Math.max(1, canvas2.clientWidth);
  const w = Math.max(1, Math.round(width * dpr));
  const h = Math.max(1, Math.round(height * dpr));
  if (cache.composite.width !== w || cache.composite.height !== h) {
    cache.composite.width = w;
    cache.composite.height = h;
  }
  const composite = cache.composite.getContext("2d");
  composite.globalCompositeOperation = "source-over";
  composite.clearRect(0, 0, w, h);
  composite.fillStyle = settings.color;
  composite.fillRect(0, 0, w, h);
  if (cache.image?.complete && cache.image.naturalWidth && cache.image.naturalHeight) {
    const scale = Math.max(w / cache.image.naturalWidth, h / cache.image.naturalHeight);
    const iw = cache.image.naturalWidth * scale;
    const ih = cache.image.naturalHeight * scale;
    composite.drawImage(cache.image, (w - iw) / 2, (h - ih) / 2, iw, ih);
  }
  composite.globalCompositeOperation = "destination-in";
  composite.drawImage(cache.mask, 0, 0, w, h);
  ctx.drawImage(cache.composite, x, y, width, height);
}
var background_replacement_default = defineFeature({
  id: "background-replacement",
  label: "Background replacement",
  renderOrder: -1,
  accepts: (source) => source.payloadType === "SegmentationMasks" && source.contentType === "segmentation",
  controls: [
    { key: "background", label: "Background", type: "select", value: "color", options: [
      { value: "color", label: "Solid color" },
      { value: "image", label: "Local image" }
    ] },
    ...STYLE_CONTROLS,
    { key: "image", label: "Background image", type: "image", value: "" }
  ],
  defaults: { color: "#00ffff" },
  resolve,
  render
});

// development/web/src/overlay-features.js
var OVERLAY_FEATURES = [
  bounding_boxes_default,
  face_circles_default,
  tracked_objects_default,
  movement_trails_default,
  classifications_default,
  gaze_default,
  camera_contact_default,
  performance_default,
  background_replacement_default
];
function featureFor(id) {
  return OVERLAY_FEATURES.find((feature) => feature.id === id);
}
function defaultSettings(id, source) {
  const defaults = featureFor(id)?.defaults || { color: "#ffffff", opacity: 1 };
  return typeof defaults === "function" ? defaults(source || {}) : { ...defaults };
}
function resolveOverlay(index, overlay2) {
  const feature = featureFor(overlay2.featureId);
  if (!feature) return { state: "unsupported", message: "Unknown rendering feature." };
  if (overlay2.selection.primary === null && feature.render) return { state: "waiting", message: "Waiting for source" };
  const settings = { ...defaultSettings(feature.id, overlay2.selection.primary), ...overlay2.settings };
  return { ...feature.resolve(index, overlay2.selection, settings), feature, settings };
}
function overlayStatus(index, overlay2, catalog, resolved = resolveOverlay(index, overlay2)) {
  if (overlay2.selection.primary === null && catalog && featureFor(overlay2.featureId)?.discover(catalog).length > 1) {
    return "Choose a source";
  }
  if (resolved.state === "unsupported") return `Unsupported \xB7 ${resolved.message}`;
  if (resolved.state === "waiting") return "Waiting for source";
  if (!overlay2.enabled) return "Hidden";
  if (resolved.state === "partial") return `Partial \xB7 ${resolved.message}`;
  if (resolved.state === "ambiguous") return `Ambiguous \xB7 ${resolved.message}`;
  return resolved.items.length ? "Ready" : "Ready \xB7 no results";
}

// development/web/src/osd-renderer.js
function renderOsd(canvas2, video5, index, overlays2 = [], { now = Date.now() } = {}) {
  const dpr = window.devicePixelRatio || 1;
  const pixelWidth = Math.max(1, Math.round(canvas2.clientWidth * dpr));
  const pixelHeight = Math.max(1, Math.round(canvas2.clientHeight * dpr));
  if (canvas2.width !== pixelWidth || canvas2.height !== pixelHeight) {
    canvas2.width = pixelWidth;
    canvas2.height = pixelHeight;
  }
  const ctx = canvas2.getContext("2d");
  const width = canvas2.width / dpr;
  const height = canvas2.height / dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);
  if (!index.frame) return;
  const mapper = createCoordinateMapper({
    canvasWidth: width,
    canvasHeight: height,
    videoWidth: video5.videoWidth,
    videoHeight: video5.videoHeight,
    perception: index.frame
  });
  const target = { ctx, canvas: canvas2, video: video5, mapper, layout: { left: 0, right: 0, performanceY: 10 }, now };
  const ordered = [...overlays2].sort((a, b) => (featureFor(a.featureId)?.renderOrder ?? 0) - (featureFor(b.featureId)?.renderOrder ?? 0));
  for (const overlay2 of ordered) {
    if (!overlay2.enabled) continue;
    const resolved = resolveOverlay(index, overlay2);
    if (["waiting", "unsupported"].includes(resolved.state) || resolved.settings.opacity <= 0) continue;
    ctx.save();
    try {
      ctx.globalAlpha = resolved.settings.opacity ?? 1;
      resolved.feature.render({ ...target, overlay: overlay2 }, resolved, resolved.settings);
    } finally {
      ctx.restore();
    }
  }
}

// development/web/src/overlays.js
var OVERLAY_STORAGE_KEY = "opk-overlays:v2";
var LEGACY_STORAGE_KEY = "opk-overlays:v1";
function legacyFeature(source) {
  if (source.payloadType === "Classifications" && source.contentType === "cameraContact") return "camera-contact";
  return {
    BoxDetections: "bounding-boxes",
    ObjectTracks: "tracked-objects",
    TrackTraces: "movement-trails",
    Classifications: "classifications",
    PoseEstimations: "gaze",
    PerformanceOverlay: "performance"
  }[source.payloadType] || "unsupported";
}
function validLegacy(overlay2) {
  return validSource(overlay2?.source) && typeof overlay2.enabled === "boolean" && typeof overlay2.settings?.color === "string" && /^#[0-9a-f]{6}$/i.test(overlay2.settings.color) && Number.isFinite(overlay2.settings?.opacity) && overlay2.settings.opacity >= 0 && overlay2.settings.opacity <= 1;
}
function migrateLegacy(overlay2, saved) {
  if (!validLegacy(overlay2)) return null;
  const featureId = legacyFeature(overlay2.source);
  const settings = { ...overlay2.settings };
  if (featureId === "tracked-objects") {
    const oldTraceKey = sourceKey({ ...overlay2.source, payloadType: "TrackTraces", contentType: "trackTrace" });
    settings.boxes = true;
    settings.trails = !saved.some((entry) => validLegacy(entry) && sourceKey(entry.source) === oldTraceKey);
  }
  return { featureId, selection: { primary: overlay2.source }, enabled: overlay2.enabled, settings };
}
function normalizeOverlay(overlay2) {
  const primary = overlay2?.selection?.primary;
  if (!overlay2 || typeof overlay2.featureId !== "string" || !overlay2.featureId || typeof overlay2.enabled !== "boolean" || primary !== null && !validSource(primary)) return null;
  const feature = featureFor(overlay2.featureId);
  if (!overlay2.settings || typeof overlay2.settings !== "object" || Array.isArray(overlay2.settings)) return null;
  const settings = feature ? feature.validateSettings(overlay2.settings) : overlay2.settings;
  if (!settings) return null;
  return { featureId: overlay2.featureId, selection: { primary: primary === null ? null : { ...primary } }, enabled: overlay2.enabled, settings };
}
function createOverlay(featureId, source = null) {
  return { featureId, selection: { primary: source }, enabled: true, settings: defaultSettings(featureId, source) };
}
function bindPendingOverlays(overlays2, catalog) {
  let changed = false;
  for (const overlay2 of overlays2) {
    if (overlay2.selection.primary !== null) continue;
    const feature = featureFor(overlay2.featureId);
    if (!feature?.render) continue;
    const sources = feature.discover(catalog);
    if (sources.length !== 1) continue;
    overlay2.selection.primary = sources[0];
    changed = true;
  }
  return changed;
}
function loadOverlays(storage2) {
  try {
    const current = storage2.getItem(OVERLAY_STORAGE_KEY);
    const raw = current ?? storage2.getItem(LEGACY_STORAGE_KEY);
    if (raw === null) return { overlays: [], notice: "" };
    const saved = JSON.parse(raw);
    const migrating = current === null;
    if (saved?.version !== (migrating ? 1 : 2) || !Array.isArray(saved.overlays)) throw new Error("Unsupported settings");
    const candidates = migrating ? saved.overlays.map((entry) => migrateLegacy(entry, saved.overlays)) : saved.overlays;
    const overlays2 = candidates.map(normalizeOverlay).filter(Boolean);
    let relabelled = false;
    for (const overlay2 of overlays2) {
      const source = overlay2.selection.primary;
      if (overlay2.featureId === "bounding-boxes" && source?.contentType === "humanFace" && ["BoxDetections", "ObjectTracks"].includes(source.payloadType)) {
        overlay2.featureId = "face-circles";
        relabelled = true;
      }
    }
    const notices = [];
    if (overlays2.length !== candidates.length) notices.push("Some invalid saved overlays were skipped.");
    if (migrating || relabelled) notices.push(saveOverlays(storage2, overlays2));
    return { overlays: overlays2, notice: notices.filter(Boolean).join(" ") };
  } catch {
    return { overlays: [], notice: "Saved overlays could not be loaded. You can still add overlays." };
  }
}
function saveOverlays(storage2, overlays2) {
  try {
    const saved = overlays2.map(normalizeOverlay);
    if (saved.some((overlay2) => !overlay2)) throw new Error("Invalid overlay settings");
    storage2.setItem(OVERLAY_STORAGE_KEY, JSON.stringify({ version: 2, overlays: saved }));
    return "";
  } catch {
    return "Overlay settings could not be saved. Changes apply only to this page session.";
  }
}

// development/web/src/client-osd.js
var canvas = document.getElementById("clientOsdCanvas");
var video4 = document.getElementById("video");
var toggleButton = document.getElementById("clientOsdControlsBtn");
var panel = document.getElementById("clientOsdControlsPanel");
var list = document.getElementById("overlayList");
var notice = document.getElementById("overlayNotice");
var connection = document.getElementById("overlayConnection");
var addButton = document.getElementById("addOverlayBtn");
var dialog = document.getElementById("overlayDialog");
var form = document.getElementById("overlayForm");
var featureSelect = document.getElementById("overlayFeature");
var outputSelect = document.getElementById("overlayOutput");
var sourceField = document.getElementById("overlaySourceField");
var sourceSummary = document.getElementById("overlaySourceSummary");
var editorDetails = document.getElementById("overlaySourceDetails");
var editorSettings = document.getElementById("overlayEditorSettings");
var editorHint = document.getElementById("overlayEditorHint");
var submitButton = document.getElementById("saveOverlayBtn");
var storage;
try {
  storage = window.localStorage;
} catch {
}
var restored = loadOverlays(storage);
var overlays = restored.overlays;
notice.textContent = restored.notice;
var frameIndex = indexFrame(null);
var displayedFrameIndex = indexFrame(null);
var knownOutputs = /* @__PURE__ */ new Map();
var cardNodes = /* @__PURE__ */ new Map();
var expandedOverlays = /* @__PURE__ */ new Set();
var editing = null;
var draft = null;
var returnFocus = addButton;
var nextControlId = 0;
function node(tag, className, text2) {
  const result = document.createElement(tag);
  if (className) result.className = className;
  if (text2 !== void 0) result.textContent = text2;
  return result;
}
function persist() {
  notice.textContent = saveOverlays(storage, overlays);
}
function setPanelOpen(open) {
  panel.hidden = !open;
  toggleButton.setAttribute("aria-expanded", String(open));
  toggleButton.setAttribute("aria-pressed", String(open));
  if (open) addButton.focus();
}
toggleButton.addEventListener("click", () => setPanelOpen(panel.hidden));
function styleControls(featureId, settings, changed, preview = changed) {
  const container = node("div", "overlay-style-controls");
  for (const control of featureFor(featureId)?.controls || []) {
    if (control.type === "checkbox") {
      const label2 = node("label", "overlay-visibility");
      const input2 = document.createElement("input");
      input2.type = "checkbox";
      input2.checked = settings[control.key];
      input2.addEventListener("change", () => {
        settings[control.key] = input2.checked;
        changed();
      });
      label2.append(input2, node("span", "wfr-txt-label", control.label));
      container.append(label2);
      continue;
    }
    const label = node("label", "overlay-field");
    label.append(node("span", "wfr-txt-label", control.label));
    if (control.type === "image") {
      const input2 = document.createElement("input");
      input2.type = "file";
      input2.accept = "image/png,image/jpeg,image/webp";
      input2.setAttribute("aria-label", control.label);
      const status2 = node("output", "overlay-hint wfr-txt-caption", settings[control.key] ? "Image saved in this browser." : "No image selected.");
      status2.setAttribute("role", "status");
      status2.id = `overlay-image-${++nextControlId}`;
      input2.setAttribute("aria-describedby", status2.id);
      const clear = node("button", "", "Clear image");
      clear.type = "button";
      let revision = 0;
      clear.addEventListener("click", () => {
        revision++;
        input2.value = "";
        input2.setCustomValidity("");
        input2.setAttribute("aria-invalid", "false");
        status2.setAttribute("data-state", "info");
        settings[control.key] = "";
        status2.textContent = "No image selected.";
        changed();
      });
      input2.addEventListener("change", async () => {
        const file = input2.files[0];
        if (!file) return;
        const current = ++revision;
        input2.setCustomValidity("Wait for the image to finish loading.");
        input2.setAttribute("aria-invalid", "false");
        status2.setAttribute("data-state", "info");
        status2.textContent = "Preparing image\u2026";
        try {
          const image = await loadLocalImage(file);
          if (current !== revision || !input2.isConnected) return;
          settings[control.key] = image;
          status2.textContent = "Image prepared locally; never uploaded.";
          changed();
        } catch (error) {
          if (current === revision) {
            status2.textContent = error.message;
            status2.setAttribute("data-state", "danger");
            input2.setAttribute("aria-invalid", "true");
          }
        } finally {
          if (current === revision) input2.setCustomValidity("");
        }
      });
      label.append(input2, status2);
      container.append(label, clear);
      continue;
    }
    if (control.type === "select") {
      const input2 = document.createElement("select");
      input2.setAttribute("aria-label", control.label);
      for (const option of control.options) input2.add(new Option(option.label, option.value));
      input2.value = settings[control.key];
      input2.addEventListener("change", () => {
        settings[control.key] = input2.value;
        changed();
      });
      label.append(input2);
      container.append(label);
      continue;
    }
    const row = node("span", "overlay-style-value");
    const input = document.createElement("input");
    input.type = control.type;
    input.setAttribute("aria-label", control.label);
    for (const attribute of ["min", "max", "step"]) {
      if (control[attribute] !== void 0) input[attribute] = control[attribute];
    }
    input.value = settings[control.key];
    const value = node("output");
    const updateValue = () => {
      value.textContent = control.type === "range" ? `${Math.round(Number(input.value) * 100)}%` : input.value;
    };
    input.addEventListener("input", () => {
      settings[control.key] = control.type === "range" ? Number(input.value) : input.value;
      updateValue();
      preview();
    });
    input.addEventListener("change", changed);
    updateValue();
    row.append(input, value);
    label.append(row);
    container.append(label);
  }
  return container;
}
function technicalDetails(source) {
  const description = node("dl", "overlay-metadata wfr-txt-code--sm");
  for (const [label, value] of sourceDetails(source)) {
    description.append(node("dt", "", label), node("dd", "", value));
  }
  return description;
}
function selectionKey(overlay2) {
  return overlay2?.selection.primary ? sourceKey(overlay2.selection.primary) : "";
}
function updateSourceLabels() {
  const catalog = new Map(knownOutputs);
  for (const overlay2 of overlays) {
    if (overlay2.selection.primary) catalog.set(selectionKey(overlay2), overlay2.selection.primary);
  }
  for (const [overlay2, row] of cardNodes) {
    const sources = featureFor(overlay2.featureId)?.discover(catalog) || [];
    const labels = sourceLabels(overlay2.selection.primary ? [...sources, overlay2.selection.primary] : sources, false);
    const source = overlay2.selection.primary ? labels.get(selectionKey(overlay2)) : "No source selected";
    const title = `${row.featureLabel} \xB7 ${source}`;
    row.title.textContent = title;
    row.toggle.title = `${title}
${row.status.textContent}`;
    row.checkbox.setAttribute("aria-label", `Show ${title}`);
    row.remove.setAttribute("aria-label", `Remove ${title} overlay`);
    if (row.metadata.hidden && overlay2.selection.primary) {
      row.metadata.replaceChildren(node("summary", "", "Details"), technicalDetails(overlay2.selection.primary));
      row.metadata.hidden = false;
    }
  }
}
function renderList() {
  cardNodes.clear();
  list.replaceChildren();
  if (!overlays.length) {
    list.append(node("p", "overlay-hint", "No overlays. Add an overlay to draw pipeline outputs on the video."));
  }
  for (const [index, overlay2] of overlays.entries()) {
    const card = node("article", "overlay-card");
    const header = node("div", "overlay-card-header");
    const featureLabel = featureFor(overlay2.featureId)?.label || overlay2.featureId;
    const visibility2 = node("label", "model-toggle-switch overlay-visible");
    const checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.setAttribute("role", "switch");
    checkbox.checked = overlay2.enabled;
    checkbox.addEventListener("change", () => {
      overlay2.enabled = checkbox.checked;
      persist();
      updateStatuses();
    });
    const track = node("span", "model-toggle-track");
    track.setAttribute("aria-hidden", "true");
    track.append(node("span", "model-toggle-thumb"));
    visibility2.append(checkbox, track);
    const toggle = node("button", "overlay-toggle");
    toggle.type = "button";
    toggle.id = `overlay-toggle-${index}`;
    const chevron = node("span", "icon");
    chevron.setAttribute("data-icon", "chevron-right");
    chevron.setAttribute("aria-hidden", "true");
    const swatch = node("span", "overlay-swatch");
    swatch.setAttribute("aria-hidden", "true");
    const title = node("span", "overlay-title wfr-txt-label");
    toggle.append(swatch, title, chevron);
    const status2 = node("p", "overlay-hint wfr-txt-caption");
    status2.id = `overlay-status-${index}`;
    const settings = node("div", "overlay-settings");
    settings.id = `overlay-settings-${index}`;
    settings.hidden = !expandedOverlays.has(overlay2);
    settings.setAttribute("role", "group");
    settings.setAttribute("aria-labelledby", toggle.id);
    toggle.setAttribute("aria-controls", settings.id);
    toggle.setAttribute("aria-describedby", status2.id);
    toggle.setAttribute("aria-expanded", String(!settings.hidden));
    toggle.addEventListener("click", () => {
      settings.hidden = !settings.hidden;
      toggle.setAttribute("aria-expanded", String(!settings.hidden));
      if (settings.hidden) expandedOverlays.delete(overlay2);
      else expandedOverlays.add(overlay2);
    });
    settings.append(status2, styleControls(
      overlay2.featureId,
      overlay2.settings,
      () => {
        persist();
        updateStatuses();
      },
      updateStatuses
    ));
    const actions = node("div", "overlay-actions");
    const edit = node("button", "", "Change source\u2026");
    edit.type = "button";
    edit.addEventListener("click", () => openEditor(overlay2, edit));
    const remove = node("button", "overlay-remove");
    remove.type = "button";
    remove.title = "Remove overlay";
    remove.setAttribute("aria-label", `Remove ${featureLabel} overlay`);
    const removeIcon = node("span", "icon");
    removeIcon.setAttribute("data-icon", "trash-2");
    removeIcon.setAttribute("aria-hidden", "true");
    remove.append(removeIcon);
    remove.addEventListener("click", () => {
      expandedOverlays.delete(overlay2);
      overlays.splice(overlays.indexOf(overlay2), 1);
      persist();
      renderList();
      addButton.focus();
    });
    header.append(visibility2, toggle, remove);
    actions.append(edit);
    settings.append(actions);
    const metadata = node("details", "overlay-details");
    metadata.hidden = !overlay2.selection.primary;
    if (overlay2.selection.primary) metadata.append(node("summary", "", "Details"), technicalDetails(overlay2.selection.primary));
    settings.append(metadata);
    card.append(header, settings);
    cardNodes.set(overlay2, { card, toggle, title, checkbox, remove, status: status2, metadata, featureLabel });
    list.append(card);
  }
  updateStatuses();
  updateSourceLabels();
}
function updateStatuses() {
  for (const [overlay2, row] of cardNodes) {
    const resolved = resolveOverlay(frameIndex, overlay2);
    const state = overlayStatus(frameIndex, overlay2, knownOutputs, resolved);
    if (row.status.textContent !== state) row.status.textContent = state;
    const available = !["waiting", "unsupported"].includes(resolved.state);
    const color = /^#[0-9a-f]{6}$/i.test(overlay2.settings.color) ? overlay2.settings.color : "transparent";
    if (row.color !== color || row.available !== available) {
      row.card.style.setProperty("--overlay-color", color);
      row.card.setAttribute("data-unavailable", String(!available));
      row.color = color;
      row.available = available;
    }
    const tooltip = `${row.title.textContent}
${state}`;
    if (row.toggle.title !== tooltip) row.toggle.title = tooltip;
  }
}
function availableDescriptors() {
  const descriptors = new Map(knownOutputs);
  if (editing?.selection.primary) descriptors.set(selectionKey(editing), editing.selection.primary);
  if (draft?.selection.primary) descriptors.set(selectionKey(draft), draft.selection.primary);
  return descriptors;
}
function refreshFeatures() {
  const previous = featureSelect.value;
  const catalog = availableDescriptors();
  const features = new Map(OVERLAY_FEATURES.filter((feature) => feature.render || feature.discover(catalog).length).map((feature) => [feature.id, feature.label]));
  for (const overlay2 of [editing, draft]) {
    if (overlay2 && !features.has(overlay2.featureId)) features.set(overlay2.featureId, featureFor(overlay2.featureId)?.label || overlay2.featureId);
  }
  featureSelect.replaceChildren();
  for (const [id, label] of features) featureSelect.add(new Option(label, id));
  if (!features.size) featureSelect.add(new Option("Waiting for pipeline outputs", ""));
  if (features.has(previous)) featureSelect.value = previous;
  featureSelect.disabled = !features.size;
}
function refreshOutputs(previous = outputSelect.value) {
  const catalog = availableDescriptors();
  const sources = featureFor(featureSelect.value)?.discover(catalog) || [];
  const choices = new Map(sources.map((source) => [sourceKey(source), source]));
  for (const overlay2 of [editing, draft]) {
    if (overlay2?.featureId === featureSelect.value && overlay2.selection.primary) choices.set(selectionKey(overlay2), overlay2.selection.primary);
  }
  const descriptors = [...choices.values()];
  const labels = sourceLabels(descriptors, false);
  outputSelect.replaceChildren();
  const hasChoice = descriptors.length > 1;
  const waiting = draft?.featureId === featureSelect.value && !draft.selection.primary || !draft && editing?.featureId === featureSelect.value && !editing.selection.primary;
  if (hasChoice && (waiting || !choices.has(previous))) outputSelect.add(new Option("Choose a source", ""));
  for (const source of descriptors) {
    const key = sourceKey(source);
    outputSelect.add(new Option(labels.get(key), key));
  }
  if (choices.has(previous)) outputSelect.value = previous;
  sourceField.hidden = !hasChoice;
  outputSelect.disabled = !hasChoice;
  outputSelect.required = hasChoice;
  sourceSummary.hidden = hasChoice;
  sourceSummary.textContent = descriptors.length === 1 ? `From ${labels.get(sourceKey(descriptors[0]))}` : "Source will be selected when available.";
  updateDraft();
}
function updateDraft() {
  const source = availableDescriptors().get(outputSelect.value) || null;
  if (!featureSelect.value) {
    draft = null;
    editorSettings.replaceChildren();
    editorDetails.hidden = true;
    editorHint.textContent = "Choose what to show.";
    submitButton.disabled = true;
    return;
  }
  const key = source ? sourceKey(source) : "";
  if (!draft || draft.featureId !== featureSelect.value || selectionKey(draft) !== key) {
    const previousDraft = draft;
    draft = createOverlay(featureSelect.value, source);
    draft.enabled = editing?.enabled ?? true;
    if (previousDraft?.featureId === draft.featureId && !previousDraft.selection.primary) {
      draft.settings = previousDraft.settings;
    } else if (editing?.featureId === draft.featureId && selectionKey(editing) === key) {
      draft.settings = { ...editing.settings };
    }
    if (draft.settings !== previousDraft?.settings) {
      editorSettings.replaceChildren(styleControls(draft.featureId, draft.settings, updateDraft));
    }
    editorDetails.replaceChildren(node("summary", "", "Details"));
    if (source) editorDetails.append(technicalDetails(source));
  }
  editorDetails.hidden = !source;
  const resolved = resolveOverlay(frameIndex, draft);
  const needsChoice = !source && !outputSelect.disabled;
  const hint = needsChoice ? "Choose a source." : resolved.state === "ready" ? "" : resolved.message;
  if (editorHint.textContent !== hint) editorHint.textContent = hint;
  submitButton.disabled = resolved.state === "unsupported" || needsChoice;
}
function openEditor(overlay2, opener) {
  editing = overlay2;
  draft = null;
  editorDetails.open = false;
  returnFocus = opener;
  document.getElementById("overlayDialogTitle").textContent = overlay2 ? "Edit overlay" : "Add overlay";
  submitButton.textContent = overlay2 ? "Apply" : "Add";
  refreshFeatures();
  if (overlay2) featureSelect.value = overlay2.featureId;
  refreshOutputs(selectionKey(overlay2));
  dialog.showModal();
}
addButton.addEventListener("click", () => openEditor(null, addButton));
featureSelect.addEventListener("change", () => refreshOutputs());
outputSelect.addEventListener("change", updateDraft);
document.getElementById("cancelOverlayBtn").addEventListener("click", () => dialog.close());
dialog.addEventListener("close", () => {
  editing = null;
  draft = null;
  if (returnFocus.isConnected) returnFocus.focus();
  else addButton.focus();
});
form.addEventListener("submit", (event) => {
  event.preventDefault();
  if (!draft || submitButton.disabled || resolveOverlay(frameIndex, draft).state === "unsupported") return;
  if (editing) Object.assign(editing, draft);
  else overlays.push(draft);
  persist();
  renderList();
  dialog.close();
});
function updateMetadata(frame, decodeError) {
  frameIndex = indexFrame(frame);
  connection.textContent = decodeError ? `Metadata unavailable: ${decodeError}` : frame ? "" : "Waiting for pipeline outputs";
  let changed = false;
  if (decodeError) {
    knownOutputs.clear();
    changed = true;
  }
  for (const [key, output] of frameIndex.outputs) {
    if (!knownOutputs.has(key)) {
      knownOutputs.set(key, output.source);
      changed = true;
    }
  }
  const bound = bindPendingOverlays(overlays, knownOutputs);
  if (bound) persist();
  updateStatuses();
  if (changed || bound) updateSourceLabels();
  if (dialog.open) {
    if (changed) {
      refreshFeatures();
      refreshOutputs();
    } else updateDraft();
  }
}
window.addEventListener("metadata-message", (event) => {
  updateMetadata(event.detail?.frame_results || null, event.detail?.decode_error);
});
window.addEventListener("video-frame-results", (event) => {
  if (event.detail) updateMetadata(event.detail);
  displayedFrameIndex = event.detail ? frameIndex : indexFrame(null);
  renderOsd(canvas, video4, displayedFrameIndex, overlays);
});
window.addEventListener("metadata-connection", (event) => {
  if (event.detail.connected) return;
  frameIndex = indexFrame(null);
  knownOutputs.clear();
  connection.textContent = "Metadata disconnected. Waiting for pipeline outputs.";
  updateStatuses();
  updateSourceLabels();
  if (dialog.open) {
    refreshFeatures();
    refreshOutputs();
  }
});
function renderFrame() {
  renderOsd(canvas, video4, displayedFrameIndex, overlays);
  window.requestAnimationFrame(renderFrame);
}
renderList();
if (typeof video4.requestVideoFrameCallback !== "function") {
  console.warn("Synchronized overlays require requestVideoFrameCallback; overlays are disabled.");
}
window.requestAnimationFrame(renderFrame);

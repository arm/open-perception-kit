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
var ICE_DISCONNECT_GRACE_MS = 1e4;
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
    this.onRemoteTrack = config.onRemoteTrack || (() => {
    });
    this.onVideoFrame = config.onVideoFrame || (() => {
    });
    this.generation = 0;
    this.session = null;
    this.restartTimer = null;
    this.started = false;
    this.paused = false;
    this.statusError = "";
    this.currentReconnectDelayMs = this.reconnectDelayMs;
  }
  start() {
    if (this.started)
      return;
    this.started = true;
    this.statusError = "";
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
      hasVideoFrame: false,
      lastFrameAt: this.now(),
      lastVideoTime: this.video.currentTime || 0,
      frameCallbackHandle: null,
      fallbackTimer: null,
      frameWatchdogTimer: null,
      iceDisconnectTimer: null,
      loadedDataHandler: null,
      frameMetadata: /* @__PURE__ */ new Map(),
      videoTimestamp: null
    };
    this.session = session;
    this.resetVideoElement(remoteStream);
    this.log(`Starting WebRTC session generation ${generation}: ${reason}`);
    const message = this.statusError || "Connecting\u2026";
    this.onStatus("connecting", "Connecting", message);
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
        if (!session.hasVideoFrame && !this.statusError) {
          this.onStatus("connecting", "Connecting", "Waiting for video\u2026");
        }
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
        if (session.iceDisconnectTimer) {
          this.clearTimeout(session.iceDisconnectTimer);
          session.iceDisconnectTimer = null;
          this.log("ICE recovered during disconnect grace period.");
        }
        if (!session.hasVideoFrame && !this.statusError) {
          this.onStatus("connecting", "Connecting", "Waiting for video\u2026");
        }
      } else if (state === "disconnected") {
        if (!session.iceDisconnectTimer) {
          this.log(`ICE disconnected; waiting ${ICE_DISCONNECT_GRACE_MS}ms for recovery.`);
          session.iceDisconnectTimer = this.setTimeout(() => {
            session.iceDisconnectTimer = null;
            if (this.isCurrent(session) && pc.iceConnectionState === "disconnected")
              this.scheduleRestart(session, "ICE disconnected");
          }, ICE_DISCONNECT_GRACE_MS);
        }
      } else if (state === "failed") {
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
          if (timestamp === session.videoTimestamp) {
            this.onVideoFrame(data);
          } else {
            session.frameMetadata.set(timestamp, { message: data, receivedAt: this.now() });
            while (session.frameMetadata.size > MAX_FRAME_METADATA_ENTRIES) {
              session.frameMetadata.delete(session.frameMetadata.keys().next().value);
            }
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
          if (!session.hasVideoFrame && !this.statusError) {
            this.onStatus("connecting", "Connecting", "Waiting for video\u2026");
          }
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
      if (this.isCurrent(session)) {
        this.log(`Signaling WebSocket error: ${formatError(err)}`, "error");
        this.statusError = "Signaling unavailable. Check server.";
        this.onStatus("disconnected", "Disconnected", this.statusError);
      }
    };
    ws.onclose = () => {
      if (!this.isCurrent(session) || session.intentionalClose)
        return;
      this.log("Signaling WebSocket closed. Scheduling reconnect.");
      const delay = this.nextReconnectDelay();
      this.scheduleRestart(session, "signaling closed", delay);
    };
    return ws;
  }
  async startOffer(session) {
    if (!this.isCurrent(session) || !session.ws || session.ws.readyState !== this.openState)
      return;
    try {
      const message = this.statusError || "Negotiating\u2026";
      this.onStatus("connecting", "Connecting", message);
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
          session.videoTimestamp = timestamp;
          const match = session.frameMetadata.get(timestamp);
          session.frameMetadata.delete(timestamp);
          this.onVideoFrame(match && this.now() - match.receivedAt < FRAME_METADATA_MAX_AGE_MS ? match.message : null);
          watchFrames();
        });
      };
      watchFrames();
    } else if (this.frameTimeoutMs > 0) {
      this.observeCurrentTimeFallback(session);
    } else {
      session.loadedDataHandler = () => {
        if (!this.isCurrent(session) || !this.video.videoWidth)
          return;
        session.hasVideoFrame = true;
        this.statusError = "";
        this.onStatus("connected", "Connected", "Connected");
      };
      this.video.addEventListener("loadeddata", session.loadedDataHandler);
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
    if (isHighFrequencyHeartbeat(source) && (source !== "video currentTime" || this.video.videoWidth > 0 && this.video.readyState >= 2) && (!session.hasVideoFrame || this.statusError)) {
      session.hasVideoFrame = true;
      this.statusError = "";
      this.onStatus("connected", "Connected", "Connected");
    }
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
    if (reason === "ICE failed") {
      const hasTurn = this.iceServers.some((server) => (Array.isArray(server.urls) ? server.urls : [server.urls]).some((url) => /^turns?:/i.test(url)));
      this.statusError = hasTurn ? "Connection failed. Check network/TURN." : "Connection failed. Try TURN.";
    } else {
      this.statusError = {
        "ICE disconnected": "Connection lost. Retrying\u2026",
        "signaling closed": "Signaling unavailable. Check server.",
        "video frame timeout": "No video. Retrying\u2026",
        "video track ended": "Video ended. Retrying\u2026"
      }[reason] || "Negotiation failed. Retrying\u2026";
    }
    this.onStatus("reconnecting", "Reconnecting", this.statusError);
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
    if (session.iceDisconnectTimer)
      this.clearTimeout(session.iceDisconnectTimer);
    if (session.fallbackTimer)
      this.clearTimeout(session.fallbackTimer);
    if (session.loadedDataHandler)
      this.video.removeEventListener("loadeddata", session.loadedDataHandler);
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
      this.statusError = "Playback failed. Check browser.";
      this.onStatus("disconnected", "Disconnected", this.statusError);
      return;
    }
    if (playback && typeof playback.catch === "function") {
      playback.catch((err) => {
        if (this.isCurrent(session)) {
          this.log(`Unable to start video playback: ${formatError(err)}`, "error");
          this.statusError = "Playback failed. Check browser.";
          this.onStatus("disconnected", "Disconnected", this.statusError);
        }
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
  setStatusLine(subtext || label);
  switch (state) {
    case "connecting":
    case "reconnecting":
    case "disconnected":
      overlay.classList.remove("hidden");
      overlayText.textContent = subtext || label;
      break;
    case "connected":
      overlay.classList.add("hidden");
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
    this.devices = /* @__PURE__ */ new Map();
  }
  stop(message = "Microphone off") {
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
    device.stream?.getTracks().forEach((track) => track.stop());
    if (device.transceiver) {
      const session = this.session;
      session.mediaChanges = session.mediaChanges.then(() => {
        if (this.session !== session) return;
        session.pc.removeTrack(device.transceiver.sender);
        device.transceiver.direction = "sendonly";
      });
    }
    this.onStatus(false, message, kind);
  }
  stopTrack(kind, message = `${kind === "audio" ? "Microphone" : "Camera"} off`) {
    const attached = Boolean(this.devices.get(kind)?.transceiver);
    this.release(kind, message);
    if (!this.devices.size) this.stop();
    else if (attached) this.session?.negotiate();
  }
  async start(kind = "audio") {
    if (!["audio", "video"].includes(kind)) throw new Error("Invalid capture kind");
    if (this.devices.has(kind))
      return;
    const label = kind === "audio" ? "Microphone" : "Camera";
    const device = {};
    this.devices.set(kind, device);
    this.onStatus(true, `Waiting for ${label.toLowerCase()} permission\u2026`, kind);
    try {
      const stream = await this.getUserMedia({
        audio: kind === "audio" ? { echoCancellation: true } : false,
        video: kind === "video"
      });
      if (this.devices.get(kind) !== device) {
        stream.getTracks().forEach((track2) => track2.stop());
        return;
      }
      device.stream = stream;
      const track = stream.getTracks().find((track2) => track2.kind === kind);
      if (!track) throw new Error(`No ${label.toLowerCase()} track`);
      if (!this.session) this.connect();
      const session = this.session;
      device.transceiver = session.transceivers.get(kind);
      if (device.transceiver) {
        const change = session.mediaChanges.then(async () => {
          if (this.session !== session || this.devices.get(kind) !== device) return;
          await device.transceiver.sender.replaceTrack(track);
          device.transceiver.sender.setStreams(stream);
        });
        session.mediaChanges = change.catch(() => {
        });
        await change;
        if (this.session !== session || this.devices.get(kind) !== device) return;
      } else {
        device.transceiver = session.pc.addTransceiver(track, { direction: "sendonly", streams: [stream] });
        session.transceivers.set(kind, device.transceiver);
      }
      track.onended = () => {
        if (this.devices.get(kind) === device)
          this.stopTrack(kind, `${label} disconnected`);
      };
      this.onStatus(true, `Connecting ${label.toLowerCase()}\u2026`, kind);
      this.session.negotiate();
    } catch (error) {
      if (this.devices.get(kind) === device)
        this.stopTrack(kind, `${label}: ${error.message || String(error)}`);
    }
  }
  connect() {
    const session = {
      localIce: [],
      remoteIce: [],
      offered: false,
      messages: Promise.resolve(),
      mediaChanges: Promise.resolve(),
      transceivers: /* @__PURE__ */ new Map()
    };
    this.session = session;
    const current = () => this.session === session;
    const fail = (error) => {
      if (current())
        this.stop("Capture: " + (error.message || String(error)));
    };
    try {
      const pc = session.pc = this.createPeer({ iceServers: this.iceServers });
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
          if (!session.negotiating) clearTimeout(session.timer);
          for (const [kind, device] of this.devices) {
            if (device.transceiver)
              this.onStatus(true, `${kind === "audio" ? "Microphone" : "Camera"} sharing`, kind);
          }
        } else if (["failed", "closed", "disconnected"].includes(pc.connectionState)) {
          fail(new Error("Connection lost"));
        }
      };
      session.negotiate = async () => {
        session.dirty = true;
        if (!session.open || session.negotiating || !current()) return;
        session.negotiating = true;
        session.dirty = false;
        session.offered = false;
        clearTimeout(session.timer);
        session.timer = setTimeout(() => fail(new Error("Negotiation timed out")), this.timeoutMs);
        try {
          await session.mediaChanges;
          if (!current())
            return;
          const capture = Object.fromEntries(["audio", "video"].map((kind) => [kind, Boolean(this.devices.get(kind)?.transceiver)]));
          const offer = await pc.createOffer();
          if (!current())
            return;
          await pc.setLocalDescription(offer);
          if (!current())
            return;
          send({ type: "offer", sdp: pc.localDescription.sdp, capture });
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
      ws.onmessage = (event) => {
        session.messages = session.messages.then(async () => {
          if (!current())
            return;
          const message = JSON.parse(event.data);
          if (message.type === "answer" && typeof message.sdp === "string") {
            if (!session.negotiating || !session.offered)
              throw new Error("Unexpected answer");
            await pc.setRemoteDescription({ type: "answer", sdp: message.sdp });
            if (!current()) return;
            for (const candidate of session.remoteIce) {
              if (!current())
                return;
              await pc.addIceCandidate(candidate);
            }
            session.remoteIce = [];
            session.negotiating = false;
            if (pc.connectionState === "connected") {
              clearTimeout(session.timer);
              for (const [kind, device] of this.devices) {
                if (device.transceiver)
                  this.onStatus(true, `${kind === "audio" ? "Microphone" : "Camera"} sharing`, kind);
              }
            }
            if (session.dirty) await session.negotiate();
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
var cameraButton = document.getElementById("cameraBtn");
var cameraIcon = document.getElementById("cameraIcon");
var cameraStatus = document.getElementById("cameraStatus");
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
  onStatus: (active, message, kind) => {
    const control = kind === "video" ? cameraButton : button;
    const indicator = kind === "video" ? cameraIcon : icon;
    const output = kind === "video" ? cameraStatus : status;
    const name = kind === "video" ? "camera" : "microphone";
    const glyph = kind === "video" ? "video" : "mic";
    control.setAttribute("aria-pressed", String(active));
    control.setAttribute("aria-label", active ? `Stop sharing ${name}` : `Share ${name}`);
    control.title = message;
    indicator.dataset.icon = active ? glyph : `${glyph}-off`;
    output.textContent = message;
  }
});
function enableMicrophoneButton(enable) {
  microphoneAvailable = enable === true;
  button.style.display = microphoneAvailable ? "inline-flex" : "none";
  button.disabled = !microphoneAvailable;
  status.hidden = !microphoneAvailable;
  cameraButton.style.display = microphoneAvailable ? "inline-flex" : "none";
  cameraButton.disabled = !microphoneAvailable;
  cameraStatus.hidden = !microphoneAvailable;
  if (!microphoneAvailable)
    client2.stop();
}
button.addEventListener("click", () => {
  if (!microphoneAvailable)
    return;
  if (client2.devices.has("audio")) {
    client2.stopTrack("audio");
  } else if (!navigator.mediaDevices?.getUserMedia) {
    status.textContent = "Microphone unavailable. Open this page through localhost.";
  } else {
    void client2.start();
  }
});
cameraButton.addEventListener("click", () => {
  if (!microphoneAvailable) return;
  if (client2.devices.has("video")) {
    client2.stopTrack("video");
  } else if (!navigator.mediaDevices?.getUserMedia) {
    cameraStatus.textContent = "Camera unavailable. Open this page through localhost.";
  } else {
    void client2.start("video");
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
function dockColumnMinimums(available2) {
  const compact = available2 < 900;
  return {
    metrics: compact ? 180 : 220,
    inference: compact ? 220 : 260,
    debug: compact ? 220 : 260
  };
}
function defaultDockColumnWidths() {
  const available2 = dockColumnAvailableWidth();
  const visibleKeys = visibleDockColumnKeys();
  const evenWidth = visibleKeys.length ? available2 / visibleKeys.length : 0;
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
  const available2 = dockColumnAvailableWidth();
  const visibleKeys = visibleDockColumnKeys();
  if (!available2)
    return widths;
  if (!visibleKeys.length)
    return Object.fromEntries(dockColumnKeys.map((key) => [key, 0]));
  const minimums = dockColumnMinimums(available2);
  const minTotal = visibleKeys.reduce((sum, key) => sum + minimums[key], 0);
  const usableMinimums = minTotal > available2 ? Object.fromEntries(visibleKeys.map((key) => [key, minimums[key] * available2 / minTotal])) : minimums;
  const next = Object.fromEntries(dockColumnKeys.map((key) => {
    if (!visibleKeys.includes(key)) {
      return [key, 0];
    }
    const requestedWidth = Number.isFinite(widths[key]) ? widths[key] : 0;
    return [key, Math.max(usableMinimums[key], requestedWidth)];
  }));
  let total = visibleKeys.reduce((sum, key) => sum + next[key], 0);
  if (total > available2) {
    let excess = total - available2;
    for (const key of ["inference", "metrics", "debug"].filter((item) => visibleKeys.includes(item))) {
      const shrinkable = Math.max(0, next[key] - usableMinimums[key]);
      const shrink = Math.min(shrinkable, excess);
      next[key] -= shrink;
      excess -= shrink;
      if (excess <= 0)
        break;
    }
  } else if (total < available2) {
    const extra = available2 - total;
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
  const available2 = dockColumnAvailableWidth();
  const visibleKeys = visibleDockColumnKeys();
  const evenWidth = visibleKeys.length ? available2 / visibleKeys.length : 0;
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
function setCopyButtonAvailable(button2, available2) {
  if (!button2) return;
  button2.disabled = !available2;
  button2.setAttribute("aria-disabled", available2 ? "false" : "true");
}

// development/web/src/performance-low-pass.js
var DISPLAY_INTERVAL_MS = 1e3;
var SMOOTHING_WEIGHT = 0.5;
function smoothValue(value, previous, digits) {
  if (value === null || previous === null || !Number.isFinite(value) || !Number.isFinite(previous)) return value;
  return Number((previous * (1 - SMOOTHING_WEIGHT) + value * SMOOTHING_WEIGHT).toFixed(digits));
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
    const counts = /* @__PURE__ */ new Map();
    for (const row of rows) if (row.key) counts.set(row.key, (counts.get(row.key) || 0) + 1);
    const identified = (row) => row.key && counts.get(row.key) === 1;
    const filtered = rows.map((row) => {
      const previous = identified(row) ? previousRows.get(row.key) : null;
      if (!previous || previous.unit !== row.unit) return { ...row };
      const digits = row.unit === "fps" ? 1 : 2;
      return {
        ...row,
        current: smoothValue(row.current, previous.current, digits),
        p95: smoothValue(row.p95, previous.p95, digits)
      };
    });
    previousRows = new Map(filtered.filter(identified).map((row) => [row.key, row]));
    lastDisplayAt = now;
    return filtered;
  };
}

// development/web/src/performance-data.js
var available = (value) => typeof value === "number" && Number.isFinite(value) && value >= 0;
var scopeLabel = (timing) => typeof timing.name === "string" && timing.name.length ? timing.name : typeof timing.scopeId === "string" && timing.scopeId.length ? timing.scopeId : "Unnamed scope";
function publishedPrecision(value, digits) {
  const scale = 10 ** digits;
  const scaled = value * scale;
  const lower = Math.floor(scaled);
  if (Number.isInteger(value * 2 ** (digits + 1)) && scaled - lower === 0.5 && lower % 2 === 0) {
    return lower / scale;
  }
  return Number(value.toFixed(digits));
}
function performanceRows(measurements) {
  const rows = [];
  for (const measurement of measurements || []) {
    const producer = measurement.producer;
    const key = (kind, id) => producer?.component && producer?.instanceId && id ? JSON.stringify([producer.component, producer.instanceId, kind, id]) : null;
    for (const timing of measurement.timings || []) {
      if (!timing) continue;
      rows.push({
        key: key("timing", timing.scopeId),
        stage: scopeLabel(timing),
        unit: "ms",
        current: available(timing.meanMs) ? publishedPrecision(timing.meanMs, 2) : null,
        p95: available(timing.p95Ms) ? publishedPrecision(timing.p95Ms, 2) : null
      });
    }
    if (measurement.frameRate) {
      rows.push({
        key: key("frameRate", "fps"),
        stage: "Pipeline",
        unit: "fps",
        current: available(measurement.frameRate.fps) ? publishedPrecision(measurement.frameRate.fps, 1) : null,
        p95: null
      });
    }
  }
  const counts = /* @__PURE__ */ new Map();
  for (const row of rows) if (row.key) counts.set(row.key, (counts.get(row.key) || 0) + 1);
  for (const row of rows) if (counts.get(row.key) > 1) row.key = null;
  return rows.filter((row) => row.current !== null || row.p95 !== null);
}
function formatMetricValue(value, unit) {
  if (!available(value)) return "";
  return unit === "fps" ? `${value.toFixed(1)} FPS` : `${value.toFixed(2)}ms`;
}
function formatPerformanceLines(measurement) {
  const rows = [];
  if (measurement.pixelFormat) rows.push(["Pixel format", measurement.pixelFormat]);
  for (const timing of measurement.timings || []) {
    if (!timing) continue;
    let value = available(timing.meanMs) ? `${publishedPrecision(timing.meanMs, 2).toFixed(2).padStart(7)}ms` : "  --.--ms";
    if (available(timing.p95Ms)) value += ` (p95: ${publishedPrecision(timing.p95Ms, 2).toFixed(2)}ms)`;
    rows.push([scopeLabel(timing), value]);
  }
  if (measurement.frameRate) {
    const fps = measurement.frameRate.fps;
    rows.push(["Pipeline", available(fps) ? `${publishedPrecision(fps, 1).toFixed(1).padStart(6)} FPS` : "   --.- FPS"]);
  }
  return rows.map(([name, value]) => `${name.padEnd(24)}: ${value}`);
}

// development/web/src/performance-metrics.js
var body = document.getElementById("performanceMetricsBody");
var copyButton = document.getElementById("copyPerformanceMetricsBtn");
var currentRows = [];
var filterMetrics = createPerformanceLowPass();
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
  row.appendChild(createCell(formatMetricValue(metric.current, metric.unit)));
  row.appendChild(createCell(formatMetricValue(metric.p95, metric.unit)));
  return row;
}
function renderRows(rows) {
  if (!body) return;
  currentRows = rows;
  updateCopyButtonState();
  body.replaceChildren(...rows.length ? rows.map(renderMetricRow) : [renderEmptyRow()]);
}
function renderPerformanceMetrics(performance2) {
  const measurements = Array.isArray(performance2?.measurements) ? performance2.measurements : [];
  const rows = filterMetrics(performanceRows(measurements));
  if (rows !== null) renderRows(rows);
}
function updateCopyButtonState() {
  setCopyButtonAvailable(copyButton, currentRows.length > 0);
}
function getMetricsText() {
  if (!currentRows.length) return "";
  return [
    "Stage	Current	P95",
    ...currentRows.map((row) => `${row.stage}	${formatMetricValue(row.current, row.unit)}	${formatMetricValue(row.p95, row.unit)}`)
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

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification-kind.js
var ClassificationKind;
(function(ClassificationKind2) {
  ClassificationKind2[ClassificationKind2["Unspecified"] = 0] = "Unspecified";
  ClassificationKind2[ClassificationKind2["Categorical"] = 1] = "Categorical";
  ClassificationKind2[ClassificationKind2["RecognizedText"] = 2] = "RecognizedText";
})(ClassificationKind || (ClassificationKind = {}));

// development/web/src/classification-results.js
var validProbability = (value) => typeof value === "number" && Number.isFinite(value) && value >= 0 && value <= 1;
function validCandidate(classification, candidate) {
  if (!candidate || candidate.confidence != null && !validProbability(candidate.confidence)) return false;
  if (classification.kind === ClassificationKind.Categorical)
    return Boolean(classification.labelSet) && Number.isInteger(candidate.classId) && candidate.classId >= 0;
  if (classification.kind === ClassificationKind.RecognizedText)
    return !classification.labelSet && candidate.classId == null;
  return false;
}
function presenceOutcome(data) {
  const result = data?.result;
  if (!result || typeof result.personPresent !== "boolean") return null;
  if (result.yesScore == null && result.noScore == null) return result.personPresent;
  return Number.isFinite(result.yesScore) && Number.isFinite(result.noScore) ? result.personPresent : null;
}
function contactOutcome(data) {
  const result = data?.result;
  if (!result || typeof result.inContact !== "boolean" || result.confidence != null && !validProbability(result.confidence)) return null;
  return result.inContact;
}

// development/web/src/gaze-results.js
function validGaze(data) {
  return Boolean(data?.angles) && Number.isFinite(data.angles.yawDegrees) && Number.isFinite(data.angles.pitchDegrees) && (data.confidence == null || validProbability(data.confidence));
}
function wrapDegrees(value) {
  const angle = value % 360;
  return angle > 180 ? angle - 360 : angle < -180 ? angle + 360 : angle;
}

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
      detail: data.spatialValid === false ? "Spatial reference unavailable" : `box x ${number(data.x)} y ${number(data.y)} w ${number(data.width)} h ${number(data.height)}`,
      meta: confidence ? `${confidence}` : ""
    };
  }
  if (type === "GazeMeasurement") {
    return {
      title: "Yaw / pitch",
      detail: validGaze(data) ? `yaw ${number(data.angles.yawDegrees)}\xB0 pitch ${number(data.angles.pitchDegrees)}\xB0` : "Gaze unavailable",
      meta: validGaze(data) ? number(data.confidence) : ""
    };
  }
  if (type === "CameraContact") {
    if (contactOutcome(data) === null)
      return { title: "Contact unavailable", detail: "classification", meta: "" };
    const result = data.result;
    return {
      title: result.inContact ? "Contact" : "No contact",
      detail: "classification",
      meta: number(result?.confidence)
    };
  }
  if (type === "Classification") {
    const best = Array.isArray(data.candidates) ? data.candidates[0] : null;
    if (best && !validCandidate(data, best))
      return { title: "Classification unavailable", detail: "Invalid kind, identity or score", meta: "" };
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
      detail: data.spatialValid === false ? "Spatial reference unavailable" : "trace",
      meta: ""
    };
  }
  if (type === "PersonClassification") {
    return {
      title: "Person classification",
      detail: presenceOutcome(data) === null ? "Presence unavailable" : `${data.result.personPresent ? "Person" : "No person"}; raw yes ${number(data.result.yesScore)} no ${number(data.result.noScore)}`,
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

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/coordinate-units.js
var CoordinateUnits;
(function(CoordinateUnits2) {
  CoordinateUnits2[CoordinateUnits2["Pixels"] = 0] = "Pixels";
  CoordinateUnits2[CoordinateUnits2["Normalized"] = 1] = "Normalized";
})(CoordinateUnits || (CoordinateUnits = {}));

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/image-space.js
var ImageSpace = class _ImageSpace {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsImageSpace(bb, obj) {
    return (obj || new _ImageSpace()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsImageSpace(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _ImageSpace()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  frameId() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  width() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint32(this.bb_pos + offset) : 0;
  }
  height() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint32(this.bb_pos + offset) : 0;
  }
  originX() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0;
  }
  originY() {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0;
  }
  extentWidth() {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0;
  }
  extentHeight() {
    const offset = this.bb.__offset(this.bb_pos, 16);
    return offset ? this.bb.readFloat64(this.bb_pos + offset) : 0;
  }
  static startImageSpace(builder) {
    builder.startObject(7);
  }
  static addFrameId(builder, frameId) {
    builder.addFieldInt64(0, frameId, BigInt("0"));
  }
  static addWidth(builder, width) {
    builder.addFieldInt32(1, width, 0);
  }
  static addHeight(builder, height) {
    builder.addFieldInt32(2, height, 0);
  }
  static addOriginX(builder, originX) {
    builder.addFieldFloat64(3, originX, 0);
  }
  static addOriginY(builder, originY) {
    builder.addFieldFloat64(4, originY, 0);
  }
  static addExtentWidth(builder, extentWidth) {
    builder.addFieldFloat64(5, extentWidth, 0);
  }
  static addExtentHeight(builder, extentHeight) {
    builder.addFieldFloat64(6, extentHeight, 0);
  }
  static endImageSpace(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createImageSpace(builder, frameId, width, height, originX, originY, extentWidth, extentHeight) {
    _ImageSpace.startImageSpace(builder);
    _ImageSpace.addFrameId(builder, frameId);
    _ImageSpace.addWidth(builder, width);
    _ImageSpace.addHeight(builder, height);
    _ImageSpace.addOriginX(builder, originX);
    _ImageSpace.addOriginY(builder, originY);
    _ImageSpace.addExtentWidth(builder, extentWidth);
    _ImageSpace.addExtentHeight(builder, extentHeight);
    return _ImageSpace.endImageSpace(builder);
  }
  unpack() {
    return new ImageSpaceT(this.frameId(), this.width(), this.height(), this.originX(), this.originY(), this.extentWidth(), this.extentHeight());
  }
  unpackTo(_o) {
    _o.frameId = this.frameId();
    _o.width = this.width();
    _o.height = this.height();
    _o.originX = this.originX();
    _o.originY = this.originY();
    _o.extentWidth = this.extentWidth();
    _o.extentHeight = this.extentHeight();
  }
};
var ImageSpaceT = class {
  constructor(frameId = BigInt("0"), width = 0, height = 0, originX = 0, originY = 0, extentWidth = 0, extentHeight = 0) {
    this.frameId = frameId;
    this.width = width;
    this.height = height;
    this.originX = originX;
    this.originY = originY;
    this.extentWidth = extentWidth;
    this.extentHeight = extentHeight;
  }
  pack(builder) {
    return ImageSpace.createImageSpace(builder, this.frameId, this.width, this.height, this.originX, this.originY, this.extentWidth, this.extentHeight);
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
  space(obj) {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? (obj || new ImageSpace()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  units() {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.readUint8(this.bb_pos + offset) : CoordinateUnits.Pixels;
  }
  static startBoundingBox(builder) {
    builder.startObject(6);
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
  static addSpace(builder, spaceOffset) {
    builder.addFieldOffset(4, spaceOffset, 0);
  }
  static addUnits(builder, units) {
    builder.addFieldInt8(5, units, CoordinateUnits.Pixels);
  }
  static endBoundingBox(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new BoundingBoxT(this.x(), this.y(), this.width(), this.height(), this.space() !== null ? this.space().unpack() : null, this.units());
  }
  unpackTo(_o) {
    _o.x = this.x();
    _o.y = this.y();
    _o.width = this.width();
    _o.height = this.height();
    _o.space = this.space() !== null ? this.space().unpack() : null;
    _o.units = this.units();
  }
};
var BoundingBoxT = class {
  constructor(x = 0, y = 0, width = 0, height = 0, space = null, units = CoordinateUnits.Pixels) {
    this.x = x;
    this.y = y;
    this.width = width;
    this.height = height;
    this.space = space;
    this.units = units;
  }
  pack(builder) {
    const space = this.space !== null ? this.space.pack(builder) : 0;
    BoundingBox.startBoundingBox(builder);
    BoundingBox.addX(builder, this.x);
    BoundingBox.addY(builder, this.y);
    BoundingBox.addWidth(builder, this.width);
    BoundingBox.addHeight(builder, this.height);
    BoundingBox.addSpace(builder, space);
    BoundingBox.addUnits(builder, this.units);
    return BoundingBox.endBoundingBox(builder);
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
    return offset ? this.bb.readUint32(this.bb_pos + offset) : null;
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
    builder.addFieldInt32(3, classId, null);
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
  constructor(object = null, box = null, confidence = 0, classId = null, text2 = null) {
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
    if (this.classId !== null)
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

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/camera-contact-result.js
var CameraContactResult = class _CameraContactResult {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsCameraContactResult(bb, obj) {
    return (obj || new _CameraContactResult()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsCameraContactResult(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _CameraContactResult()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  inContact() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
  }
  confidence() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : null;
  }
  modelClassIndex() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint32(this.bb_pos + offset) : null;
  }
  static startCameraContactResult(builder) {
    builder.startObject(3);
  }
  static addInContact(builder, inContact) {
    builder.addFieldInt8(0, +inContact, 0);
  }
  static addConfidence(builder, confidence) {
    builder.addFieldFloat32(1, confidence, null);
  }
  static addModelClassIndex(builder, modelClassIndex) {
    builder.addFieldInt32(2, modelClassIndex, null);
  }
  static endCameraContactResult(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createCameraContactResult(builder, inContact, confidence, modelClassIndex) {
    _CameraContactResult.startCameraContactResult(builder);
    _CameraContactResult.addInContact(builder, inContact);
    if (confidence !== null)
      _CameraContactResult.addConfidence(builder, confidence);
    if (modelClassIndex !== null)
      _CameraContactResult.addModelClassIndex(builder, modelClassIndex);
    return _CameraContactResult.endCameraContactResult(builder);
  }
  unpack() {
    return new CameraContactResultT(this.inContact(), this.confidence(), this.modelClassIndex());
  }
  unpackTo(_o) {
    _o.inContact = this.inContact();
    _o.confidence = this.confidence();
    _o.modelClassIndex = this.modelClassIndex();
  }
};
var CameraContactResultT = class {
  constructor(inContact = false, confidence = null, modelClassIndex = null) {
    this.inContact = inContact;
    this.confidence = confidence;
    this.modelClassIndex = modelClassIndex;
  }
  pack(builder) {
    return CameraContactResult.createCameraContactResult(builder, this.inContact, this.confidence, this.modelClassIndex);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/camera-contact.js
var CameraContact = class _CameraContact {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsCameraContact(bb, obj) {
    return (obj || new _CameraContact()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsCameraContact(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _CameraContact()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  result(obj) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? (obj || new CameraContactResult()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  subjectId() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : null;
  }
  static startCameraContact(builder) {
    builder.startObject(3);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addResult(builder, resultOffset) {
    builder.addFieldOffset(1, resultOffset, 0);
  }
  static addSubjectId(builder, subjectId) {
    builder.addFieldInt64(2, subjectId, null);
  }
  static endCameraContact(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new CameraContactT(this.object() !== null ? this.object().unpack() : null, this.result() !== null ? this.result().unpack() : null, this.subjectId());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.result = this.result() !== null ? this.result().unpack() : null;
    _o.subjectId = this.subjectId();
  }
};
var CameraContactT = class {
  constructor(object = null, result = null, subjectId = null) {
    this.object = object;
    this.result = result;
    this.subjectId = subjectId;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const result = this.result !== null ? this.result.pack(builder) : 0;
    CameraContact.startCameraContact(builder);
    CameraContact.addObject(builder, object);
    CameraContact.addResult(builder, result);
    if (this.subjectId !== null)
      CameraContact.addSubjectId(builder, this.subjectId);
    return CameraContact.endCameraContact(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/camera-contacts.js
var CameraContacts = class _CameraContacts {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsCameraContacts(bb, obj) {
    return (obj || new _CameraContacts()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsCameraContacts(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _CameraContacts()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("CCON");
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
  contacts(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new CameraContact()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  contactsLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startCameraContacts(builder) {
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
  static addContacts(builder, contactsOffset) {
    builder.addFieldOffset(3, contactsOffset, 0);
  }
  static createContactsVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startContactsVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endCameraContacts(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishCameraContactsBuffer(builder, offset) {
    builder.finish(offset, "CCON");
  }
  static finishSizePrefixedCameraContactsBuffer(builder, offset) {
    builder.finish(offset, "CCON", true);
  }
  unpack() {
    return new CameraContactsT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.contacts.bind(this), this.contactsLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.contacts = this.bb.createObjList(this.contacts.bind(this), this.contactsLength());
  }
};
var CameraContactsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, contacts = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.contacts = contacts;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const contacts = CameraContacts.createContactsVector(builder, builder.createObjectOffsetList(this.contacts));
    CameraContacts.startCameraContacts(builder);
    CameraContacts.addSchemaMajor(builder, this.schemaMajor);
    CameraContacts.addSchemaMinor(builder, this.schemaMinor);
    CameraContacts.addLayer(builder, layer2);
    CameraContacts.addContacts(builder, contacts);
    return CameraContacts.endCameraContacts(builder);
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
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : null;
  }
  classId() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint32(this.bb_pos + offset) : null;
  }
  text(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  static startClassificationCandidate(builder) {
    builder.startObject(3);
  }
  static addConfidence(builder, confidence) {
    builder.addFieldFloat32(0, confidence, null);
  }
  static addClassId(builder, classId) {
    builder.addFieldInt32(1, classId, null);
  }
  static addText(builder, textOffset) {
    builder.addFieldOffset(2, textOffset, 0);
  }
  static endClassificationCandidate(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createClassificationCandidate(builder, confidence, classId, textOffset) {
    _ClassificationCandidate.startClassificationCandidate(builder);
    if (confidence !== null)
      _ClassificationCandidate.addConfidence(builder, confidence);
    if (classId !== null)
      _ClassificationCandidate.addClassId(builder, classId);
    _ClassificationCandidate.addText(builder, textOffset);
    return _ClassificationCandidate.endClassificationCandidate(builder);
  }
  unpack() {
    return new ClassificationCandidateT(this.confidence(), this.classId(), this.text());
  }
  unpackTo(_o) {
    _o.confidence = this.confidence();
    _o.classId = this.classId();
    _o.text = this.text();
  }
};
var ClassificationCandidateT = class {
  constructor(confidence = null, classId = null, text2 = null) {
    this.confidence = confidence;
    this.classId = classId;
    this.text = text2;
  }
  pack(builder) {
    const text2 = this.text !== null ? builder.createString(this.text) : 0;
    return ClassificationCandidate.createClassificationCandidate(builder, this.confidence, this.classId, text2);
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
  labelSet(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  kind() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readUint8(this.bb_pos + offset) : ClassificationKind.Unspecified;
  }
  subjectId() {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : null;
  }
  static startClassification(builder) {
    builder.startObject(5);
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
  static addLabelSet(builder, labelSetOffset) {
    builder.addFieldOffset(2, labelSetOffset, 0);
  }
  static addKind(builder, kind) {
    builder.addFieldInt8(3, kind, ClassificationKind.Unspecified);
  }
  static addSubjectId(builder, subjectId) {
    builder.addFieldInt64(4, subjectId, null);
  }
  static endClassification(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createClassification(builder, objectOffset, candidatesOffset, labelSetOffset, kind, subjectId) {
    _Classification.startClassification(builder);
    _Classification.addObject(builder, objectOffset);
    _Classification.addCandidates(builder, candidatesOffset);
    _Classification.addLabelSet(builder, labelSetOffset);
    _Classification.addKind(builder, kind);
    if (subjectId !== null)
      _Classification.addSubjectId(builder, subjectId);
    return _Classification.endClassification(builder);
  }
  unpack() {
    return new ClassificationT(this.object() !== null ? this.object().unpack() : null, this.bb.createObjList(this.candidates.bind(this), this.candidatesLength()), this.labelSet(), this.kind(), this.subjectId());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.candidates = this.bb.createObjList(this.candidates.bind(this), this.candidatesLength());
    _o.labelSet = this.labelSet();
    _o.kind = this.kind();
    _o.subjectId = this.subjectId();
  }
};
var ClassificationT = class {
  constructor(object = null, candidates = [], labelSet = null, kind = ClassificationKind.Unspecified, subjectId = null) {
    this.object = object;
    this.candidates = candidates;
    this.labelSet = labelSet;
    this.kind = kind;
    this.subjectId = subjectId;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const candidates = Classification.createCandidatesVector(builder, builder.createObjectOffsetList(this.candidates));
    const labelSet = this.labelSet !== null ? builder.createString(this.labelSet) : 0;
    return Classification.createClassification(builder, object, candidates, labelSet, this.kind, this.subjectId);
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
  static startClassifications(builder) {
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
    return new ClassificationsT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.classifications.bind(this), this.classificationsLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.classifications = this.bb.createObjList(this.classifications.bind(this), this.classificationsLength());
  }
};
var ClassificationsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, classifications = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.classifications = classifications;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const classifications = Classifications.createClassificationsVector(builder, builder.createObjectOffsetList(this.classifications));
    Classifications.startClassifications(builder);
    Classifications.addSchemaMajor(builder, this.schemaMajor);
    Classifications.addSchemaMinor(builder, this.schemaMinor);
    Classifications.addLayer(builder, layer2);
    Classifications.addClassifications(builder, classifications);
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
  presentationTsNs() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : null;
  }
  static startVideoFrameContext(builder) {
    builder.startObject(4);
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
  static addPresentationTsNs(builder, presentationTsNs) {
    builder.addFieldInt64(3, presentationTsNs, null);
  }
  static endVideoFrameContext(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createVideoFrameContext(builder, objectOffset, originalWidth, originalHeight, presentationTsNs) {
    _VideoFrameContext.startVideoFrameContext(builder);
    _VideoFrameContext.addObject(builder, objectOffset);
    _VideoFrameContext.addOriginalWidth(builder, originalWidth);
    _VideoFrameContext.addOriginalHeight(builder, originalHeight);
    if (presentationTsNs !== null)
      _VideoFrameContext.addPresentationTsNs(builder, presentationTsNs);
    return _VideoFrameContext.endVideoFrameContext(builder);
  }
  unpack() {
    return new VideoFrameContextT(this.object() !== null ? this.object().unpack() : null, this.originalWidth(), this.originalHeight(), this.presentationTsNs());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.originalWidth = this.originalWidth();
    _o.originalHeight = this.originalHeight();
    _o.presentationTsNs = this.presentationTsNs();
  }
};
var VideoFrameContextT = class {
  constructor(object = null, originalWidth = BigInt("0"), originalHeight = BigInt("0"), presentationTsNs = null) {
    this.object = object;
    this.originalWidth = originalWidth;
    this.originalHeight = originalHeight;
    this.presentationTsNs = presentationTsNs;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    return VideoFrameContext.createVideoFrameContext(builder, object, this.originalWidth, this.originalHeight, this.presentationTsNs);
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

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/gaze-angles.js
var GazeAngles = class _GazeAngles {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsGazeAngles(bb, obj) {
    return (obj || new _GazeAngles()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsGazeAngles(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _GazeAngles()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  yawDegrees() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  pitchDegrees() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  static startGazeAngles(builder) {
    builder.startObject(2);
  }
  static addYawDegrees(builder, yawDegrees) {
    builder.addFieldFloat32(0, yawDegrees, 0);
  }
  static addPitchDegrees(builder, pitchDegrees) {
    builder.addFieldFloat32(1, pitchDegrees, 0);
  }
  static endGazeAngles(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createGazeAngles(builder, yawDegrees, pitchDegrees) {
    _GazeAngles.startGazeAngles(builder);
    _GazeAngles.addYawDegrees(builder, yawDegrees);
    _GazeAngles.addPitchDegrees(builder, pitchDegrees);
    return _GazeAngles.endGazeAngles(builder);
  }
  unpack() {
    return new GazeAnglesT(this.yawDegrees(), this.pitchDegrees());
  }
  unpackTo(_o) {
    _o.yawDegrees = this.yawDegrees();
    _o.pitchDegrees = this.pitchDegrees();
  }
};
var GazeAnglesT = class {
  constructor(yawDegrees = 0, pitchDegrees = 0) {
    this.yawDegrees = yawDegrees;
    this.pitchDegrees = pitchDegrees;
  }
  pack(builder) {
    return GazeAngles.createGazeAngles(builder, this.yawDegrees, this.pitchDegrees);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/gaze-measurement.js
var GazeMeasurement = class _GazeMeasurement {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsGazeMeasurement(bb, obj) {
    return (obj || new _GazeMeasurement()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsGazeMeasurement(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _GazeMeasurement()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  subjectId() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : null;
  }
  angles(obj) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? (obj || new GazeAngles()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  confidence() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : null;
  }
  static startGazeMeasurement(builder) {
    builder.startObject(4);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addSubjectId(builder, subjectId) {
    builder.addFieldInt64(1, subjectId, null);
  }
  static addAngles(builder, anglesOffset) {
    builder.addFieldOffset(2, anglesOffset, 0);
  }
  static addConfidence(builder, confidence) {
    builder.addFieldFloat32(3, confidence, null);
  }
  static endGazeMeasurement(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new GazeMeasurementT(this.object() !== null ? this.object().unpack() : null, this.subjectId(), this.angles() !== null ? this.angles().unpack() : null, this.confidence());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.subjectId = this.subjectId();
    _o.angles = this.angles() !== null ? this.angles().unpack() : null;
    _o.confidence = this.confidence();
  }
};
var GazeMeasurementT = class {
  constructor(object = null, subjectId = null, angles = null, confidence = null) {
    this.object = object;
    this.subjectId = subjectId;
    this.angles = angles;
    this.confidence = confidence;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const angles = this.angles !== null ? this.angles.pack(builder) : 0;
    GazeMeasurement.startGazeMeasurement(builder);
    GazeMeasurement.addObject(builder, object);
    if (this.subjectId !== null)
      GazeMeasurement.addSubjectId(builder, this.subjectId);
    GazeMeasurement.addAngles(builder, angles);
    if (this.confidence !== null)
      GazeMeasurement.addConfidence(builder, this.confidence);
    return GazeMeasurement.endGazeMeasurement(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/gaze-measurements.js
var GazeMeasurements = class _GazeMeasurements {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsGazeMeasurements(bb, obj) {
    return (obj || new _GazeMeasurements()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsGazeMeasurements(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _GazeMeasurements()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("GZMS");
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
  measurements(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new GazeMeasurement()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  measurementsLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startGazeMeasurements(builder) {
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
  static addMeasurements(builder, measurementsOffset) {
    builder.addFieldOffset(3, measurementsOffset, 0);
  }
  static createMeasurementsVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startMeasurementsVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endGazeMeasurements(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishGazeMeasurementsBuffer(builder, offset) {
    builder.finish(offset, "GZMS");
  }
  static finishSizePrefixedGazeMeasurementsBuffer(builder, offset) {
    builder.finish(offset, "GZMS", true);
  }
  unpack() {
    return new GazeMeasurementsT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.measurements.bind(this), this.measurementsLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.measurements = this.bb.createObjList(this.measurements.bind(this), this.measurementsLength());
  }
};
var GazeMeasurementsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, measurements = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.measurements = measurements;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const measurements = GazeMeasurements.createMeasurementsVector(builder, builder.createObjectOffsetList(this.measurements));
    GazeMeasurements.startGazeMeasurements(builder);
    GazeMeasurements.addSchemaMajor(builder, this.schemaMajor);
    GazeMeasurements.addSchemaMinor(builder, this.schemaMinor);
    GazeMeasurements.addLayer(builder, layer2);
    GazeMeasurements.addMeasurements(builder, measurements);
    return GazeMeasurements.endGazeMeasurements(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/bitmap-encoding.js
var BitmapEncoding;
(function(BitmapEncoding2) {
  BitmapEncoding2[BitmapEncoding2["Unspecified"] = 0] = "Unspecified";
  BitmapEncoding2[BitmapEncoding2["Uint8"] = 1] = "Uint8";
  BitmapEncoding2[BitmapEncoding2["Uint32LE"] = 2] = "Uint32LE";
})(BitmapEncoding || (BitmapEncoding = {}));

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
  encoding() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint8(this.bb_pos + offset) : BitmapEncoding.Unspecified;
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
  space(obj) {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? (obj || new ImageSpace()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  static startBitmapData(builder) {
    builder.startObject(5);
  }
  static addWidth(builder, width) {
    builder.addFieldInt32(0, width, 0);
  }
  static addHeight(builder, height) {
    builder.addFieldInt32(1, height, 0);
  }
  static addEncoding(builder, encoding) {
    builder.addFieldInt8(2, encoding, BitmapEncoding.Unspecified);
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
  static addSpace(builder, spaceOffset) {
    builder.addFieldOffset(4, spaceOffset, 0);
  }
  static endBitmapData(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new BitmapDataT(this.width(), this.height(), this.encoding(), this.bb.createScalarList(this.pixels.bind(this), this.pixelsLength()), this.space() !== null ? this.space().unpack() : null);
  }
  unpackTo(_o) {
    _o.width = this.width();
    _o.height = this.height();
    _o.encoding = this.encoding();
    _o.pixels = this.bb.createScalarList(this.pixels.bind(this), this.pixelsLength());
    _o.space = this.space() !== null ? this.space().unpack() : null;
  }
};
var BitmapDataT = class {
  constructor(width = 0, height = 0, encoding = BitmapEncoding.Unspecified, pixels = [], space = null) {
    this.width = width;
    this.height = height;
    this.encoding = encoding;
    this.pixels = pixels;
    this.space = space;
  }
  pack(builder) {
    const pixels = BitmapData.createPixelsVector(builder, this.pixels);
    const space = this.space !== null ? this.space.pack(builder) : 0;
    BitmapData.startBitmapData(builder);
    BitmapData.addWidth(builder, this.width);
    BitmapData.addHeight(builder, this.height);
    BitmapData.addEncoding(builder, this.encoding);
    BitmapData.addPixels(builder, pixels);
    BitmapData.addSpace(builder, space);
    return BitmapData.endBitmapData(builder);
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
    return offset ? this.bb.readUint32(this.bb_pos + offset) : null;
  }
  confidence() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : null;
  }
  box(obj) {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? (obj || new BoundingBox()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  labelSet(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  static startInstanceSegmentationMask(builder) {
    builder.startObject(6);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addBitmap(builder, bitmapOffset) {
    builder.addFieldOffset(1, bitmapOffset, 0);
  }
  static addClassId(builder, classId) {
    builder.addFieldInt32(2, classId, null);
  }
  static addConfidence(builder, confidence) {
    builder.addFieldFloat32(3, confidence, null);
  }
  static addBox(builder, boxOffset) {
    builder.addFieldOffset(4, boxOffset, 0);
  }
  static addLabelSet(builder, labelSetOffset) {
    builder.addFieldOffset(5, labelSetOffset, 0);
  }
  static endInstanceSegmentationMask(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new InstanceSegmentationMaskT(this.object() !== null ? this.object().unpack() : null, this.bitmap() !== null ? this.bitmap().unpack() : null, this.classId(), this.confidence(), this.box() !== null ? this.box().unpack() : null, this.labelSet());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.bitmap = this.bitmap() !== null ? this.bitmap().unpack() : null;
    _o.classId = this.classId();
    _o.confidence = this.confidence();
    _o.box = this.box() !== null ? this.box().unpack() : null;
    _o.labelSet = this.labelSet();
  }
};
var InstanceSegmentationMaskT = class {
  constructor(object = null, bitmap = null, classId = null, confidence = null, box = null, labelSet = null) {
    this.object = object;
    this.bitmap = bitmap;
    this.classId = classId;
    this.confidence = confidence;
    this.box = box;
    this.labelSet = labelSet;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const bitmap = this.bitmap !== null ? this.bitmap.pack(builder) : 0;
    const box = this.box !== null ? this.box.pack(builder) : 0;
    const labelSet = this.labelSet !== null ? builder.createString(this.labelSet) : 0;
    InstanceSegmentationMask.startInstanceSegmentationMask(builder);
    InstanceSegmentationMask.addObject(builder, object);
    InstanceSegmentationMask.addBitmap(builder, bitmap);
    if (this.classId !== null)
      InstanceSegmentationMask.addClassId(builder, this.classId);
    if (this.confidence !== null)
      InstanceSegmentationMask.addConfidence(builder, this.confidence);
    InstanceSegmentationMask.addBox(builder, box);
    InstanceSegmentationMask.addLabelSet(builder, labelSet);
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
    return offset ? this.bb.readUint32(this.bb_pos + offset) : null;
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
    builder.addFieldInt32(5, classId, null);
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
  constructor(object = null, sourceId = BigInt("0"), trackId = BigInt("0"), box = null, confidence = 0, classId = null, text2 = null, diagnostic = null, predictedOnly = false) {
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
    if (this.classId !== null)
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

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/frame-rate.js
var FrameRate = class _FrameRate {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsFrameRate(bb, obj) {
    return (obj || new _FrameRate()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsFrameRate(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _FrameRate()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  fps() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readFloat64(this.bb_pos + offset) : null;
  }
  static startFrameRate(builder) {
    builder.startObject(1);
  }
  static addFps(builder, fps) {
    builder.addFieldFloat64(0, fps, null);
  }
  static endFrameRate(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createFrameRate(builder, fps) {
    _FrameRate.startFrameRate(builder);
    if (fps !== null)
      _FrameRate.addFps(builder, fps);
    return _FrameRate.endFrameRate(builder);
  }
  unpack() {
    return new FrameRateT(this.fps());
  }
  unpackTo(_o) {
    _o.fps = this.fps();
  }
};
var FrameRateT = class {
  constructor(fps = null) {
    this.fps = fps;
  }
  pack(builder) {
    return FrameRate.createFrameRate(builder, this.fps);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/scope-timing.js
var ScopeTiming = class _ScopeTiming {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsScopeTiming(bb, obj) {
    return (obj || new _ScopeTiming()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsScopeTiming(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _ScopeTiming()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  name(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  meanMs() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat64(this.bb_pos + offset) : null;
  }
  p95Ms() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readFloat64(this.bb_pos + offset) : null;
  }
  scopeId(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  static startScopeTiming(builder) {
    builder.startObject(4);
  }
  static addName(builder, nameOffset) {
    builder.addFieldOffset(0, nameOffset, 0);
  }
  static addMeanMs(builder, meanMs) {
    builder.addFieldFloat64(1, meanMs, null);
  }
  static addP95Ms(builder, p95Ms) {
    builder.addFieldFloat64(2, p95Ms, null);
  }
  static addScopeId(builder, scopeIdOffset) {
    builder.addFieldOffset(3, scopeIdOffset, 0);
  }
  static endScopeTiming(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createScopeTiming(builder, nameOffset, meanMs, p95Ms, scopeIdOffset) {
    _ScopeTiming.startScopeTiming(builder);
    _ScopeTiming.addName(builder, nameOffset);
    if (meanMs !== null)
      _ScopeTiming.addMeanMs(builder, meanMs);
    if (p95Ms !== null)
      _ScopeTiming.addP95Ms(builder, p95Ms);
    _ScopeTiming.addScopeId(builder, scopeIdOffset);
    return _ScopeTiming.endScopeTiming(builder);
  }
  unpack() {
    return new ScopeTimingT(this.name(), this.meanMs(), this.p95Ms(), this.scopeId());
  }
  unpackTo(_o) {
    _o.name = this.name();
    _o.meanMs = this.meanMs();
    _o.p95Ms = this.p95Ms();
    _o.scopeId = this.scopeId();
  }
};
var ScopeTimingT = class {
  constructor(name = null, meanMs = null, p95Ms = null, scopeId = null) {
    this.name = name;
    this.meanMs = meanMs;
    this.p95Ms = p95Ms;
    this.scopeId = scopeId;
  }
  pack(builder) {
    const name = this.name !== null ? builder.createString(this.name) : 0;
    const scopeId = this.scopeId !== null ? builder.createString(this.scopeId) : 0;
    return ScopeTiming.createScopeTiming(builder, name, this.meanMs, this.p95Ms, scopeId);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/performance-measurements.js
var PerformanceMeasurements = class _PerformanceMeasurements {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsPerformanceMeasurements(bb, obj) {
    return (obj || new _PerformanceMeasurements()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsPerformanceMeasurements(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _PerformanceMeasurements()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("PMET");
  }
  schemaMajor() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 1;
  }
  schemaMinor() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readUint16(this.bb_pos + offset) : 0;
  }
  pixelFormat(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  timings(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new ScopeTiming()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  timingsLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  frameRate(obj) {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? (obj || new FrameRate()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  producer(obj) {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? (obj || new ProducerInfo()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  static startPerformanceMeasurements(builder) {
    builder.startObject(6);
  }
  static addSchemaMajor(builder, schemaMajor) {
    builder.addFieldInt16(0, schemaMajor, 1);
  }
  static addSchemaMinor(builder, schemaMinor) {
    builder.addFieldInt16(1, schemaMinor, 0);
  }
  static addPixelFormat(builder, pixelFormatOffset) {
    builder.addFieldOffset(2, pixelFormatOffset, 0);
  }
  static addTimings(builder, timingsOffset) {
    builder.addFieldOffset(3, timingsOffset, 0);
  }
  static createTimingsVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startTimingsVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static addFrameRate(builder, frameRateOffset) {
    builder.addFieldOffset(4, frameRateOffset, 0);
  }
  static addProducer(builder, producerOffset) {
    builder.addFieldOffset(5, producerOffset, 0);
  }
  static endPerformanceMeasurements(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishPerformanceMeasurementsBuffer(builder, offset) {
    builder.finish(offset, "PMET");
  }
  static finishSizePrefixedPerformanceMeasurementsBuffer(builder, offset) {
    builder.finish(offset, "PMET", true);
  }
  unpack() {
    return new PerformanceMeasurementsT(this.schemaMajor(), this.schemaMinor(), this.pixelFormat(), this.bb.createObjList(this.timings.bind(this), this.timingsLength()), this.frameRate() !== null ? this.frameRate().unpack() : null, this.producer() !== null ? this.producer().unpack() : null);
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.pixelFormat = this.pixelFormat();
    _o.timings = this.bb.createObjList(this.timings.bind(this), this.timingsLength());
    _o.frameRate = this.frameRate() !== null ? this.frameRate().unpack() : null;
    _o.producer = this.producer() !== null ? this.producer().unpack() : null;
  }
};
var PerformanceMeasurementsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, pixelFormat = null, timings = [], frameRate = null, producer = null) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.pixelFormat = pixelFormat;
    this.timings = timings;
    this.frameRate = frameRate;
    this.producer = producer;
  }
  pack(builder) {
    const pixelFormat = this.pixelFormat !== null ? builder.createString(this.pixelFormat) : 0;
    const timings = PerformanceMeasurements.createTimingsVector(builder, builder.createObjectOffsetList(this.timings));
    const frameRate = this.frameRate !== null ? this.frameRate.pack(builder) : 0;
    const producer = this.producer !== null ? this.producer.pack(builder) : 0;
    PerformanceMeasurements.startPerformanceMeasurements(builder);
    PerformanceMeasurements.addSchemaMajor(builder, this.schemaMajor);
    PerformanceMeasurements.addSchemaMinor(builder, this.schemaMinor);
    PerformanceMeasurements.addPixelFormat(builder, pixelFormat);
    PerformanceMeasurements.addTimings(builder, timings);
    PerformanceMeasurements.addFrameRate(builder, frameRate);
    PerformanceMeasurements.addProducer(builder, producer);
    return PerformanceMeasurements.endPerformanceMeasurements(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/person-presence-result.js
var PersonPresenceResult = class _PersonPresenceResult {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsPersonPresenceResult(bb, obj) {
    return (obj || new _PersonPresenceResult()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsPersonPresenceResult(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _PersonPresenceResult()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  personPresent() {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
  }
  yesScore() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : null;
  }
  noScore() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : null;
  }
  static startPersonPresenceResult(builder) {
    builder.startObject(3);
  }
  static addPersonPresent(builder, personPresent) {
    builder.addFieldInt8(0, +personPresent, 0);
  }
  static addYesScore(builder, yesScore) {
    builder.addFieldFloat32(1, yesScore, null);
  }
  static addNoScore(builder, noScore) {
    builder.addFieldFloat32(2, noScore, null);
  }
  static endPersonPresenceResult(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createPersonPresenceResult(builder, personPresent, yesScore, noScore) {
    _PersonPresenceResult.startPersonPresenceResult(builder);
    _PersonPresenceResult.addPersonPresent(builder, personPresent);
    if (yesScore !== null)
      _PersonPresenceResult.addYesScore(builder, yesScore);
    if (noScore !== null)
      _PersonPresenceResult.addNoScore(builder, noScore);
    return _PersonPresenceResult.endPersonPresenceResult(builder);
  }
  unpack() {
    return new PersonPresenceResultT(this.personPresent(), this.yesScore(), this.noScore());
  }
  unpackTo(_o) {
    _o.personPresent = this.personPresent();
    _o.yesScore = this.yesScore();
    _o.noScore = this.noScore();
  }
};
var PersonPresenceResultT = class {
  constructor(personPresent = false, yesScore = null, noScore = null) {
    this.personPresent = personPresent;
    this.yesScore = yesScore;
    this.noScore = noScore;
  }
  pack(builder) {
    return PersonPresenceResult.createPersonPresenceResult(builder, this.personPresent, this.yesScore, this.noScore);
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
  result(obj) {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? (obj || new PersonPresenceResult()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  subjectId() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : null;
  }
  static startPersonPresence(builder) {
    builder.startObject(3);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addResult(builder, resultOffset) {
    builder.addFieldOffset(1, resultOffset, 0);
  }
  static addSubjectId(builder, subjectId) {
    builder.addFieldInt64(2, subjectId, null);
  }
  static endPersonPresence(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new PersonPresenceT(this.object() !== null ? this.object().unpack() : null, this.result() !== null ? this.result().unpack() : null, this.subjectId());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.result = this.result() !== null ? this.result().unpack() : null;
    _o.subjectId = this.subjectId();
  }
};
var PersonPresenceT = class {
  constructor(object = null, result = null, subjectId = null) {
    this.object = object;
    this.result = result;
    this.subjectId = subjectId;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const result = this.result !== null ? this.result.pack(builder) : 0;
    PersonPresence.startPersonPresence(builder);
    PersonPresence.addObject(builder, object);
    PersonPresence.addResult(builder, result);
    if (this.subjectId !== null)
      PersonPresence.addSubjectId(builder, this.subjectId);
    return PersonPresence.endPersonPresence(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/person-presences.js
var PersonPresences = class _PersonPresences {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsPersonPresences(bb, obj) {
    return (obj || new _PersonPresences()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsPersonPresences(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _PersonPresences()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("PPRS");
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
  presences(index, obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new PersonPresence()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  presencesLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startPersonPresences(builder) {
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
  static addPresences(builder, presencesOffset) {
    builder.addFieldOffset(3, presencesOffset, 0);
  }
  static createPresencesVector(builder, data) {
    builder.startVector(4, data.length, 4);
    for (let i = data.length - 1; i >= 0; i--) {
      builder.addOffset(data[i]);
    }
    return builder.endVector();
  }
  static startPresencesVector(builder, numElems) {
    builder.startVector(4, numElems, 4);
  }
  static endPersonPresences(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishPersonPresencesBuffer(builder, offset) {
    builder.finish(offset, "PPRS");
  }
  static finishSizePrefixedPersonPresencesBuffer(builder, offset) {
    builder.finish(offset, "PPRS", true);
  }
  unpack() {
    return new PersonPresencesT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.presences.bind(this), this.presencesLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.presences = this.bb.createObjList(this.presences.bind(this), this.presencesLength());
  }
};
var PersonPresencesT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, presences = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.presences = presences;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const presences = PersonPresences.createPresencesVector(builder, builder.createObjectOffsetList(this.presences));
    PersonPresences.startPersonPresences(builder);
    PersonPresences.addSchemaMajor(builder, this.schemaMajor);
    PersonPresences.addSchemaMinor(builder, this.schemaMinor);
    PersonPresences.addLayer(builder, layer2);
    PersonPresences.addPresences(builder, presences);
    return PersonPresences.endPersonPresences(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/opacity-meaning.js
var OpacityMeaning;
(function(OpacityMeaning2) {
  OpacityMeaning2[OpacityMeaning2["Unspecified"] = 0] = "Unspecified";
  OpacityMeaning2[OpacityMeaning2["BackgroundOpacity"] = 1] = "BackgroundOpacity";
  OpacityMeaning2[OpacityMeaning2["RegionOpacity"] = 2] = "RegionOpacity";
})(OpacityMeaning || (OpacityMeaning = {}));

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
  meaning() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.readUint8(this.bb_pos + offset) : OpacityMeaning.Unspecified;
  }
  static startSegmentationMask(builder) {
    builder.startObject(3);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addBitmap(builder, bitmapOffset) {
    builder.addFieldOffset(1, bitmapOffset, 0);
  }
  static addMeaning(builder, meaning) {
    builder.addFieldInt8(2, meaning, OpacityMeaning.Unspecified);
  }
  static endSegmentationMask(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new SegmentationMaskT(this.object() !== null ? this.object().unpack() : null, this.bitmap() !== null ? this.bitmap().unpack() : null, this.meaning());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.bitmap = this.bitmap() !== null ? this.bitmap().unpack() : null;
    _o.meaning = this.meaning();
  }
};
var SegmentationMaskT = class {
  constructor(object = null, bitmap = null, meaning = OpacityMeaning.Unspecified) {
    this.object = object;
    this.bitmap = bitmap;
    this.meaning = meaning;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const bitmap = this.bitmap !== null ? this.bitmap.pack(builder) : 0;
    SegmentationMask.startSegmentationMask(builder);
    SegmentationMask.addObject(builder, object);
    SegmentationMask.addBitmap(builder, bitmap);
    SegmentationMask.addMeaning(builder, this.meaning);
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
  labelSet(optionalEncoding) {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? this.bb.__string(this.bb_pos + offset, optionalEncoding) : null;
  }
  voidValue() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readUint32(this.bb_pos + offset) : null;
  }
  static startSemanticSegmentationMask(builder) {
    builder.startObject(4);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addBitmap(builder, bitmapOffset) {
    builder.addFieldOffset(1, bitmapOffset, 0);
  }
  static addLabelSet(builder, labelSetOffset) {
    builder.addFieldOffset(2, labelSetOffset, 0);
  }
  static addVoidValue(builder, voidValue) {
    builder.addFieldInt32(3, voidValue, null);
  }
  static endSemanticSegmentationMask(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new SemanticSegmentationMaskT(this.object() !== null ? this.object().unpack() : null, this.bitmap() !== null ? this.bitmap().unpack() : null, this.labelSet(), this.voidValue());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.bitmap = this.bitmap() !== null ? this.bitmap().unpack() : null;
    _o.labelSet = this.labelSet();
    _o.voidValue = this.voidValue();
  }
};
var SemanticSegmentationMaskT = class {
  constructor(object = null, bitmap = null, labelSet = null, voidValue = null) {
    this.object = object;
    this.bitmap = bitmap;
    this.labelSet = labelSet;
    this.voidValue = voidValue;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const bitmap = this.bitmap !== null ? this.bitmap.pack(builder) : 0;
    const labelSet = this.labelSet !== null ? builder.createString(this.labelSet) : 0;
    SemanticSegmentationMask.startSemanticSegmentationMask(builder);
    SemanticSegmentationMask.addObject(builder, object);
    SemanticSegmentationMask.addBitmap(builder, bitmap);
    SemanticSegmentationMask.addLabelSet(builder, labelSet);
    if (this.voidValue !== null)
      SemanticSegmentationMask.addVoidValue(builder, this.voidValue);
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
  space(obj) {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? (obj || new ImageSpace()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  static startTrackTrace(builder) {
    builder.startObject(4);
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
  static addSpace(builder, spaceOffset) {
    builder.addFieldOffset(3, spaceOffset, 0);
  }
  static endTrackTrace(builder) {
    const offset = builder.endObject();
    return offset;
  }
  unpack() {
    return new TrackTraceT(this.object() !== null ? this.object().unpack() : null, this.trackId(), this.bb.createObjList(this.points.bind(this), this.pointsLength()), this.space() !== null ? this.space().unpack() : null);
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.trackId = this.trackId();
    _o.points = this.bb.createObjList(this.points.bind(this), this.pointsLength());
    _o.space = this.space() !== null ? this.space().unpack() : null;
  }
};
var TrackTraceT = class {
  constructor(object = null, trackId = BigInt("0"), points = [], space = null) {
    this.object = object;
    this.trackId = trackId;
    this.points = points;
    this.space = space;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    const points = TrackTrace.createPointsVector(builder, builder.createObjectOffsetList(this.points));
    const space = this.space !== null ? this.space.pack(builder) : 0;
    TrackTrace.startTrackTrace(builder);
    TrackTrace.addObject(builder, object);
    TrackTrace.addTrackId(builder, this.trackId);
    TrackTrace.addPoints(builder, points);
    TrackTrace.addSpace(builder, space);
    return TrackTrace.endTrackTrace(builder);
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

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/turn-detection.js
var TurnDetection = class _TurnDetection {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsTurnDetection(bb, obj) {
    return (obj || new _TurnDetection()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsTurnDetection(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _TurnDetection()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  turnProbability() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  turnComplete() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
  }
  windowStartSample() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  windowEndSample() {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  windowStartTsNs() {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : null;
  }
  windowEndTsNs() {
    const offset = this.bb.__offset(this.bb_pos, 16);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : null;
  }
  discontinuity() {
    const offset = this.bb.__offset(this.bb_pos, 18);
    return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
  }
  static startTurnDetection(builder) {
    builder.startObject(8);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addTurnProbability(builder, turnProbability) {
    builder.addFieldFloat32(1, turnProbability, 0);
  }
  static addTurnComplete(builder, turnComplete) {
    builder.addFieldInt8(2, +turnComplete, 0);
  }
  static addWindowStartSample(builder, windowStartSample) {
    builder.addFieldInt64(3, windowStartSample, BigInt("0"));
  }
  static addWindowEndSample(builder, windowEndSample) {
    builder.addFieldInt64(4, windowEndSample, BigInt("0"));
  }
  static addWindowStartTsNs(builder, windowStartTsNs) {
    builder.addFieldInt64(5, windowStartTsNs, null);
  }
  static addWindowEndTsNs(builder, windowEndTsNs) {
    builder.addFieldInt64(6, windowEndTsNs, null);
  }
  static addDiscontinuity(builder, discontinuity) {
    builder.addFieldInt8(7, +discontinuity, 0);
  }
  static endTurnDetection(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createTurnDetection(builder, objectOffset, turnProbability, turnComplete, windowStartSample, windowEndSample, windowStartTsNs, windowEndTsNs, discontinuity) {
    _TurnDetection.startTurnDetection(builder);
    _TurnDetection.addObject(builder, objectOffset);
    _TurnDetection.addTurnProbability(builder, turnProbability);
    _TurnDetection.addTurnComplete(builder, turnComplete);
    _TurnDetection.addWindowStartSample(builder, windowStartSample);
    _TurnDetection.addWindowEndSample(builder, windowEndSample);
    if (windowStartTsNs !== null)
      _TurnDetection.addWindowStartTsNs(builder, windowStartTsNs);
    if (windowEndTsNs !== null)
      _TurnDetection.addWindowEndTsNs(builder, windowEndTsNs);
    _TurnDetection.addDiscontinuity(builder, discontinuity);
    return _TurnDetection.endTurnDetection(builder);
  }
  unpack() {
    return new TurnDetectionT(this.object() !== null ? this.object().unpack() : null, this.turnProbability(), this.turnComplete(), this.windowStartSample(), this.windowEndSample(), this.windowStartTsNs(), this.windowEndTsNs(), this.discontinuity());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.turnProbability = this.turnProbability();
    _o.turnComplete = this.turnComplete();
    _o.windowStartSample = this.windowStartSample();
    _o.windowEndSample = this.windowEndSample();
    _o.windowStartTsNs = this.windowStartTsNs();
    _o.windowEndTsNs = this.windowEndTsNs();
    _o.discontinuity = this.discontinuity();
  }
};
var TurnDetectionT = class {
  constructor(object = null, turnProbability = 0, turnComplete = false, windowStartSample = BigInt("0"), windowEndSample = BigInt("0"), windowStartTsNs = null, windowEndTsNs = null, discontinuity = false) {
    this.object = object;
    this.turnProbability = turnProbability;
    this.turnComplete = turnComplete;
    this.windowStartSample = windowStartSample;
    this.windowEndSample = windowEndSample;
    this.windowStartTsNs = windowStartTsNs;
    this.windowEndTsNs = windowEndTsNs;
    this.discontinuity = discontinuity;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    return TurnDetection.createTurnDetection(builder, object, this.turnProbability, this.turnComplete, this.windowStartSample, this.windowEndSample, this.windowStartTsNs, this.windowEndTsNs, this.discontinuity);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/turn-detections.js
var TurnDetections = class _TurnDetections {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsTurnDetections(bb, obj) {
    return (obj || new _TurnDetections()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsTurnDetections(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _TurnDetections()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("TURN");
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
    return offset ? (obj || new TurnDetection()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  detectionsLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startTurnDetections(builder) {
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
  static endTurnDetections(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishTurnDetectionsBuffer(builder, offset) {
    builder.finish(offset, "TURN");
  }
  static finishSizePrefixedTurnDetectionsBuffer(builder, offset) {
    builder.finish(offset, "TURN", true);
  }
  unpack() {
    return new TurnDetectionsT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.detections.bind(this), this.detectionsLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.detections = this.bb.createObjList(this.detections.bind(this), this.detectionsLength());
  }
};
var TurnDetectionsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, detections = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.detections = detections;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const detections = TurnDetections.createDetectionsVector(builder, builder.createObjectOffsetList(this.detections));
    TurnDetections.startTurnDetections(builder);
    TurnDetections.addSchemaMajor(builder, this.schemaMajor);
    TurnDetections.addSchemaMinor(builder, this.schemaMinor);
    TurnDetections.addLayer(builder, layer2);
    TurnDetections.addDetections(builder, detections);
    return TurnDetections.endTurnDetections(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/voice-activity-detection.js
var VoiceActivityDetection = class _VoiceActivityDetection {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsVoiceActivityDetection(bb, obj) {
    return (obj || new _VoiceActivityDetection()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsVoiceActivityDetection(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _VoiceActivityDetection()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  object(obj) {
    const offset = this.bb.__offset(this.bb_pos, 4);
    return offset ? (obj || new ObjectMeta()).__init(this.bb.__indirect(this.bb_pos + offset), this.bb) : null;
  }
  speechProbability() {
    const offset = this.bb.__offset(this.bb_pos, 6);
    return offset ? this.bb.readFloat32(this.bb_pos + offset) : 0;
  }
  speechActive() {
    const offset = this.bb.__offset(this.bb_pos, 8);
    return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
  }
  windowStartSample() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  windowEndSample() {
    const offset = this.bb.__offset(this.bb_pos, 12);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : BigInt("0");
  }
  windowStartTsNs() {
    const offset = this.bb.__offset(this.bb_pos, 14);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : null;
  }
  windowEndTsNs() {
    const offset = this.bb.__offset(this.bb_pos, 16);
    return offset ? this.bb.readUint64(this.bb_pos + offset) : null;
  }
  discontinuity() {
    const offset = this.bb.__offset(this.bb_pos, 18);
    return offset ? !!this.bb.readInt8(this.bb_pos + offset) : false;
  }
  static startVoiceActivityDetection(builder) {
    builder.startObject(8);
  }
  static addObject(builder, objectOffset) {
    builder.addFieldOffset(0, objectOffset, 0);
  }
  static addSpeechProbability(builder, speechProbability) {
    builder.addFieldFloat32(1, speechProbability, 0);
  }
  static addSpeechActive(builder, speechActive) {
    builder.addFieldInt8(2, +speechActive, 0);
  }
  static addWindowStartSample(builder, windowStartSample) {
    builder.addFieldInt64(3, windowStartSample, BigInt("0"));
  }
  static addWindowEndSample(builder, windowEndSample) {
    builder.addFieldInt64(4, windowEndSample, BigInt("0"));
  }
  static addWindowStartTsNs(builder, windowStartTsNs) {
    builder.addFieldInt64(5, windowStartTsNs, null);
  }
  static addWindowEndTsNs(builder, windowEndTsNs) {
    builder.addFieldInt64(6, windowEndTsNs, null);
  }
  static addDiscontinuity(builder, discontinuity) {
    builder.addFieldInt8(7, +discontinuity, 0);
  }
  static endVoiceActivityDetection(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static createVoiceActivityDetection(builder, objectOffset, speechProbability, speechActive, windowStartSample, windowEndSample, windowStartTsNs, windowEndTsNs, discontinuity) {
    _VoiceActivityDetection.startVoiceActivityDetection(builder);
    _VoiceActivityDetection.addObject(builder, objectOffset);
    _VoiceActivityDetection.addSpeechProbability(builder, speechProbability);
    _VoiceActivityDetection.addSpeechActive(builder, speechActive);
    _VoiceActivityDetection.addWindowStartSample(builder, windowStartSample);
    _VoiceActivityDetection.addWindowEndSample(builder, windowEndSample);
    if (windowStartTsNs !== null)
      _VoiceActivityDetection.addWindowStartTsNs(builder, windowStartTsNs);
    if (windowEndTsNs !== null)
      _VoiceActivityDetection.addWindowEndTsNs(builder, windowEndTsNs);
    _VoiceActivityDetection.addDiscontinuity(builder, discontinuity);
    return _VoiceActivityDetection.endVoiceActivityDetection(builder);
  }
  unpack() {
    return new VoiceActivityDetectionT(this.object() !== null ? this.object().unpack() : null, this.speechProbability(), this.speechActive(), this.windowStartSample(), this.windowEndSample(), this.windowStartTsNs(), this.windowEndTsNs(), this.discontinuity());
  }
  unpackTo(_o) {
    _o.object = this.object() !== null ? this.object().unpack() : null;
    _o.speechProbability = this.speechProbability();
    _o.speechActive = this.speechActive();
    _o.windowStartSample = this.windowStartSample();
    _o.windowEndSample = this.windowEndSample();
    _o.windowStartTsNs = this.windowStartTsNs();
    _o.windowEndTsNs = this.windowEndTsNs();
    _o.discontinuity = this.discontinuity();
  }
};
var VoiceActivityDetectionT = class {
  constructor(object = null, speechProbability = 0, speechActive = false, windowStartSample = BigInt("0"), windowEndSample = BigInt("0"), windowStartTsNs = null, windowEndTsNs = null, discontinuity = false) {
    this.object = object;
    this.speechProbability = speechProbability;
    this.speechActive = speechActive;
    this.windowStartSample = windowStartSample;
    this.windowEndSample = windowEndSample;
    this.windowStartTsNs = windowStartTsNs;
    this.windowEndTsNs = windowEndTsNs;
    this.discontinuity = discontinuity;
  }
  pack(builder) {
    const object = this.object !== null ? this.object.pack(builder) : 0;
    return VoiceActivityDetection.createVoiceActivityDetection(builder, object, this.speechProbability, this.speechActive, this.windowStartSample, this.windowEndSample, this.windowStartTsNs, this.windowEndTsNs, this.discontinuity);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/voice-activity-detections.js
var VoiceActivityDetections = class _VoiceActivityDetections {
  constructor() {
    this.bb = null;
    this.bb_pos = 0;
  }
  __init(i, bb) {
    this.bb_pos = i;
    this.bb = bb;
    return this;
  }
  static getRootAsVoiceActivityDetections(bb, obj) {
    return (obj || new _VoiceActivityDetections()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static getSizePrefixedRootAsVoiceActivityDetections(bb, obj) {
    bb.setPosition(bb.position() + SIZE_PREFIX_LENGTH);
    return (obj || new _VoiceActivityDetections()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
  }
  static bufferHasIdentifier(bb) {
    return bb.__has_identifier("VACT");
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
    return offset ? (obj || new VoiceActivityDetection()).__init(this.bb.__indirect(this.bb.__vector(this.bb_pos + offset) + index * 4), this.bb) : null;
  }
  detectionsLength() {
    const offset = this.bb.__offset(this.bb_pos, 10);
    return offset ? this.bb.__vector_len(this.bb_pos + offset) : 0;
  }
  static startVoiceActivityDetections(builder) {
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
  static endVoiceActivityDetections(builder) {
    const offset = builder.endObject();
    return offset;
  }
  static finishVoiceActivityDetectionsBuffer(builder, offset) {
    builder.finish(offset, "VACT");
  }
  static finishSizePrefixedVoiceActivityDetectionsBuffer(builder, offset) {
    builder.finish(offset, "VACT", true);
  }
  unpack() {
    return new VoiceActivityDetectionsT(this.schemaMajor(), this.schemaMinor(), this.layer() !== null ? this.layer().unpack() : null, this.bb.createObjList(this.detections.bind(this), this.detectionsLength()));
  }
  unpackTo(_o) {
    _o.schemaMajor = this.schemaMajor();
    _o.schemaMinor = this.schemaMinor();
    _o.layer = this.layer() !== null ? this.layer().unpack() : null;
    _o.detections = this.bb.createObjList(this.detections.bind(this), this.detectionsLength());
  }
};
var VoiceActivityDetectionsT = class {
  constructor(schemaMajor = 1, schemaMinor = 0, layer2 = null, detections = []) {
    this.schemaMajor = schemaMajor;
    this.schemaMinor = schemaMinor;
    this.layer = layer2;
    this.detections = detections;
  }
  pack(builder) {
    const layer2 = this.layer !== null ? this.layer.pack(builder) : 0;
    const detections = VoiceActivityDetections.createDetectionsVector(builder, builder.createObjectOffsetList(this.detections));
    VoiceActivityDetections.startVoiceActivityDetections(builder);
    VoiceActivityDetections.addSchemaMajor(builder, this.schemaMajor);
    VoiceActivityDetections.addSchemaMinor(builder, this.schemaMinor);
    VoiceActivityDetections.addLayer(builder, layer2);
    VoiceActivityDetections.addDetections(builder, detections);
    return VoiceActivityDetections.endVoiceActivityDetections(builder);
  }
};

// generated/open_perception_kit/ts/dist/open_perception_kit/registry.js
var decode_3248104387212218840 = (blob) => BoxDetections.getRootAsBoxDetections(new ByteBuffer(blob)).unpack();
var verify_3248104387212218840 = (blob) => BoxDetections.bufferHasIdentifier(new ByteBuffer(blob));
var decode_7630654510173219138 = (blob) => CameraContacts.getRootAsCameraContacts(new ByteBuffer(blob)).unpack();
var verify_7630654510173219138 = (blob) => CameraContacts.bufferHasIdentifier(new ByteBuffer(blob));
var decode_3375056508603945320 = (blob) => Classifications.getRootAsClassifications(new ByteBuffer(blob)).unpack();
var verify_3375056508603945320 = (blob) => Classifications.bufferHasIdentifier(new ByteBuffer(blob));
var decode_5246681475592864076 = (blob) => FrameContext.getRootAsFrameContext(new ByteBuffer(blob)).unpack();
var verify_5246681475592864076 = (blob) => FrameContext.bufferHasIdentifier(new ByteBuffer(blob));
var decode_2646100978796938776 = (blob) => GazeMeasurements.getRootAsGazeMeasurements(new ByteBuffer(blob)).unpack();
var verify_2646100978796938776 = (blob) => GazeMeasurements.bufferHasIdentifier(new ByteBuffer(blob));
var decode_7634499802271603959 = (blob) => InstanceSegmentationMasks.getRootAsInstanceSegmentationMasks(new ByteBuffer(blob)).unpack();
var verify_7634499802271603959 = (blob) => InstanceSegmentationMasks.bufferHasIdentifier(new ByteBuffer(blob));
var decode_6121378805478677067 = (blob) => ObjectEmbeddings.getRootAsObjectEmbeddings(new ByteBuffer(blob)).unpack();
var verify_6121378805478677067 = (blob) => ObjectEmbeddings.bufferHasIdentifier(new ByteBuffer(blob));
var decode_5316059591418463553 = (blob) => ObjectTracks.getRootAsObjectTracks(new ByteBuffer(blob)).unpack();
var verify_5316059591418463553 = (blob) => ObjectTracks.bufferHasIdentifier(new ByteBuffer(blob));
var decode_2630163151625760217 = (blob) => PerformanceMeasurements.getRootAsPerformanceMeasurements(new ByteBuffer(blob)).unpack();
var verify_2630163151625760217 = (blob) => PerformanceMeasurements.bufferHasIdentifier(new ByteBuffer(blob));
var decode_7550522492754457054 = (blob) => PersonPresences.getRootAsPersonPresences(new ByteBuffer(blob)).unpack();
var verify_7550522492754457054 = (blob) => PersonPresences.bufferHasIdentifier(new ByteBuffer(blob));
var decode_3128408959875976772 = (blob) => SegmentationMasks.getRootAsSegmentationMasks(new ByteBuffer(blob)).unpack();
var verify_3128408959875976772 = (blob) => SegmentationMasks.bufferHasIdentifier(new ByteBuffer(blob));
var decode_133049182594848948 = (blob) => SemanticSegmentationMasks.getRootAsSemanticSegmentationMasks(new ByteBuffer(blob)).unpack();
var verify_133049182594848948 = (blob) => SemanticSegmentationMasks.bufferHasIdentifier(new ByteBuffer(blob));
var decode_7618457940451616939 = (blob) => TrackTraces.getRootAsTrackTraces(new ByteBuffer(blob)).unpack();
var verify_7618457940451616939 = (blob) => TrackTraces.bufferHasIdentifier(new ByteBuffer(blob));
var decode_7194963704333257544 = (blob) => TurnDetections.getRootAsTurnDetections(new ByteBuffer(blob)).unpack();
var verify_7194963704333257544 = (blob) => TurnDetections.bufferHasIdentifier(new ByteBuffer(blob));
var decode_4750671262425495264 = (blob) => VoiceActivityDetections.getRootAsVoiceActivityDetections(new ByteBuffer(blob)).unpack();
var verify_4750671262425495264 = (blob) => VoiceActivityDetections.bufferHasIdentifier(new ByteBuffer(blob));
var _TYPE_REGISTRY = /* @__PURE__ */ new Map([
  [3248104387212218840n, {
    name: "open_perception_kit::metadata::BoxDetections",
    root_type: "BoxDetections",
    qualified_root_type: "open_perception_kit.metadata.BoxDetections",
    file_identifier: "BDET",
    decode: decode_3248104387212218840,
    verify: verify_3248104387212218840
  }],
  [7630654510173219138n, {
    name: "open_perception_kit::metadata::CameraContacts",
    root_type: "CameraContacts",
    qualified_root_type: "open_perception_kit.metadata.CameraContacts",
    file_identifier: "CCON",
    decode: decode_7630654510173219138,
    verify: verify_7630654510173219138
  }],
  [3375056508603945320n, {
    name: "open_perception_kit::metadata::Classifications",
    root_type: "Classifications",
    qualified_root_type: "open_perception_kit.metadata.Classifications",
    file_identifier: "CLSF",
    decode: decode_3375056508603945320,
    verify: verify_3375056508603945320
  }],
  [5246681475592864076n, {
    name: "open_perception_kit::metadata::FrameContext",
    root_type: "FrameContext",
    qualified_root_type: "open_perception_kit.metadata.FrameContext",
    file_identifier: "FCTX",
    decode: decode_5246681475592864076,
    verify: verify_5246681475592864076
  }],
  [2646100978796938776n, {
    name: "open_perception_kit::metadata::GazeMeasurements",
    root_type: "GazeMeasurements",
    qualified_root_type: "open_perception_kit.metadata.GazeMeasurements",
    file_identifier: "GZMS",
    decode: decode_2646100978796938776,
    verify: verify_2646100978796938776
  }],
  [7634499802271603959n, {
    name: "open_perception_kit::metadata::InstanceSegmentationMasks",
    root_type: "InstanceSegmentationMasks",
    qualified_root_type: "open_perception_kit.metadata.InstanceSegmentationMasks",
    file_identifier: "ISGM",
    decode: decode_7634499802271603959,
    verify: verify_7634499802271603959
  }],
  [6121378805478677067n, {
    name: "open_perception_kit::metadata::ObjectEmbeddings",
    root_type: "ObjectEmbeddings",
    qualified_root_type: "open_perception_kit.metadata.ObjectEmbeddings",
    file_identifier: "EMBE",
    decode: decode_6121378805478677067,
    verify: verify_6121378805478677067
  }],
  [5316059591418463553n, {
    name: "open_perception_kit::metadata::ObjectTracks",
    root_type: "ObjectTracks",
    qualified_root_type: "open_perception_kit.metadata.ObjectTracks",
    file_identifier: "TRKS",
    decode: decode_5316059591418463553,
    verify: verify_5316059591418463553
  }],
  [2630163151625760217n, {
    name: "open_perception_kit::metadata::PerformanceMeasurements",
    root_type: "PerformanceMeasurements",
    qualified_root_type: "open_perception_kit.metadata.PerformanceMeasurements",
    file_identifier: "PMET",
    decode: decode_2630163151625760217,
    verify: verify_2630163151625760217
  }],
  [7550522492754457054n, {
    name: "open_perception_kit::metadata::PersonPresences",
    root_type: "PersonPresences",
    qualified_root_type: "open_perception_kit.metadata.PersonPresences",
    file_identifier: "PPRS",
    decode: decode_7550522492754457054,
    verify: verify_7550522492754457054
  }],
  [3128408959875976772n, {
    name: "open_perception_kit::metadata::SegmentationMasks",
    root_type: "SegmentationMasks",
    qualified_root_type: "open_perception_kit.metadata.SegmentationMasks",
    file_identifier: "SGMS",
    decode: decode_3128408959875976772,
    verify: verify_3128408959875976772
  }],
  [133049182594848948n, {
    name: "open_perception_kit::metadata::SemanticSegmentationMasks",
    root_type: "SemanticSegmentationMasks",
    qualified_root_type: "open_perception_kit.metadata.SemanticSegmentationMasks",
    file_identifier: "SSGM",
    decode: decode_133049182594848948,
    verify: verify_133049182594848948
  }],
  [7618457940451616939n, {
    name: "open_perception_kit::metadata::TrackTraces",
    root_type: "TrackTraces",
    qualified_root_type: "open_perception_kit.metadata.TrackTraces",
    file_identifier: "TRCE",
    decode: decode_7618457940451616939,
    verify: verify_7618457940451616939
  }],
  [7194963704333257544n, {
    name: "open_perception_kit::metadata::TurnDetections",
    root_type: "TurnDetections",
    qualified_root_type: "open_perception_kit.metadata.TurnDetections",
    file_identifier: "TURN",
    decode: decode_7194963704333257544,
    verify: verify_7194963704333257544
  }],
  [4750671262425495264n, {
    name: "open_perception_kit::metadata::VoiceActivityDetections",
    root_type: "VoiceActivityDetections",
    qualified_root_type: "open_perception_kit.metadata.VoiceActivityDetections",
    file_identifier: "VACT",
    decode: decode_4750671262425495264,
    verify: verify_4750671262425495264
  }]
]);
var _CLASS_TO_ID = /* @__PURE__ */ new Map([
  [BoxDetectionsT, 3248104387212218840n],
  [CameraContactsT, 7630654510173219138n],
  [ClassificationsT, 3375056508603945320n],
  [FrameContextT, 5246681475592864076n],
  [GazeMeasurementsT, 2646100978796938776n],
  [InstanceSegmentationMasksT, 7634499802271603959n],
  [ObjectEmbeddingsT, 6121378805478677067n],
  [ObjectTracksT, 5316059591418463553n],
  [PerformanceMeasurementsT, 2630163151625760217n],
  [PersonPresencesT, 7550522492754457054n],
  [SegmentationMasksT, 3128408959875976772n],
  [SemanticSegmentationMasksT, 133049182594848948n],
  [TrackTracesT, 7618457940451616939n],
  [TurnDetectionsT, 7194963704333257544n],
  [VoiceActivityDetectionsT, 4750671262425495264n]
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
var SDK_VERSION = "1.0.0";
var SCHEMA_SET_SHA256 = "b500edeafa21c9cc735b38a334cf85ebebf123e913b9c02c41f544c25c3a539c";
var EXTERNAL_KEY_MIN = BigInt("9223372036854775808");
var EXTERNAL_KEY_MASK = EXTERNAL_KEY_MIN - BigInt(1);
var EXTERNAL_HASH_OFFSET = BigInt("14695981039346656037");
var EXTERNAL_HASH_PRIME = BigInt("1099511628211");
var UINT64_MASK = BigInt("0xffffffffffffffff");
var textEncoder = new TextEncoder();
var externalKeyToken = /* @__PURE__ */ Symbol("external-key");
var SEMANTIC_VERSION_PATTERN = /^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/;
var ProducerIdentityStatus;
(function(ProducerIdentityStatus2) {
  ProducerIdentityStatus2["NameMismatch"] = "name_mismatch";
  ProducerIdentityStatus2["ExactMatch"] = "exact_match";
  ProducerIdentityStatus2["CompatibleMatch"] = "compatible_match";
  ProducerIdentityStatus2["VersionMismatch"] = "version_mismatch";
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
      this.payloadEntries = [];
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
    if (this.producerName !== SDK_NAME)
      return ProducerIdentityStatus.NameMismatch;
    const version = SEMANTIC_VERSION_PATTERN.exec(this.producerVersion);
    if (!version || version[0] !== this.producerVersion) {
      return ProducerIdentityStatus.VersionMismatch;
    }
    if (this.producerVersion === SDK_VERSION)
      return ProducerIdentityStatus.ExactMatch;
    if (version[1] === SDK_VERSION.split(".")[0]) {
      return ProducerIdentityStatus.CompatibleMatch;
    }
    return ProducerIdentityStatus.VersionMismatch;
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
    if (!this.validEnvelope)
      return;
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

// development/web/src/image-geometry.js
function validImageSpace(space) {
  const id = space?.frameId;
  return (typeof id === "bigint" ? id > 0n : Number.isSafeInteger(id) && id > 0) && Number.isInteger(space.width) && space.width > 0 && Number.isInteger(space.height) && space.height > 0 && [space.originX, space.originY, space.extentWidth, space.extentHeight].every(Number.isFinite) && space.originX >= 0 && space.originY >= 0 && space.extentWidth > 0 && space.extentHeight > 0 && space.originX + space.extentWidth <= space.width && space.originY + space.extentHeight <= space.height;
}
function imagePoint(x, y, space) {
  return { x: space.originX + x, y: space.originY + y };
}
function imageBox(box) {
  const space = box?.space;
  if (!validImageSpace(space) || ![box.x, box.y, box.width, box.height].every(Number.isFinite) || box.width <= 0 || box.height <= 0 || ![CoordinateUnits.Pixels, CoordinateUnits.Normalized].includes(box.units)) return null;
  const unitX = box.units === CoordinateUnits.Normalized ? space.extentWidth : 1;
  const unitY = box.units === CoordinateUnits.Normalized ? space.extentHeight : 1;
  return {
    ...imagePoint(box.x * unitX, box.y * unitY, space),
    width: box.width * unitX,
    height: box.height * unitY,
    frameWidth: space.width,
    frameHeight: space.height
  };
}

// development/web/src/mask-data.js
function validBitmap(data) {
  const bytes = data?.encoding === BitmapEncoding.Uint8 ? 1 : data?.encoding === BitmapEncoding.Uint32LE ? 4 : 0;
  return bytes > 0 && validImageSpace(data.space) && Number.isInteger(data.width) && data.width > 0 && data.width <= 4294967295 && Number.isInteger(data.height) && data.height > 0 && data.height <= 4294967295 && Number.isSafeInteger(data.width * data.height * bytes) && data.pixels instanceof Uint8Array && data.pixels.length === data.width * data.height * bytes;
}
function validMask(data, type) {
  if (!validBitmap(data)) return false;
  if (type === "SegmentationMask") return data.encoding === BitmapEncoding.Uint8 && [OpacityMeaning.BackgroundOpacity, OpacityMeaning.RegionOpacity].includes(data.meaning);
  const identified = typeof data.labelSet === "string" && data.labelSet.length > 0;
  if (type === "SemanticSegmentationMask") return identified && (data.voidValue == null || Number.isInteger(data.voidValue) && data.voidValue >= 0 && data.voidValue <= (data.encoding === BitmapEncoding.Uint8 ? 255 : 4294967295));
  if (type !== "InstanceSegmentationMask" || data.encoding !== BitmapEncoding.Uint8) return false;
  if (data.classId != null && (!identified || !Number.isInteger(data.classId) || data.classId < 0 || data.classId > 4294967295)) return false;
  if (data.confidence != null && (!Number.isFinite(data.confidence) || data.confidence < 0 || data.confidence > 1)) return false;
  if (data.box && (!imageBox(data.box) || String(data.box.space.frameId) !== String(data.space.frameId) || data.box.space.width !== data.space.width || data.box.space.height !== data.space.height)) return false;
  return data.pixels.every((value) => value <= 1);
}
function maskData(item, type) {
  const bitmap = item?.bitmap;
  const data = {
    width: bitmap?.width,
    height: bitmap?.height,
    encoding: bitmap?.encoding,
    pixels: bitmap?.pixels != null ? Uint8Array.from(bitmap.pixels) : void 0,
    space: bitmap?.space,
    meaning: item?.meaning,
    labelSet: item?.labelSet,
    voidValue: item?.voidValue,
    classId: item?.classId,
    confidence: item?.confidence,
    box: item?.box
  };
  return { ...data, valid: validMask(data, type) };
}

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
  const geometry = imageBox(item.box);
  return {
    ...objectData(item.object),
    x: Number(item.box?.x ?? 0),
    y: Number(item.box?.y ?? 0),
    width: Number(item.box?.width ?? 0),
    height: Number(item.box?.height ?? 0),
    ...geometry,
    spatialValid: geometry !== null,
    confidence: item.confidence,
    classId: item.classId,
    text: text(item.text)
  };
}
function candidateData(candidate) {
  return {
    confidence: candidate.confidence,
    classId: candidate.classId,
    text: text(candidate.text)
  };
}
function addPayloadLayers(envelope, payloadType, name, mapper, layers) {
  for (const payload of envelope.for_each(payloadType)) {
    layers.push(layer(payload, name, mapper(payload)));
  }
}
var classificationMappers = /* @__PURE__ */ new Map([
  ["Classifications", (payload) => payload.classifications.map((item) => ({ type: "Classification", data: {
    ...objectData(item.object),
    candidates: item.candidates.map(candidateData),
    labelSet: text(item.labelSet),
    kind: item.kind,
    subjectUuid: item.subjectId == null ? null : identifier(item.subjectId)
  } }))],
  ["CameraContacts", (payload) => payload.contacts.map((item) => ({ type: "CameraContact", data: {
    ...objectData(item.object),
    subjectUuid: item.subjectId == null ? null : identifier(item.subjectId),
    result: item.result ? {
      inContact: item.result.inContact,
      modelClassIndex: item.result.modelClassIndex,
      confidence: item.result.confidence
    } : null
  } }))],
  ["PersonPresences", (payload) => payload.presences.map((item) => ({ type: "PersonClassification", data: {
    ...objectData(item.object),
    result: item.result ? {
      personPresent: item.result.personPresent,
      yesScore: item.result.yesScore,
      noScore: item.result.noScore
    } : null,
    subjectUuid: item.subjectId == null ? null : identifier(item.subjectId)
  } }))]
]);
function addClassificationLayers(packet, layers) {
  const wire = WireEnvelope.getRootAsWireEnvelope(new ByteBuffer(packet));
  for (let index = 0; index < wire.payloadsLength(); index++) {
    const entry = wire.payloads(index);
    const info = entry && _TYPE_REGISTRY.get(entry.id());
    const mapper = classificationMappers.get(info?.root_type);
    if (!mapper) continue;
    const blob = entry.blobArray();
    if (!blob) continue;
    let payload;
    try {
      const copy = new Uint8Array(blob);
      if (info.verify && !info.verify(copy)) continue;
      payload = info.decode(copy);
    } catch {
      continue;
    }
    layers.push(layer(payload, info.root_type, mapper(payload)));
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
  const bytes = new Uint8Array(decoded.length);
  for (let index = 0; index < decoded.length; index++) bytes[index] = decoded.charCodeAt(index);
  return bytes;
}
function decodeFrameResultsMessage(message) {
  if (message?.frame_results_encoding !== FRAME_RESULTS_ENCODING) {
    throw new FrameResultsDecodeError(
      `unsupported frame_results_encoding: ${String(message?.frame_results_encoding)}`
    );
  }
  const packet = decodeBase64(message.frame_results_packet_b64);
  const envelope = new Envelope(packet);
  if (!envelope.valid()) {
    throw new FrameResultsDecodeError(
      `invalid frame results packet: ${envelope.error() || "unknown error"}`
    );
  }
  const producerIdentity = envelope.producerIdentity();
  if (![ProducerIdentityStatus.ExactMatch, ProducerIdentityStatus.CompatibleMatch].includes(producerIdentity)) {
    throw new FrameResultsDecodeError(
      `incompatible frame results producer identity: ${producerIdentity}`
    );
  }
  const layers = [];
  const performance2 = [];
  addPayloadLayers(envelope, FrameContextT, "FrameContext", (payload) => {
    const detections = [];
    if (payload.video) {
      detections.push({
        type: "VideoFrame",
        data: {
          ...objectData(payload.video.object),
          originalWidth: Number(payload.video.originalWidth),
          originalHeight: Number(payload.video.originalHeight),
          presentationTsNs: payload.video.presentationTsNs === null ? null : identifier(payload.video.presentationTsNs)
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
  addClassificationLayers(packet, layers);
  addPayloadLayers(envelope, GazeMeasurementsT, "GazeMeasurements", (payload) => payload.measurements.map((item) => ({
    type: "GazeMeasurement",
    data: {
      ...objectData(item.object),
      subjectUuid: item.subjectId == null ? null : identifier(item.subjectId),
      confidence: item.confidence,
      angles: item.angles ? {
        yawDegrees: item.angles.yawDegrees,
        pitchDegrees: item.angles.pitchDegrees
      } : null
    }
  })), layers);
  addPayloadLayers(envelope, TrackTracesT, "TrackTraces", (payload) => payload.traces.map((item) => ({
    type: "TrackTrace",
    data: {
      ...objectData(item.object),
      trackId: identifier(item.trackId),
      spatialValid: validImageSpace(item.space),
      frameWidth: item.space?.width,
      frameHeight: item.space?.height,
      points: validImageSpace(item.space) ? item.points.map((point) => point && Number.isFinite(point.x) && Number.isFinite(point.y) ? imagePoint(point.x, point.y, item.space) : null) : []
    }
  })), layers);
  for (const [root2, type] of [
    [SegmentationMasksT, "SegmentationMask"],
    [SemanticSegmentationMasksT, "SemanticSegmentationMask"],
    [InstanceSegmentationMasksT, "InstanceSegmentationMask"]
  ]) {
    addPayloadLayers(envelope, root2, type + "s", (payload) => payload.masks.map((item) => ({
      type,
      data: { ...objectData(item?.object), ...maskData(item, type) }
    })), layers);
  }
  addPayloadLayers(envelope, ObjectEmbeddingsT, "ObjectEmbeddings", (payload) => payload.embeddings.map((item) => ({
    type: "ObjectEmbedding",
    data: { ...objectData(item.object), values: [...item.values] }
  })), layers);
  for (const payload of envelope.for_each(PerformanceMeasurementsT)) {
    const data = {
      pixelFormat: text(payload.pixelFormat),
      producer: layerData({ producer: payload.producer }).producer,
      timings: payload.timings.filter(Boolean).map((timing) => ({
        scopeId: text(timing.scopeId),
        name: text(timing.name),
        meanMs: timing.meanMs,
        p95Ms: timing.p95Ms
      })),
      frameRate: payload.frameRate ? { fps: payload.frameRate.fps } : null
    };
    performance2.push(data);
    layers.push({
      ...layer(payload, "PerformanceMeasurements", [{ type: "PerformanceMeasurements", data }]),
      producer: data.producer
    });
  }
  return {
    frame_counter: Number.isInteger(message.frame_counter) ? message.frame_counter : void 0,
    frame_results: { layers, performance: performance2 }
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
    measurements: Array.isArray(frameResults?.performance) ? frameResults.performance : []
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
    point(x, y, basis) {
      return {
        x: display.x + Number(x || 0) * (positiveNumber(basis?.frameWidth) ? display.width / basis.frameWidth : scaleX),
        y: display.y + Number(y || 0) * (positiveNumber(basis?.frameHeight) ? display.height / basis.frameHeight : scaleY)
      };
    },
    lengthX(value) {
      return Number(value || 0) * scaleX;
    },
    lengthY(value) {
      return Number(value || 0) * scaleY;
    },
    rect(rect) {
      if (rect.spatialValid === false) return null;
      const origin = this.point(rect.x, rect.y, rect);
      return {
        x: origin.x,
        y: origin.y,
        width: positiveNumber(rect.frameWidth) ? rect.width * display.width / rect.frameWidth : this.lengthX(rect.width ?? rect.w),
        height: positiveNumber(rect.frameHeight) ? rect.height * display.height / rect.frameHeight : this.lengthY(rect.height ?? rect.h)
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
  if (!box || box.width <= 0 || box.height <= 0) {
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
  if (!box) return;
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
    if (!validCandidate(classification, candidate)) return;
    const score = candidate.confidence == null ? "" : ` (${(candidate.confidence * 100).toFixed(1)}%)`;
    const text2 = `#${index + 1}: ${candidate.text || (candidate.classId ?? "")}${score}`;
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
  const isPerson = presenceOutcome(personClassification);
  if (now % 1e3 >= 800 || isPerson === null) {
    return;
  }
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
  if (trace.spatialValid === false || points.length < 2) {
    return;
  }
  const segments = points.length - 1;
  for (let i = 0; i < segments; i += 1) {
    if (!points[i] || !points[i + 1]) continue;
    const from = mapper.point(points[i].x, points[i].y, trace);
    const to = mapper.point(points[i + 1].x, points[i + 1].y, trace);
    const alpha = 0.35 + 0.65 * ((i + 1) / segments);
    drawArrow(ctx, from, to, hexToRgba(color, alpha), 4, 0);
  }
}
function drawGazeVector(ctx, gaze, parent, mapper, color) {
  if (!validGaze(gaze)) return;
  const yawDegrees = wrapDegrees(gaze.angles.yawDegrees);
  const pitchDegrees = wrapDegrees(gaze.angles.pitchDegrees);
  if (nearZero(yawDegrees) && nearZero(pitchDegrees)) return;
  const parentBox = mapper.rect(parent);
  if (!parentBox) return;
  const start = { x: parentBox.x + parentBox.width / 2, y: parentBox.y + parentBox.height / 2 };
  drawArrow(ctx, start, gazeEndpoint(start, yawDegrees, pitchDegrees, 120), color, 3, 10);
}
function drawCameraContactMarker(ctx, classification, face, mapper, color) {
  const hasContact = contactOutcome(classification);
  if (hasContact === null || !face) {
    return;
  }
  const box = mapper.rect(face);
  if (!box) return;
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
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
  const yaw = wrapDegrees(yawDeg) * Math.PI / 180;
  const pitch = wrapDegrees(pitchDeg) * Math.PI / 180;
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
  if (source.payloadType === "PerformanceMeasurements") return "Pipeline performance";
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
  cameraContact: "#66ff00",
  personClassification: "#66ff00",
  trackTrace: "#ffff00"
};
function defineFeature({ controls = STYLE_CONTROLS, defaults = {}, resolve: resolve5 = (result) => result, reason, ...definition }) {
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
      return resolve5({ state: "ready", message: "", output, items: output.items }, index, settings);
    }
  };
}
function resolveBoxSubjects(result, index, type, available2) {
  const items = result.items.filter((item) => item.type === type);
  const resolved = { ...result, items, attached: [] };
  let missing = false, ambiguous = false, unavailable = false;
  for (const item of items) {
    if (!available2(item.data)) {
      unavailable = true;
      continue;
    }
    const candidates = (index.objects.get(objectId(item.data.subjectUuid)) || []).filter((subject) => ["BoxDetections", "ObjectTracks"].includes(subject.source.payloadType) && subject.type === "Rect");
    if (candidates.length > 1) ambiguous = true;
    else if (candidates.length === 1 && candidates[0].data.spatialValid !== false)
      resolved.attached.push({ data: item.data, parent: candidates[0].data });
    else missing = true;
  }
  if (ambiguous) return { ...resolved, state: "ambiguous", message: "Multiple subjects match; ambiguous markers are omitted." };
  if (missing) return { ...resolved, state: "partial", message: "Subject geometry is missing or invalid." };
  if (unavailable) return { ...resolved, state: "partial", message: "Measurement unavailable." };
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
  accepts: (source) => ["Classifications", "PersonPresences"].includes(source.payloadType),
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
  defaults: { color: "#fafad2" },
  accepts: (source) => source.payloadType === "GazeMeasurements",
  resolve: (result, index) => resolveBoxSubjects(result, index, "GazeMeasurement", validGaze),
  render({ ctx, mapper }, resolved, settings) {
    for (const { data, parent } of resolved.attached) drawGazeVector(ctx, data, parent, mapper, settings.color);
  }
});

// development/web/src/features/camera-contact.js
var camera_contact_default = defineFeature({
  id: "camera-contact",
  label: "Camera contact",
  defaults: { color: "#66ff00" },
  accepts: (source) => source.payloadType === "CameraContacts",
  resolve: (result, index) => resolveBoxSubjects(result, index, "CameraContact", (data) => contactOutcome(data) !== null),
  render({ ctx, mapper }, resolved, settings) {
    for (const { data, parent } of resolved.attached) drawCameraContactMarker(ctx, data, parent, mapper, settings.color);
  }
});

// development/web/src/features/performance.js
var performance_default = defineFeature({
  id: "performance",
  label: "Performance text",
  accepts: (source) => source.payloadType === "PerformanceMeasurements",
  render({ ctx, layout }, resolved, settings) {
    for (const item of resolved.items) {
      if (item.type !== "PerformanceMeasurements") continue;
      for (const line of formatPerformanceLines(item.data)) {
        drawTextChip(ctx, line, 10, layout.performanceY, 16, settings.color);
        layout.performanceY += 20;
      }
    }
  }
});

// development/web/src/features/background-replacement.js
var resources = /* @__PURE__ */ new WeakMap();
function resolve(result) {
  const masks = result.items.filter((item) => item.type === "SegmentationMask");
  if (masks.length > 1) return { ...result, items: [], state: "ambiguous", message: "Expected one opacity mask." };
  if (!masks.length) return { ...result, items: [] };
  const data = masks[0].data;
  const { width, height } = data;
  if (!validMask(data, "SegmentationMask") || data.meaning !== OpacityMeaning.BackgroundOpacity || width > 8192 || height > 8192 || width * height > 16777216) {
    return { ...result, items: [], state: "unsupported", message: "Requires a valid, bounded background-opacity mask with image space." };
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
  if (cache.pixels !== mask.pixels || cache.mask.width !== mask.width || cache.mask.height !== mask.height) {
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
  const space = mask.space;
  composite.drawImage(
    cache.mask,
    space.originX * w / space.width,
    space.originY * h / space.height,
    space.extentWidth * w / space.width,
    space.extentHeight * h / space.height
  );
  ctx.drawImage(cache.composite, x, y, width, height);
}
var background_replacement_default = defineFeature({
  id: "background-replacement",
  label: "Background replacement",
  renderOrder: -1,
  accepts: (source) => source.payloadType === "SegmentationMasks",
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

// development/web/src/features/segmentation.js
var MAX_MASK_PIXELS = 16777216;
var MAX_CLASSES = 4096;
var classKey = (id) => id ?? -1;
var CLASS_CONTROLS = [{ key: "enabled", label: "Visible", type: "checkbox", switch: true, value: true }, ...STYLE_CONTROLS];
var MASK_CONTROLS = [{ key: "opacity", label: "Opacity", type: "range", min: 0, max: 1, step: 0.01, value: 0.5 }];
function classStyle(id, settings) {
  id = classKey(id);
  const key = (id ^ id >>> 8 ^ id >>> 16 ^ id >>> 24) & 255;
  const rgb = [64 + (key & 7) * 24, 64 + (key >> 3 & 7) * 24, 64 + (key >> 6) * 48];
  const style = settings?.classes?.[id];
  return {
    enabled: true,
    opacity: 1,
    color: "#" + rgb.map((value) => value.toString(16).padStart(2, "0")).join(""),
    ...style,
    label: style?.label?.trim() || (id === -1 ? "Unclassified" : `Class ${id}`)
  };
}
function classLegend(present, settings, message = "") {
  const ids = /* @__PURE__ */ new Set([...present, ...Object.keys(settings?.classes || {}).map(Number)]);
  return { entries: [...ids].sort((a, b) => a - b).map((id) => ({ id, ...classStyle(id, settings), present: present.has(id) })), message };
}
function classRenderSignature(settings) {
  return JSON.stringify(Object.entries(settings.classes).map(([id, style]) => [id, style.enabled, style.color, style.opacity]));
}
function withClassSettings(feature2, minId = 0, maxId = 4294967295) {
  return { ...feature2, validateSettings(settings) {
    const result = feature2.validateSettings(settings);
    if (!result) return null;
    const classes = settings.classes === void 0 ? {} : settings.classes;
    if (!classes || typeof classes !== "object" || Array.isArray(classes) || Object.keys(classes).length > MAX_CLASSES) return null;
    result.classes = {};
    for (const [id, style] of Object.entries(classes)) {
      const value = Number(id);
      if (!Number.isInteger(value) || String(value) !== id || value < minId || value > maxId || !style || typeof style.enabled !== "boolean" || typeof style.color !== "string" || !/^#[0-9a-f]{6}$/i.test(style.color) || style.label !== void 0 && (typeof style.label !== "string" || style.label.length > 80) || !Number.isFinite(style.opacity) || style.opacity < 0 || style.opacity > 1) return null;
      result.classes[id] = { enabled: style.enabled, color: style.color, opacity: style.opacity };
      if (style.label?.trim()) result.classes[id].label = style.label.trim();
    }
    return result;
  } };
}
function maskGeometryError(mask, type) {
  const { width, height } = mask;
  if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0 || width > 8192 || height > 8192 || width * height > MAX_MASK_PIXELS || !validMask(mask, type)) {
    return { state: "unsupported", message: "Invalid or oversized mask, encoding, identity or image space." };
  }
  return null;
}
function maskValueReader(mask) {
  if (mask.encoding === BitmapEncoding.Uint8) return (index) => mask.pixels[index];
  const view = new DataView(mask.pixels.buffer, mask.pixels.byteOffset, mask.pixels.byteLength);
  return (index) => view.getUint32(index * 4, true);
}
function maskPlacement(space, display) {
  return [
    display.x + space.originX * display.width / space.width,
    display.y + space.originY * display.height / space.height,
    space.extentWidth * display.width / space.width,
    space.extentHeight * display.height / space.height
  ];
}

// development/web/src/features/semantic-segmentation.js
var presentClasses = /* @__PURE__ */ new WeakMap();
var surfaces = /* @__PURE__ */ new WeakMap();
function classesIn(mask) {
  if (!mask) return /* @__PURE__ */ new Set();
  if (!presentClasses.has(mask)) {
    const seen = /* @__PURE__ */ new Set();
    const sample = maskValueReader(mask);
    for (let i = 0; i < mask.width * mask.height; i++) {
      const id = sample(i);
      if (id !== mask.voidValue) seen.add(id);
      if (seen.size > MAX_CLASSES) break;
    }
    presentClasses.set(mask, seen);
  }
  return presentClasses.get(mask);
}
function colorize(mask, settings) {
  const colors = new Map([...classesIn(mask)].map((id) => {
    const style = classStyle(id, settings);
    const rgb = style.color.slice(1).match(/../g).map((hex) => parseInt(hex, 16));
    return [id, [...rgb, style.enabled ? Math.round(style.opacity * 255) : 0]];
  }));
  const rgba = new Uint8ClampedArray(mask.width * mask.height * 4);
  const sample = maskValueReader(mask);
  for (let i = 0; i < mask.width * mask.height; i++) {
    const id = sample(i);
    if (id === mask.voidValue) continue;
    const color = colors.get(id);
    const offset = i * 4;
    rgba[offset] = color[0];
    rgba[offset + 1] = color[1];
    rgba[offset + 2] = color[2];
    rgba[offset + 3] = color[3];
  }
  return rgba;
}
function resolve2(result) {
  const items = result.items.filter((item) => item.type === "SemanticSegmentationMask");
  const invalid = (state, message) => ({ ...result, items: [], state, message });
  if (items.length > 1) return invalid("ambiguous", "Expected one class map for this source.");
  if (!items.length) return { ...result, items: [] };
  const mask = items[0].data;
  const error = maskGeometryError(mask, "SemanticSegmentationMask");
  if (error) return invalid(error.state, error.message);
  if (classesIn(mask).size > MAX_CLASSES) return invalid("unsupported", `Too many classes (maximum ${MAX_CLASSES}).`);
  return { ...result, items, mask };
}
function render2({ ctx, mapper, overlay: overlay2 }, resolved, settings = resolved.settings) {
  if (!resolved.mask) return;
  const mask = resolved.mask;
  const signature = classRenderSignature(settings);
  let surface = surfaces.get(overlay2);
  if (!surface) {
    surface = { canvas: document.createElement("canvas") };
    surfaces.set(overlay2, surface);
  }
  if (surface.mask !== mask || surface.signature !== signature || surface.canvas.width !== mask.width || surface.canvas.height !== mask.height) {
    if (surface.canvas.width !== mask.width) surface.canvas.width = mask.width;
    if (surface.canvas.height !== mask.height) surface.canvas.height = mask.height;
    surface.canvas.getContext("2d").putImageData(new ImageData(colorize(mask, settings), mask.width, mask.height), 0, 0);
    surface.mask = mask;
    surface.signature = signature;
  }
  ctx.imageSmoothingEnabled = false;
  ctx.drawImage(surface.canvas, ...maskPlacement(mask.space, mapper.display));
}
var semantic_segmentation_default = withClassSettings(defineFeature({
  id: "semantic-segmentation",
  label: "Semantic segmentation",
  renderOrder: -1,
  accepts: (source) => source.payloadType === "SemanticSegmentationMasks",
  controls: MASK_CONTROLS,
  defaults: () => ({ classes: {} }),
  classControls: CLASS_CONTROLS,
  resolve: resolve2,
  render: render2,
  legend(resolved, settings = resolved.settings) {
    const present = /* @__PURE__ */ new Set();
    classesIn(resolved.mask).forEach((id) => present.add(id));
    const message = !resolved.mask ? resolved.message || "No class map in the displayed frame." : "";
    return classLegend(present, settings, message);
  }
}));

// development/web/src/features/instance-segmentation.js
var resolvedFrames = /* @__PURE__ */ new WeakMap();
var surfaces2 = /* @__PURE__ */ new WeakMap();
var MAX_TOTAL_MASK_PIXELS = 64 * 1024 * 1024;
function resolve3(result) {
  if (resolvedFrames.has(result.output)) return resolvedFrames.get(result.output);
  const finish = (value) => {
    resolvedFrames.set(result.output, value);
    return value;
  };
  const invalid = (state, message) => finish({ ...result, items: [], state, message });
  const items = result.items.filter((item) => item.type === "InstanceSegmentationMask");
  if (items.length > 512) return invalid("unsupported", "Too many instance masks (maximum 512).");
  const totalPixels = items.reduce((total, { data }) => total + (data.pixels instanceof Uint8Array ? data.pixels.length : 0), 0);
  if (totalPixels > MAX_TOTAL_MASK_PIXELS) return invalid("unsupported", `Instance masks exceed the total pixel budget (${MAX_TOTAL_MASK_PIXELS}).`);
  const validated = [];
  let frame, labelSet;
  for (const [position, item] of items.entries()) {
    const mask = item.data;
    const error = maskGeometryError(mask, "InstanceSegmentationMask");
    if (error) return invalid(error.state, error.message);
    const { space, width, height } = mask;
    if (frame && (String(frame.frameId) !== String(space.frameId) || frame.width !== space.width || frame.height !== space.height))
      return invalid("ambiguous", "Instance masks must share one declared source frame.");
    frame = space;
    if (mask.classId != null) {
      if (labelSet && labelSet !== mask.labelSet) return invalid("ambiguous", "Instance class namespaces differ.");
      labelSet = mask.labelSet;
    }
    let left = width, right = -1, top = height, bottom = -1;
    for (let y = 0, pixelIndex = 0; y < height; y++) {
      for (let x = 0; x < width; x++, pixelIndex++) {
        const value = mask.pixels[pixelIndex];
        if (value > 1) return invalid("unsupported", "Instance masks require binary Uint8 pixels (0 or 1).");
        if (!value) continue;
        if (x < left) left = x;
        if (x > right) right = x;
        if (y < top) top = y;
        bottom = y;
      }
    }
    if (right >= 0) validated.push({ ...item, number: position + 1, bounds: { left, right, top, bottom } });
  }
  return finish({ ...result, items: validated, frame });
}
function colorize2({ data, bounds }, style) {
  const { width, height } = data;
  const rgba = new Uint8ClampedArray(width * height * 4);
  const coverage = new Uint8ClampedArray(rgba.length);
  const rgb = style.color.slice(1).match(/../g).map((hex) => parseInt(hex, 16));
  const alpha = Math.round(style.opacity * 255);
  const edge = rgb[0] * 0.299 + rgb[1] * 0.587 + rgb[2] * 0.114 >= 128 ? 0 : 255;
  const fill = [...rgb, alpha];
  const outline = [edge, edge, edge, alpha];
  const pixels = data.pixels;
  for (let y = bounds.top; y <= bounds.bottom; y++) {
    const row = y * width;
    for (let x = bounds.left; x <= bounds.right; x++) {
      const i = row + x;
      if (!pixels[i]) continue;
      const boundary = x === 0 || x === width - 1 || !pixels[i - 1] || !pixels[i + 1] || !pixels[i - width] || !pixels[i + width];
      const color = boundary ? outline : fill;
      const offset = i * 4;
      rgba[offset] = color[0];
      rgba[offset + 1] = color[1];
      rgba[offset + 2] = color[2];
      rgba[offset + 3] = color[3];
      coverage[offset + 3] = 255;
    }
  }
  return { rgba, coverage };
}
function render3({ ctx, mapper, overlay: overlay2 }, resolved, settings = resolved.settings) {
  if (!resolved.items?.length) return;
  const display = mapper.display;
  const dpr = ctx.getTransform().a;
  const pixelWidth = display.width * dpr, pixelHeight = display.height * dpr;
  if (![pixelWidth, pixelHeight].every((value) => Number.isFinite(value) && value > 0)) return;
  const requestedWidth = Math.max(1, Math.round(pixelWidth));
  const requestedHeight = Math.max(1, Math.round(pixelHeight));
  const scale = Math.min(
    1,
    8192 / requestedWidth,
    8192 / requestedHeight,
    Math.sqrt(MAX_MASK_PIXELS / requestedWidth / requestedHeight)
  );
  const width = Math.max(1, Math.floor(requestedWidth * scale));
  const height = Math.max(1, Math.min(Math.floor(requestedHeight * scale), Math.floor(MAX_MASK_PIXELS / width)));
  const signature = classRenderSignature(settings);
  let surface = surfaces2.get(overlay2);
  if (!surface) {
    surface = { canvas: document.createElement("canvas"), raster: document.createElement("canvas") };
    surfaces2.set(overlay2, surface);
  }
  if (surface.output !== resolved.output || surface.signature !== signature || surface.canvas.width !== width || surface.canvas.height !== height) {
    if (surface.canvas.width !== width) surface.canvas.width = width;
    if (surface.canvas.height !== height) surface.canvas.height = height;
    const composite = surface.canvas.getContext("2d");
    composite.clearRect(0, 0, width, height);
    composite.imageSmoothingEnabled = false;
    for (const item of resolved.items) {
      const style = classStyle(item.data.classId, settings);
      if (!style.enabled || style.opacity <= 0) continue;
      const pixels = colorize2(item, style);
      surface.raster.width = item.data.width;
      surface.raster.height = item.data.height;
      const raster = surface.raster.getContext("2d");
      const placement = maskPlacement(item.data.space, { x: 0, y: 0, width, height });
      raster.putImageData(new ImageData(pixels.coverage, item.data.width, item.data.height), 0, 0);
      composite.globalCompositeOperation = "destination-out";
      composite.drawImage(surface.raster, ...placement);
      raster.putImageData(new ImageData(pixels.rgba, item.data.width, item.data.height), 0, 0);
      composite.globalCompositeOperation = "source-over";
      composite.drawImage(surface.raster, ...placement);
    }
    surface.output = resolved.output;
    surface.signature = signature;
  }
  ctx.imageSmoothingEnabled = false;
  ctx.drawImage(surface.canvas, display.x, display.y, display.width, display.height);
}
function renderAnnotations({ ctx, mapper }, resolved, settings = resolved.settings) {
  if (settings.boxes) {
    for (const { data, number: number2 } of resolved.items || []) {
      const style = classStyle(data.classId, settings);
      if (!style.enabled || style.opacity <= 0) continue;
      const box = data.box && imageBox(data.box);
      if (!box) continue;
      const x = Math.max(0, box.x), y = Math.max(0, box.y);
      const width = Math.min(box.frameWidth, box.x + box.width) - x;
      const height = Math.min(box.frameHeight, box.y + box.height) - y;
      if (width <= 0 || height <= 0) continue;
      ctx.save();
      ctx.globalAlpha *= style.opacity;
      drawObjectBox(ctx, {
        ...box,
        x,
        y,
        width,
        height,
        label: `#${number2} \xB7 ${style.label}${data.confidence == null ? "" : ` \xB7 ${Math.round(data.confidence * 100)}%`}`
      }, mapper, "#ffffff");
      ctx.restore();
    }
  }
}
var instance_segmentation_default = withClassSettings(defineFeature({
  id: "instance-segmentation",
  label: "Instance segmentation",
  renderOrder: -1,
  accepts: (source) => source.payloadType === "InstanceSegmentationMasks",
  controls: [...MASK_CONTROLS, { key: "boxes", label: "Boxes and labels", type: "checkbox", value: true }],
  defaults: () => ({ classes: {} }),
  classControls: CLASS_CONTROLS,
  resolve: resolve3,
  render: render3,
  renderAnnotations,
  legend(resolved, settings = resolved.settings) {
    const present = new Set((resolved.items || []).map((item) => classKey(item.data.classId)));
    return classLegend(present, settings, resolved.message || (!present.size ? "No instances in the displayed frame." : ""));
  }
}), -1, 4294967295);

// development/web/src/features/ocr.js
var resolvedFrames2 = /* @__PURE__ */ new WeakMap();
var labelLayouts = /* @__PURE__ */ new WeakMap();
var validScore = (value) => Number.isFinite(value) && value >= 0 && value <= 1;
function displayText(text2) {
  let result = "";
  let count = 0;
  for (const character of text2.trim()) {
    if (count++ === 512) {
      result += "\u2026";
      break;
    }
    result += character;
  }
  return result.replace(/\s+/gu, " ").trim();
}
function resolve4(result, index) {
  if (resolvedFrames2.has(result.output)) return resolvedFrames2.get(result.output);
  const regions = [];
  let missing = false;
  let ambiguous = false;
  let invalid = false;
  const geometry = (data) => {
    const frame = { originalWidth: data.frameWidth, originalHeight: data.frameHeight };
    const { x, y, width, height, confidence } = data;
    if (data.spatialValid !== true || !Number.isSafeInteger(frame.originalWidth) || !Number.isSafeInteger(frame.originalHeight) || frame.originalWidth <= 0 || frame.originalHeight <= 0 || ![x, y, width, height].every(Number.isFinite) || width <= 0 || height <= 0 || x >= frame.originalWidth || y >= frame.originalHeight || x + width <= 0 || y + height <= 0 || confidence != null && !validScore(confidence)) {
      invalid = true;
      return null;
    }
    const left = Math.max(0, x), top = Math.max(0, y);
    return {
      box: { x: left, y: top, width: Math.min(x + width, frame.originalWidth) - left, height: Math.min(y + height, frame.originalHeight) - top },
      frame,
      text: "",
      detectionScore: confidence,
      recognitionScore: void 0
    };
  };
  const isRecognition = result.output.source.payloadType === "Classifications";
  const items = result.items.filter((item) => item.type === (isRecognition ? "Classification" : "Rect"));
  if (items.length > 4096) {
    const failed = { ...result, items: [], regions: [], state: "unsupported", message: "Too many OCR results (maximum 4096)." };
    resolvedFrames2.set(result.output, failed);
    return failed;
  }
  if (isRecognition) {
    const groups = /* @__PURE__ */ new Map();
    for (const item of items) {
      if (item.data.kind !== ClassificationKind.RecognizedText || item.data.labelSet) {
        invalid = true;
        continue;
      }
      const id = objectId(item.data.subjectUuid);
      if (!id) {
        missing = true;
        continue;
      }
      if (!groups.has(id)) groups.set(id, []);
      groups.get(id).push(item);
    }
    for (const [id, records] of groups) {
      const parents = (index.objects.get(id) || []).filter((item) => item.source.payloadType === "BoxDetections" && item.type === "Rect");
      if (parents.length !== 1) {
        if (parents.length > 1) ambiguous = true;
        else missing = true;
        continue;
      }
      const region = geometry(parents[0].data);
      if (!region) continue;
      const candidates = records[0].data.candidates;
      if (records.length > 1 || Array.isArray(candidates) && candidates.length > 1) ambiguous = true;
      else if (!Array.isArray(candidates)) invalid = true;
      else if (candidates.length) {
        const candidate = candidates[0];
        if (!validCandidate(records[0].data, candidate) || typeof candidate.text !== "string") invalid = true;
        else {
          region.text = displayText(candidate.text);
          region.recognitionScore = candidate.confidence;
        }
      }
      regions.push(region);
    }
  } else {
    for (const { data } of items) {
      const region = geometry(data);
      if (!region) continue;
      if (data.text !== void 0 && typeof data.text !== "string") invalid = true;
      else region.text = displayText(data.text || "");
      regions.push(region);
    }
  }
  const resolved = { ...result, items: regions, regions };
  if (ambiguous) Object.assign(resolved, { state: "ambiguous", message: "Ambiguous OCR relationships or candidates; unambiguous regions only, conflicting text omitted." });
  else if (invalid) Object.assign(resolved, { state: regions.length ? "partial" : "unsupported", message: "Invalid OCR geometry, text, or confidence; affected content omitted." });
  else if (missing) Object.assign(resolved, { state: "partial", message: "Waiting for subject boxes with declared image geometry." });
  resolvedFrames2.set(result.output, resolved);
  return resolved;
}
function fitLabel(ctx, text2, width) {
  if (ctx.measureText(text2).width <= width) return text2;
  if (ctx.measureText("\u2026").width > width) return "";
  const characters = Array.from(text2);
  let low = 0;
  let high = characters.length;
  while (low < high) {
    const middle = Math.ceil((low + high) / 2);
    if (ctx.measureText(characters.slice(0, middle).join("") + "\u2026").width <= width) low = middle;
    else high = middle - 1;
  }
  return characters.slice(0, low).join("") + "\u2026";
}
function fitLabels(ctx, regions, settings, width) {
  return regions.map(({ text: text2, recognitionScore, detectionScore }) => {
    const scores = [];
    if (settings.regionConfidence && validScore(detectionScore)) scores.push(`Region: ${(detectionScore * 100).toFixed(1)}%`);
    if (settings.textConfidence && validScore(recognitionScore)) scores.push(`Text: ${(recognitionScore * 100).toFixed(1)}%`);
    let label = settings.text ? text2 : "";
    const scoreLabel = scores.join(" \xB7 ");
    if (scoreLabel) {
      if (label) label = fitLabel(ctx, label, width - 8 - ctx.measureText(` \xB7 ${scoreLabel}`).width);
      label = [label, scoreLabel].filter(Boolean).join(" \xB7 ");
    }
    if (label) label = fitLabel(ctx, label, width - 8);
    return { label, width: label ? ctx.measureText(label).width + 8 : 0 };
  });
}
function render4({ ctx, mapper, overlay: overlay2 }, resolved, settings) {
  const display = mapper.display;
  const fontSize = Number(settings.textSize);
  const labelHeight = fontSize + 4;
  ctx.save();
  ctx.beginPath();
  ctx.rect(display.x, display.y, display.width, display.height);
  ctx.clip();
  ctx.strokeStyle = settings.color;
  ctx.lineWidth = 2;
  ctx.font = `${fontSize}px monospace`;
  const signature = JSON.stringify([display.width, ctx.font, settings.text, settings.regionConfidence, settings.textConfidence]);
  let layout = labelLayouts.get(overlay2);
  if (!layout || layout.regions !== resolved.regions || layout.signature !== signature) {
    layout = { regions: resolved.regions, signature, labels: fitLabels(ctx, resolved.regions || [], settings, display.width) };
    labelLayouts.set(overlay2, layout);
  }
  for (const [index, region] of (resolved.regions || []).entries()) {
    const { box, frame, text: text2 } = region;
    const x = display.x + box.x * display.width / frame.originalWidth;
    const y = display.y + box.y * display.height / frame.originalHeight;
    const width = box.width * display.width / frame.originalWidth;
    const height = box.height * display.height / frame.originalHeight;
    if (settings.regions) {
      ctx.setLineDash(text2 ? [] : [4, 3]);
      ctx.strokeRect(x, y, width, height);
    }
    const { label, width: labelWidth } = layout.labels[index];
    if (!label || display.height < labelHeight) continue;
    const labelX = Math.max(display.x, Math.min(x, display.x + display.width - labelWidth));
    const preferredY = y - labelHeight - 3 >= display.y ? y - labelHeight - 3 : y + height + 3;
    const labelY = Math.max(display.y, Math.min(preferredY, display.y + display.height - labelHeight));
    drawTextChip(ctx, label, labelX, labelY, fontSize);
  }
  ctx.restore();
}
var feature = defineFeature({
  id: "ocr",
  label: "OCR",
  requiresExplicitSource: true,
  accepts: (source) => ["BoxDetections", "Classifications"].includes(source.payloadType),
  defaults: { color: "#00ffff" },
  controls: [
    { key: "regions", label: "Regions", type: "checkbox", value: true },
    { key: "text", label: "Recognized text", type: "checkbox", value: true },
    { key: "regionConfidence", label: "Region confidence", type: "checkbox", value: false },
    { key: "textConfidence", label: "Text confidence", type: "checkbox", value: false },
    ...STYLE_CONTROLS,
    { key: "textSize", label: "Text size", type: "select", value: "16", options: [12, 16, 20, 24].map((size) => ({ value: String(size), label: `${size} px` })) }
  ],
  resolve: resolve4,
  render: render4
});
var ocr_default = {
  ...feature,
  validateSettings(settings) {
    if (settings?.confidence === void 0) return feature.validateSettings(settings);
    if (typeof settings.confidence !== "boolean") return null;
    return feature.validateSettings({ regionConfidence: settings.confidence, textConfidence: settings.confidence, ...settings });
  }
};

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
  background_replacement_default,
  semantic_segmentation_default,
  instance_segmentation_default,
  ocr_default
];
function featureFor(id) {
  return OVERLAY_FEATURES.find((feature2) => feature2.id === id);
}
function defaultSettings(id, source) {
  const defaults = featureFor(id)?.defaults || { color: "#ffffff", opacity: 1 };
  return typeof defaults === "function" ? defaults(source || {}) : { ...defaults };
}
function resolveOverlay(index, overlay2) {
  const feature2 = featureFor(overlay2.featureId);
  if (!feature2) return { state: "unsupported", message: "Unknown rendering feature." };
  if (overlay2.selection.primary === null && feature2.render) return { state: "waiting", message: "Waiting for source" };
  const settings = { ...defaultSettings(feature2.id, overlay2.selection.primary), ...overlay2.settings };
  return { ...feature2.resolve(index, overlay2.selection, settings), feature: feature2, settings };
}
function overlayStatus(index, overlay2, catalog, resolved = resolveOverlay(index, overlay2)) {
  const feature2 = featureFor(overlay2.featureId);
  if (overlay2.selection.primary === null && catalog && feature2?.discover(catalog).length > (feature2?.requiresExplicitSource ? 0 : 1)) {
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
  const passes = [];
  for (const overlay2 of overlays2) {
    if (!overlay2.enabled) continue;
    const resolved = resolveOverlay(index, overlay2);
    if (["waiting", "unsupported"].includes(resolved.state) || resolved.settings.opacity <= 0) continue;
    passes.push({ overlay: overlay2, resolved, method: "render", order: resolved.feature.renderOrder ?? 0 });
    if (resolved.feature.renderAnnotations) passes.push({ overlay: overlay2, resolved, method: "renderAnnotations", order: 0 });
  }
  for (const { overlay: overlay2, resolved, method } of passes.sort((a, b) => a.order - b.order)) {
    ctx.save();
    try {
      ctx.globalAlpha = resolved.settings.opacity ?? 1;
      resolved.feature[method]({ ...target, overlay: overlay2 }, resolved, resolved.settings);
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
  const feature2 = featureFor(overlay2.featureId);
  if (!overlay2.settings || typeof overlay2.settings !== "object" || Array.isArray(overlay2.settings)) return null;
  const settings = feature2 ? feature2.validateSettings(overlay2.settings) : overlay2.settings;
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
    const feature2 = featureFor(overlay2.featureId);
    if (!feature2?.render || feature2.requiresExplicitSource) continue;
    const sources = feature2.discover(catalog);
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
      if (overlay2.featureId === "gaze" && source?.payloadType === "PoseEstimations" && source.contentType === "eyeYawPitch") {
        source.payloadType = "GazeMeasurements";
        relabelled = true;
      }
      if (source?.payloadType === "Classifications" && ["cameraContact", "personClassification"].includes(source.contentType)) {
        source.payloadType = source.contentType === "cameraContact" ? "CameraContacts" : "PersonPresences";
        relabelled = true;
      }
      if (overlay2.featureId === "performance" && source?.payloadType === "PerformanceOverlay") {
        source.payloadType = "PerformanceMeasurements";
        relabelled = true;
      }
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
function styleControls(featureId, settings, changed, preview = changed, controls = featureFor(featureId)?.controls || []) {
  const container = node("div", "overlay-style-controls");
  for (const control of controls) {
    if (control.type === "checkbox") {
      const label2 = node("label", control.switch ? "model-toggle-switch overlay-visible" : "overlay-visibility");
      const input2 = document.createElement("input");
      input2.type = "checkbox";
      input2.checked = settings[control.key];
      if (control.ariaLabel) input2.setAttribute("aria-label", control.ariaLabel);
      input2.addEventListener("change", () => {
        settings[control.key] = input2.checked;
        changed();
      });
      if (control.switch) {
        input2.setAttribute("role", "switch");
        const track = node("span", "model-toggle-track");
        track.setAttribute("aria-hidden", "true");
        track.append(node("span", "model-toggle-thumb"));
        label2.append(input2, track);
      } else label2.append(input2, node("span", "wfr-txt-label", control.label));
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
    input.setAttribute("aria-label", control.ariaLabel || control.label);
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
      updateLegends();
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
    swatch.hidden = !Object.hasOwn(overlay2.settings, "color");
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
    const legend = featureFor(overlay2.featureId)?.legend ? node("section", "overlay-legend") : null;
    if (legend) {
      legend.setAttribute("aria-label", "Class legend");
      settings.append(legend);
    }
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
    cardNodes.set(overlay2, { card, toggle, title, checkbox, remove, status: status2, metadata, featureLabel, legend });
    list.append(card);
  }
  updateStatuses();
  updateSourceLabels();
  updateLegends();
}
function updateLegends() {
  for (const [overlay2, row] of cardNodes) {
    if (!row.legend) continue;
    const feature2 = featureFor(overlay2.featureId);
    const resolved = !displayedFrameIndex.frame ? { message: "Waiting for matching video metadata." } : resolveOverlay(displayedFrameIndex, overlay2);
    const legend = feature2.legend(resolved, overlay2.settings);
    if (!row.legendNodes) {
      row.legendNodes = /* @__PURE__ */ new Map();
      row.legendMessage = node("p", "overlay-hint wfr-txt-caption");
      row.legendList = node("ul", "overlay-legend-list");
      const header = node("div", "overlay-legend-header");
      header.append(node("p", "wfr-txt-label", "Class legend"));
      if (feature2.classControls) {
        const reset = node("button", "wfr-txt-caption", "Reset labels");
        reset.type = "button";
        reset.addEventListener("click", () => {
          for (const style of Object.values(overlay2.settings.classes)) delete style.label;
          for (const item of row.legendNodes.values()) item.name.value = "";
          persist();
          updateLegends();
        });
        header.append(reset);
      }
      row.legend.replaceChildren(header, row.legendMessage, row.legendList);
      if (feature2.classControls) row.legend.append(node(
        "p",
        "overlay-hint wfr-txt-caption",
        "Class opacity is multiplied by the overlay opacity. Hidden classes reveal the video."
      ));
    }
    const message = overlay2.enabled ? legend.message : "Overlay hidden.";
    if (row.legendMessage.textContent !== message) row.legendMessage.textContent = message;
    const present = new Set(legend.entries.filter((entry) => entry.present).map((entry) => entry.id));
    for (const entry of legend.entries) {
      let item = row.legendNodes.get(entry.id);
      if (!item) {
        const swatch = feature2.classControls ? null : node("span", "overlay-swatch");
        swatch?.setAttribute("aria-hidden", "true");
        const element = node("li", "wfr-txt-caption");
        const heading = node("div", "overlay-class-heading");
        const presence = feature2.classControls ? null : node("span", "overlay-class-presence");
        const name = node(feature2.classControls ? "input" : "span", "overlay-class-name");
        if (feature2.classControls) heading.append(name);
        else heading.append(swatch, name, presence);
        element.append(heading);
        if (feature2.classControls) {
          element.className += " overlay-class-row";
          const settings = overlay2.settings.classes?.[entry.id] || { enabled: entry.enabled, color: entry.color, opacity: entry.opacity };
          const preview = () => {
            var _a;
            ((_a = overlay2.settings).classes ?? (_a.classes = {}))[entry.id] = settings;
            updateLegends();
          };
          name.type = "text";
          name.maxLength = 80;
          name.placeholder = `Class ${entry.id}`;
          name.value = settings.label || "";
          name.setAttribute("aria-label", `Class ${entry.id} name`);
          name.addEventListener("input", () => {
            const label = name.value.trim();
            if (label) settings.label = label;
            else delete settings.label;
            preview();
          });
          name.addEventListener("change", () => {
            name.value = settings.label || "";
            persist();
          });
          name.addEventListener("keydown", (event) => {
            if (event.key === "Enter") {
              event.preventDefault();
              name.blur();
            }
          });
          const controls = feature2.classControls.map((control) => ({
            ...control,
            ariaLabel: `Class ${entry.id} (${entry.id}) ${control.label.toLowerCase()}`
          }));
          const inputs = styleControls(overlay2.featureId, settings, () => {
            preview();
            persist();
          }, preview, controls);
          inputs.className += " overlay-class-controls";
          element.append(inputs);
        } else name.textContent = entry.label;
        const nextId = [...row.legendNodes.keys()].sort((a, b) => a - b).find((id) => id > entry.id);
        row.legendList.insertBefore(element, row.legendNodes.get(nextId)?.element || null);
        item = { element, swatch, presence, heading, name, label: `${entry.id} \xB7 ${entry.label}` };
        row.legendNodes.set(entry.id, item);
      }
      item.label = `${entry.id} \xB7 ${entry.label}`;
      if (item.swatch && item.color !== entry.color) {
        item.swatch.style.setProperty("--overlay-color", entry.color);
        item.color = entry.color;
      }
    }
    for (const [id, item] of row.legendNodes) {
      const text2 = present.has(id) ? "In frame" : resolved.state === "ready" ? "Not in frame" : "Unavailable";
      if (item.presence && item.presence.textContent !== text2) {
        item.presence.textContent = text2;
      }
      const title = `${item.label} \xB7 ${text2}`;
      if (item.heading.title !== title) {
        item.heading.title = title;
        if (feature2.classControls) item.name.setAttribute("aria-description", text2);
      }
    }
  }
}
function updateStatuses() {
  for (const [overlay2, row] of cardNodes) {
    const resolved = resolveOverlay(frameIndex, overlay2);
    const state = overlayStatus(frameIndex, overlay2, knownOutputs, resolved);
    if (row.status.textContent !== state) row.status.textContent = state;
    const available2 = !["waiting", "unsupported"].includes(resolved.state);
    const color = /^#[0-9a-f]{6}$/i.test(overlay2.settings.color) ? overlay2.settings.color : "transparent";
    if (row.color !== color || row.available !== available2) {
      row.card.style.setProperty("--overlay-color", color);
      row.card.setAttribute("data-unavailable", String(!available2));
      row.color = color;
      row.available = available2;
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
  const features = new Map(OVERLAY_FEATURES.filter((feature2) => feature2.render || feature2.discover(catalog).length).map((feature2) => [feature2.id, feature2.label]));
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
  const feature2 = featureFor(featureSelect.value);
  if (feature2?.requiresExplicitSource && draft && draft.featureId !== featureSelect.value) previous = "";
  const sources = feature2?.discover(catalog) || [];
  const choices = new Map(sources.map((source) => [sourceKey(source), source]));
  for (const overlay2 of [editing, draft]) {
    if (overlay2?.featureId === featureSelect.value && overlay2.selection.primary) choices.set(selectionKey(overlay2), overlay2.selection.primary);
  }
  const descriptors = [...choices.values()];
  const labels = sourceLabels(descriptors, false);
  outputSelect.replaceChildren();
  const hasChoice = descriptors.length > (feature2?.requiresExplicitSource ? 0 : 1);
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
  sourceSummary.textContent = descriptors.length === 1 ? `From ${labels.get(sourceKey(descriptors[0]))}` : feature2?.requiresExplicitSource ? "Choose a source when available." : "Source will be selected when available.";
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
  updateLegends();
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

import {createWebRtcClient} from "./webrtc_client.js";
import {resolveWebRtcIceConfig, resolveWebRtcTimingConfig} from "./webrtc_config.js";

import {createMetadataClient} from "./metadata-client.js";
import {renderOsd, resizeCanvasToDisplaySize} from "./osd-renderer.js";

const elements = {
  video: document.getElementById("video"),
  overlay: document.getElementById("overlay"),
  stageMessage: document.getElementById("stage-message"),
  statusLine: document.getElementById("status-line"),
  connectVideoButton: document.getElementById("connect-video-button"),
  disconnectVideoButton: document.getElementById("disconnect-video-button"),
  connectMetadataButton: document.getElementById("connect-metadata-button"),
  disconnectMetadataButton: document.getElementById("disconnect-metadata-button"),
  signalingUrl: document.getElementById("signaling-url"),
  metadataUrl: document.getElementById("metadata-url"),
  videoState: document.getElementById("video-state"),
  metadataState: document.getElementById("metadata-state"),
  frameCounter: document.getElementById("frame-counter"),
  layerCount: document.getElementById("layer-count"),
  detectionCount: document.getElementById("detection-count"),
  videoSize: document.getElementById("video-size"),
  renderObjects: document.getElementById("render-objects"),
  renderFaces: document.getElementById("render-faces"),
  renderGaze: document.getElementById("render-gaze"),
  renderCameraContact: document.getElementById("render-camera-contact"),
  renderTrackTraces: document.getElementById("render-track-traces"),
  renderClassification: document.getElementById("render-classification"),
  renderPersonStatus: document.getElementById("render-person-status"),
  renderPerformance: document.getElementById("render-performance"),
  colorObjects: document.getElementById("color-objects"),
  colorFaces: document.getElementById("color-faces"),
  colorGaze: document.getElementById("color-gaze"),
  colorCameraContact: document.getElementById("color-camera-contact"),
  colorTrackTraces: document.getElementById("color-track-traces"),
  colorClassification: document.getElementById("color-classification"),
  colorPersonStatus: document.getElementById("color-person-status"),
  colorPerformance: document.getElementById("color-performance"),
};

const state = {
  videoClient: null,
  metadataClient: null,
  latest: null,
  videoConnected: false,
  metadataConnected: false,
  renderHandle: null,
};

elements.signalingUrl.value = "ws://127.0.0.1:8000/ws";
elements.metadataUrl.value = "ws://127.0.0.1:8002/ws";

elements.connectVideoButton.addEventListener("click", connectVideo);
elements.disconnectVideoButton.addEventListener("click", disconnectVideo);
elements.connectMetadataButton.addEventListener("click", connectMetadata);
elements.disconnectMetadataButton.addEventListener("click", disconnectMetadata);
window.addEventListener("resize", () => resizeCanvasToDisplaySize(elements.overlay));
elements.video.addEventListener("loadedmetadata", updateVideoSize);
elements.video.addEventListener("resize", updateVideoSize);

startRenderLoop();
updateMetrics();

function connectVideo() {
  disconnectVideo();

  const timingConfig = resolveWebRtcTimingConfig({});
  const iceConfig = resolveWebRtcIceConfig({});
  state.videoClient = createWebRtcClient({
    video: elements.video,
    signalingUrl: elements.signalingUrl.value.trim(),
    ...timingConfig,
    ...iceConfig,
    logger: logMessage,
    onStatus: setVideoStatus,
    onStatusLine: setStatusLine,
    onRemoteTrack: () => updateVideoSize(),
    WebSocketFactory: (url) => new WebSocket(url),
    RTCPeerConnectionFactory: (options) => new RTCPeerConnection(options),
    MediaStreamFactory: () => new MediaStream(),
    RTCSessionDescriptionFactory: (description) => new RTCSessionDescription(description),
    RTCIceCandidateFactory: (candidate) => new RTCIceCandidate(candidate),
    webSocketOpenState: WebSocket.OPEN,
    webSocketConnectingState: WebSocket.CONNECTING,
  });

  state.videoClient.start();
  elements.connectVideoButton.disabled = true;
  elements.disconnectVideoButton.disabled = false;
  setStatusLine("Connecting video");
}

function disconnectVideo() {
  if (state.videoClient) {
    state.videoClient.stop();
    state.videoClient = null;
  }
  state.videoConnected = false;
  elements.videoState.textContent = "Closed";
  elements.stageMessage.classList.remove("hidden");
  elements.connectVideoButton.disabled = false;
  elements.disconnectVideoButton.disabled = true;
  setStatusLine(state.metadataConnected ? "Metadata connected" : "Video disconnected");
  updateMetrics();
}

function connectMetadata() {
  disconnectMetadata();

  state.metadataClient = createMetadataClient({
    url: elements.metadataUrl.value.trim(),
    onStatus: setMetadataStatus,
    onMessage: (message) => {
      state.latest = message;
      updateMetrics();
    },
    onError: logMessage,
  });

  state.metadataClient.connect();
  elements.connectMetadataButton.disabled = true;
  elements.disconnectMetadataButton.disabled = false;
  setStatusLine("Connecting metadata");
}

function disconnectMetadata() {
  if (state.metadataClient) {
    state.metadataClient.disconnect();
    state.metadataClient = null;
  }
  state.latest = null;
  state.metadataConnected = false;
  elements.metadataState.textContent = "Closed";
  elements.connectMetadataButton.disabled = false;
  elements.disconnectMetadataButton.disabled = true;
  setStatusLine(state.videoConnected ? "Video connected" : "Metadata disconnected");
  updateMetrics();
}

function startRenderLoop() {
  const render = () => {
    renderOsd(elements.overlay, elements.video, state.latest?.perception || null, getRenderOptions());
    state.renderHandle = requestAnimationFrame(render);
  };
  state.renderHandle = requestAnimationFrame(render);
}

function getRenderOptions() {
  return {
    objects: elements.renderObjects.checked,
    faces: elements.renderFaces.checked,
    gaze: elements.renderGaze.checked,
    cameraContact: elements.renderCameraContact.checked,
    trackTraces: elements.renderTrackTraces.checked,
    classification: elements.renderClassification.checked,
    personStatus: elements.renderPersonStatus.checked,
    performance: elements.renderPerformance.checked,
    colors: {
      objects: elements.colorObjects.value,
      faces: elements.colorFaces.value,
      gaze: elements.colorGaze.value,
      cameraContact: elements.colorCameraContact.value,
      trackTraces: elements.colorTrackTraces.value,
      classification: elements.colorClassification.value,
      personStatus: elements.colorPersonStatus.value,
      performance: elements.colorPerformance.value,
    },
  };
}

function setVideoStatus(status, label, subtext) {
  state.videoConnected = status === "connected";
  elements.videoState.textContent = label || status;
  elements.stageMessage.classList.toggle("hidden", state.videoConnected);
  if (subtext) {
    setStatusLine(subtext);
  }
  updateMetrics();
}

function setMetadataStatus(status, label) {
  state.metadataConnected = status === "connected";
  elements.metadataState.textContent = label || status;
  updateMetrics();
}

function setStatusLine(text) {
  elements.statusLine.textContent = text;
}

function updateMetrics() {
  const perception = state.latest?.perception;
  const layers = Array.isArray(perception?.layers) ? perception.layers : [];
  const detections = layers.reduce((sum, layer) => sum + (Array.isArray(layer.detections) ? layer.detections.length : 0), 0);

  elements.frameCounter.textContent = state.latest?.frameCounter ?? "n/a";
  elements.layerCount.textContent = String(layers.length);
  elements.detectionCount.textContent = String(detections);
  updateVideoSize();
}

function updateVideoSize() {
  const {videoWidth, videoHeight} = elements.video;
  elements.videoSize.textContent = videoWidth && videoHeight ? `${videoWidth}x${videoHeight}` : "n/a";
}

function logMessage(message, type = "info") {
  console[type === "error" ? "error" : "log"](`[browser-osd] ${message}`);
}

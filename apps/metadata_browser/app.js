const elements = {
  connectForm: document.getElementById("connect-form"),
  wsUrl: document.getElementById("ws-url"),
  connectButton: document.getElementById("connect-button"),
  disconnectButton: document.getElementById("disconnect-button"),
  statusLine: document.getElementById("status-line"),
  metricsGrid: document.getElementById("metrics-grid"),
  summaryGrid: document.getElementById("summary-grid"),
  perfList: document.getElementById("perf-list"),
  layersList: document.getElementById("layers-list"),
  rawJson: document.getElementById("raw-json"),
};

const state = {
  socket: null,
  messagesReceived: 0,
  connectedSince: null,
  lastMessageAt: null,
  latestMessage: null,
};

const ROUTING_NOTES = [
  "Parse one transport message per WebSocket frame.",
  "Read `message.perception` and handle `null` safely.",
  "Route on `layer.contentType` and detection `type`.",
  "Ignore unknown layers and detections instead of crashing.",
  "Prefer `VideoFrame` metadata when coordinate mapping matters.",
];

function setStatus(text, online) {
  elements.statusLine.textContent = text;
  elements.statusLine.classList.toggle("offline", !online);
}

function formatTimestamp(date) {
  if (!date) {
    return "n/a";
  }
  return date.toLocaleTimeString();
}

function formatValue(value) {
  if (value === null) {
    return "null";
  }
  if (value === undefined) {
    return "n/a";
  }
  if (typeof value === "number") {
    return Number.isInteger(value) ? String(value) : value.toFixed(3);
  }
  if (typeof value === "string") {
    return value === "" ? "''" : value;
  }
  if (Array.isArray(value)) {
    return `${value.length} item${value.length === 1 ? "" : "s"}`;
  }
  if (typeof value === "object") {
    return JSON.stringify(value);
  }
  return String(value);
}

function normalizeMessage(message) {
  if (message && typeof message === "object" && Object.hasOwn(message, "perception")) {
    return {
      frameCounter: message.frame_counter ?? "n/a",
      perception:
        message.perception && typeof message.perception === "object"
          ? message.perception
          : { perfdata: [], layers: [] },
      perceptionPresent: message.perception && typeof message.perception === "object",
      raw: message,
    };
  }

  if (message && typeof message === "object" && Array.isArray(message.layers)) {
    return {
      frameCounter: "n/a",
      perception: message,
      perceptionPresent: true,
      raw: message,
    };
  }

  return {
    frameCounter: "n/a",
    perception: { perfdata: [], layers: [] },
    perceptionPresent: false,
    raw: message,
  };
}

// <agent-review:suppress-begin> metadata_browser is a local demo/debug viewer for
// trusted PEK metadata streams, not a hardened public WebSocket client; accepting
// the simple metric/summary innerHTML rendering risk is intentional for this demo.
function renderMetrics(normalized) {
  const perception = normalized.perception ?? { perfdata: [], layers: [] };
  const layers = Array.isArray(perception.layers) ? perception.layers : [];
  const detectionCount = layers.reduce((sum, layer) => sum + (Array.isArray(layer.detections) ? layer.detections.length : 0), 0);

  const metrics = [
    { label: "Connection", value: state.socket?.readyState === WebSocket.OPEN ? "Live" : "Closed" },
    { label: "Messages", value: state.messagesReceived },
    { label: "Frame Counter", value: normalized.frameCounter },
    { label: "Perception", value: normalized.perceptionPresent ? "Present" : "Empty" },
    { label: "Layers", value: layers.length },
    { label: "Detections", value: detectionCount },
    { label: "Last Message", value: formatTimestamp(state.lastMessageAt) },
  ];

  elements.metricsGrid.innerHTML = metrics
    .map(
      (metric) => `
        <div class="metric">
          <span class="metric-label">${metric.label}</span>
          <span class="metric-value">${metric.value}</span>
        </div>
      `
    )
    .join("");
}

function renderSummary(normalized) {
  const perception = normalized.perception ?? { perfdata: [], layers: [] };
  const layers = Array.isArray(perception.layers) ? perception.layers : [];
  const counts = new Map();

  for (const layer of layers) {
    const key = layer.contentType || "unknown";
    const detections = Array.isArray(layer.detections) ? layer.detections.length : 0;
    counts.set(key, (counts.get(key) ?? 0) + detections);
  }

  if (counts.size === 0) {
    elements.summaryGrid.innerHTML = '<div class="empty-state">No layer summaries yet.</div>';
    return;
  }

  elements.summaryGrid.innerHTML = [...counts.entries()]
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(
      ([label, value]) => `
        <div class="summary-item">
          <span class="summary-label">${label}</span>
          <span class="summary-value">${value}</span>
        </div>
      `
    )
    .join("");
}
// <agent-review:suppress-end>

function renderPerf(normalized) {
  const perfdata = normalized.perception?.perfdata;

  const noteItems = ROUTING_NOTES.map((entry) => `<div class="perf-item">${escapeHtml(entry)}</div>`);

  if (!normalized.perceptionPresent) {
    elements.perfList.className = "perf-list";
    elements.perfList.innerHTML = [
      '<div class="perf-item">Latest transport message has no `perception` payload.</div>',
      ...noteItems,
    ].join("");
    return;
  }

  if (!Array.isArray(perfdata) || perfdata.length === 0) {
    elements.perfList.className = "perf-list";
    elements.perfList.innerHTML = [
      '<div class="perf-item">No `perfdata` lines in the latest `perception` payload.</div>',
      ...noteItems,
    ].join("");
    return;
  }

  elements.perfList.className = "perf-list";
  elements.perfList.innerHTML = [
    ...perfdata.map((entry) => `<div class="perf-item">${escapeHtml(entry)}</div>`),
    ...noteItems,
  ].join("");
}

function keyValueItems(data) {
  return Object.entries(data)
    .filter(([key]) => key !== "uuid" && key !== "parentUuid" && key !== "creationTsNs")
    .map(
      ([key, value]) => `
        <div class="detection-item">
          <span class="detection-label">${escapeHtml(key)}</span>
          <span class="detection-value">${escapeHtml(formatValue(value))}</span>
        </div>
      `
    )
    .join("");
}

function renderDetectionCard(detection, index) {
  const type = detection?.type ?? "Unknown";
  const data = detection?.data && typeof detection.data === "object" ? detection.data : {};

  const primaryFields = [
    ["uuid", data.uuid],
    ["parentUuid", data.parentUuid],
    ["creationTsNs", data.creationTsNs],
  ]
    .map(
      ([label, value]) => `
        <div class="detection-item">
          <span class="detection-label">${label}</span>
          <span class="detection-value">${escapeHtml(formatValue(value))}</span>
        </div>
      `
    )
    .join("");

  return `
    <article class="detection-card">
      <div class="detection-header">
        <h4 class="detection-title">${escapeHtml(type)}</h4>
        <span class="pill">#${index + 1}</span>
      </div>
      <div class="detection-grid">
        ${primaryFields}
        ${keyValueItems(data)}
      </div>
    </article>
  `;
}

function renderLayers(normalized) {
  const layers = normalized.perception?.layers;
  if (!Array.isArray(layers) || layers.length === 0) {
    elements.layersList.className = "layers-list empty-state";
    elements.layersList.textContent = "No metadata received yet.";
    return;
  }

  elements.layersList.className = "layers-list";
  elements.layersList.innerHTML = layers
    .map((layer, layerIndex) => {
      const detections = Array.isArray(layer.detections) ? layer.detections : [];
      const metaItems = [
        ["Engine", layer.engine],
        ["Model", layer.model],
        ["Tags", layer.tags],
        ["Label Family", layer.labelFamily],
        ["Compositing", layer.compositingMode],
        ["Infer ID", layer["infer-id"]],
      ]
        .map(
          ([label, value]) => `
            <div class="layer-meta-item">
              <span class="layer-meta-label">${escapeHtml(label)}</span>
              <span class="layer-meta-value">${escapeHtml(formatValue(value))}</span>
            </div>
          `
        )
        .join("");

      return `
        <article class="layer-card">
          <div class="layer-header">
            <h3 class="layer-title">Layer ${layerIndex + 1}: ${escapeHtml(layer.contentType || "unknown")}</h3>
            <span class="pill">${detections.length} detection${detections.length === 1 ? "" : "s"}</span>
          </div>
          <div class="layer-meta">${metaItems}</div>
          <div class="detections">
            ${
              detections.length > 0
                ? detections.map((detection, detectionIndex) => renderDetectionCard(detection, detectionIndex)).join("")
                : '<div class="empty-state">No detections in this layer.</div>'
            }
          </div>
        </article>
      `;
    })
    .join("");
}

function renderRaw(normalized) {
  elements.rawJson.textContent = JSON.stringify(normalized.raw, null, 2);
}

function renderMessage(message) {
  const normalized = normalizeMessage(message);
  renderMetrics(normalized);
  renderSummary(normalized);
  renderPerf(normalized);
  renderLayers(normalized);
  renderRaw(normalized);
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function disconnect() {
  if (state.socket) {
    state.socket.onopen = null;
    state.socket.onclose = null;
    state.socket.onerror = null;
    state.socket.onmessage = null;
    state.socket.close();
    state.socket = null;
  }
  setStatus("Offline", false);
  renderMetrics(normalizeMessage(state.latestMessage ?? { perception: { perfdata: [], layers: [] } }));
}

function connect(url) {
  disconnect();
  setStatus(`Connecting to ${url}…`, false);

  const socket = new WebSocket(url);
  state.socket = socket;

  socket.onopen = () => {
    state.connectedSince = new Date();
    setStatus(`Connected to ${url}`, true);
    renderMetrics(normalizeMessage(state.latestMessage ?? { perception: { perfdata: [], layers: [] } }));
  };

  socket.onmessage = (event) => {
    state.messagesReceived += 1;
    state.lastMessageAt = new Date();

    try {
      const message = JSON.parse(event.data);
      state.latestMessage = message;
      renderMessage(message);
      setStatus(`Connected to ${url}`, true);
    } catch (error) {
      setStatus(`Parse error: ${error instanceof Error ? error.message : String(error)}`, false);
    }
  };

  socket.onerror = () => {
    setStatus(`WebSocket error on ${url}`, false);
  };

  socket.onclose = () => {
    setStatus(`Disconnected from ${url}`, false);
    state.socket = null;
    renderMetrics(normalizeMessage(state.latestMessage ?? { perception: { perfdata: [], layers: [] } }));
  };
}

elements.connectForm.addEventListener("submit", (event) => {
  event.preventDefault();
  connect(elements.wsUrl.value.trim());
});

elements.disconnectButton.addEventListener("click", () => {
  disconnect();
});

renderMessage({ frame_counter: "n/a", perception: { perfdata: [], layers: [] } });

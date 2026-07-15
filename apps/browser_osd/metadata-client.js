export function normalizeTransportMessage(message) {
  if (message && typeof message === "object" && Object.hasOwn(message, "perception")) {
    const perception = isPerception(message.perception) ? message.perception : null;
    return {
      frameCounter: Number.isInteger(message.frame_counter) ? message.frame_counter : "n/a",
      perception,
      perceptionPresent: perception !== null,
      raw: message,
    };
  }

  if (isPerception(message)) {
    return {
      frameCounter: "n/a",
      perception: message,
      perceptionPresent: true,
      raw: message,
    };
  }

  return {
    frameCounter: "n/a",
    perception: null,
    perceptionPresent: false,
    raw: message,
  };
}

export function createMetadataClient(config) {
  return new MetadataClient(config);
}

function isPerception(value) {
  return Boolean(value) && typeof value === "object" && Array.isArray(value.layers);
}

class MetadataClient {
  constructor(config) {
    this.url = requireConfig(config, "url");
    this.WebSocketFactory = config.WebSocketFactory || ((url) => new WebSocket(url));
    this.onStatus = config.onStatus || (() => {});
    this.onMessage = config.onMessage || (() => {});
    this.onError = config.onError || (() => {});
    this.socket = null;
  }

  connect() {
    this.disconnect();
    this.onStatus("connecting", "Connecting");
    this.socket = this.WebSocketFactory(this.url);

    this.socket.onopen = () => {
      this.onStatus("connected", "Connected");
    };

    this.socket.onmessage = (event) => {
      try {
        const parsed = JSON.parse(event.data);
        this.onMessage(normalizeTransportMessage(parsed));
      } catch (err) {
        this.onError(`Invalid metadata message: ${formatError(err)}`);
      }
    };

    this.socket.onerror = () => {
      this.onError("Metadata WebSocket error");
    };

    this.socket.onclose = () => {
      this.onStatus("disconnected", "Closed");
    };
  }

  disconnect() {
    if (!this.socket) {
      return;
    }

    const socket = this.socket;
    this.socket = null;
    socket.onopen = null;
    socket.onmessage = null;
    socket.onerror = null;
    socket.onclose = null;
    if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING) {
      socket.close();
    }
    this.onStatus("disconnected", "Closed");
  }
}

function requireConfig(config, key) {
  if (!config || !config[key]) {
    throw new Error(`Missing metadata client config: ${key}`);
  }
  return config[key];
}

function formatError(err) {
  return err?.message || String(err || "unknown error");
}

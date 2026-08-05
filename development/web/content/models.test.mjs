import assert from "node:assert/strict";
import test from "node:test";

class FakeClassList {
  constructor(owner) {
    this.owner = owner;
    this.values = new Set();
  }

  add(...names) {
    names.forEach((name) => this.values.add(name));
    this.sync();
  }

  remove(...names) {
    names.forEach((name) => this.values.delete(name));
    this.sync();
  }

  toggle(name, force) {
    const enabled = force === undefined ? !this.values.has(name) : Boolean(force);
    if (enabled) {
      this.values.add(name);
    } else {
      this.values.delete(name);
    }
    this.sync();
    return enabled;
  }

  sync() {
    this.owner._className = [...this.values].join(" ");
  }
}

class FakeElement {
  constructor(tagName) {
    this.tagName = tagName.toUpperCase();
    this.children = [];
    this.attributes = new Map();
    this.listeners = new Map();
    this.classList = new FakeClassList(this);
    this.style = {};
    this.textContent = "";
    this.title = "";
    this.checked = false;
    this.disabled = false;
    this._className = "";
    this._innerHTML = "";
  }

  set className(value) {
    this._className = value;
    this.classList.values = new Set(value.split(/\s+/).filter(Boolean));
  }

  get className() {
    return this._className;
  }

  set innerHTML(value) {
    this._innerHTML = value;
    this.children = [];
  }

  get innerHTML() {
    return this._innerHTML;
  }

  appendChild(child) {
    this.children.push(child);
    return child;
  }

  append(...children) {
    children.forEach((child) => this.appendChild(child));
  }

  setAttribute(name, value) {
    this.attributes.set(name, String(value));
  }

  getAttribute(name) {
    return this.attributes.get(name) ?? null;
  }

  addEventListener(type, listener) {
    this.listeners.set(type, listener);
  }

  dispatchEvent(event) {
    this.listeners.get(event.type)?.(event);
  }

  querySelector(selector) {
    return findElement(this, (element) => {
      if (selector.startsWith(".")) {
        return element.classList.values.has(selector.slice(1));
      }
      return element.tagName === selector.toUpperCase();
    });
  }
}

function findElement(root, predicate) {
  for (const child of root.children) {
    if (predicate(child)) return child;
    const nested = findElement(child, predicate);
    if (nested) return nested;
  }
  return null;
}

const modelsContainer = new FakeElement("div");
const muteButton = new FakeElement("button");
const muteText = new FakeElement("span");
muteText.className = "video-control-text";
muteButton.appendChild(muteText);
const muteIcon = new FakeElement("span");

globalThis.document = {
  body: new FakeElement("body"),
  createElement: (tagName) => new FakeElement(tagName),
  getElementById: (id) => {
    if (id === "models-container") return modelsContainer;
    if (id === "muteUnmuteBtn") return muteButton;
    if (id === "muteUnmuteIcon") return muteIcon;
    return null;
  },
};
globalThis.location = {
  protocol: "http:",
  hostname: "localhost",
  port: "9999",
};
globalThis.window = {
  PEK_CONFIG: {ctrlPort: 8001},
  dispatchEvent() {},
};
globalThis.CustomEvent = class {
  constructor(type, init) {
    this.type = type;
    this.detail = init?.detail;
  }
};

const sentMessages = [];
globalThis.WebSocket = class {
  static CONNECTING = 0;
  static OPEN = 1;

  constructor() {
    this.readyState = WebSocket.OPEN;
  }

  send(payload) {
    sentMessages.push(JSON.parse(payload));
  }
};

const {modelsManager} = await import("./models.js");

test("selector renders task and model details from descriptor metadata", () => {
  const model = {
    active: false,
    displayName: "YOLOv11n",
    element_name: "pekinfer1",
    name: "YoloV11",
    runtime: "ONNX",
    task: "Object detection",
  };

  modelsManager.render([model]);

  const item = modelsContainer.children[0];
  const task = item.querySelector(".model-task");
  const details = item.querySelector(".model-details");
  const toggleLabel = item.querySelector("label");
  const toggle = item.querySelector("input");

  assert.equal(item.getAttribute("data-model-name"), "YoloV11");
  assert.equal(task.textContent, "Object detection");
  assert.equal(details.textContent, "YOLOv11n (ONNX)");
  assert.equal(item.querySelector(".model-copy").title, "Object detection - YOLOv11n (ONNX)");
  assert.equal(toggleLabel.getAttribute("aria-label"), "Toggle Object detection - YOLOv11n (ONNX)");

  toggle.checked = true;
  toggle.dispatchEvent({type: "change"});

  assert.deepEqual(sentMessages.at(-1), {
    type: "model_toggle",
    name: "pekinfer1",
  });
});

test("partial metadata remains readable", () => {
  modelsManager.render([
    {
      active: false,
      displayName: "Custom Accelerator Model",
      element_name: "custom-runtime",
      name: "CustomInternalName",
      runtime: "CustomRT",
    },
  ]);

  const item = modelsContainer.children[0];

  assert.equal(item.querySelector(".model-task").textContent, "Custom Accelerator Model");
  assert.equal(item.querySelector(".model-details").textContent, "CustomRT");
  assert.equal(item.querySelector("label").getAttribute("aria-label"),
    "Toggle Custom Accelerator Model - CustomRT");
});

test("unknown model names render on one line without invented metadata", () => {
  modelsManager.render([
    {
      active: false,
      element_name: "custom0",
      name: "My Custom Chain",
    },
  ]);

  const item = modelsContainer.children[0];

  assert.equal(item.querySelector(".model-task").textContent, "My Custom Chain");
  assert.equal(item.querySelector(".model-details"), null);
  assert.equal(item.querySelector("label").getAttribute("aria-label"),
    "Toggle My Custom Chain");
});

test("missing model names use an explicit fallback", () => {
  modelsManager.render([{active: false, element_name: "missing-name"}]);

  const item = modelsContainer.children[0];
  assert.equal(item.querySelector(".model-task").textContent, "Unknown model");
  assert.equal(item.querySelector(".model-details"), null);
});

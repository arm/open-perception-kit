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

import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
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
    this.dataset = {};
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
const microphoneButton = new FakeElement("button");
const microphoneIcon = new FakeElement("span");
const microphoneStatus = new FakeElement("div");

globalThis.document = {
  body: new FakeElement("body"),
  createElement: (tagName) => new FakeElement(tagName),
  getElementById: (id) => {
    if (id === "models-container") return modelsContainer;
    if (id === "muteUnmuteBtn") return muteButton;
    if (id === "muteUnmuteIcon") return muteIcon;
    if (id === "microphoneBtn") return microphoneButton;
    if (id === "microphoneIcon") return microphoneIcon;
    if (id === "microphoneStatus") return microphoneStatus;
    if (["cameraBtn", "cameraIcon", "cameraStatus"].includes(id)) return new FakeElement("span");
    return null;
  },
};
globalThis.location = {
  href: "http://localhost:9999/",
  protocol: "http:",
  hostname: "localhost",
  port: "9999",
};
globalThis.window = {
  OPK_CONFIG: {ctrlPort: 8001},
  addEventListener() {},
  dispatchEvent() {},
};
globalThis.CustomEvent = function CustomEvent(type, init) {
  this.type = type;
  this.detail = init?.detail;
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

const {modelsManager} = await import("../src/models.js");

test("selector keeps the current default detector first in the preferred model order", () => {
  const models = [
    "yolo26n-320",
    "ultraface-rfb-320",
    "nitec-resnet-18",
    "mobilegaze-mobilenet-v2",
  ].map((id) => JSON.parse(readFileSync(
    new URL(`../../../config/models/${id}/opchain.json`, import.meta.url), "utf8")));

  modelsManager.render([...models].reverse());

  assert.deepEqual(
    modelsContainer.children.map((item) => item.getAttribute("data-model-name")),
    models.map((model) => model.name),
  );
});

test("selector renders task and model details from descriptor metadata", () => {
  const model = {
    active: false,
    displayName: "YOLOv11n",
    element_name: "opkinfer1",
    name: "YoloV11",
    runtime: "ONNX",
    task: "Object detection",
    providedOutputs: [{payloadType: "open_perception_kit.metadata.BoxDetections", producerId: "opkinfer1/detector"}],
  };

  modelsManager.render([model]);

  const item = modelsContainer.children[0];
  const task = item.querySelector(".model-task");
  const details = item.querySelector(".model-details");
  const toggleLabel = item.querySelector("label");
  const toggle = item.querySelector("input");

  assert.equal(item.getAttribute("data-model-name"), "YoloV11");
  assert.equal(item.getAttribute("data-model-element-name"), "opkinfer1");
  assert.equal(task.textContent, "Object detection");
  assert.equal(details.textContent, "YOLOv11n (ONNX)");
  assert.equal(item.querySelector(".model-dependency-info"), null);
  assert.equal(item.querySelector(".model-copy").title, "Object detection - YOLOv11n (ONNX)");
  assert.equal(toggleLabel.getAttribute("aria-label"), "Toggle Object detection - YOLOv11n (ONNX)");

  toggle.checked = true;
  toggle.dispatchEvent({type: "change"});

  assert.deepEqual(sentMessages.at(-1), {
    type: "model_toggle",
    name: "opkinfer1",
  });
});

test("dependent models show their provider networks beside the toggle", () => {
  modelsManager.render([
    {
      active: false,
      displayName: "L2CS MobileGaze",
      element_name: "opkinfer2",
      name: "GazeDetection",
      requiredInputs: [{consumerId: "roi", inputId: "input", payloadType: "open_perception_kit.metadata.BoxDetections", producerId: "opkinfer1/detector"}],
    },
    {
      active: false,
      displayName: "UltraFace",
      element_name: "opkinfer1",
      name: "Ultraface",
      providedOutputs: [{payloadType: "open_perception_kit.metadata.BoxDetections", producerId: "opkinfer1/detector"}],
      task: "Face detection",
    },
    {
      active: false,
      displayName: "YOLOv11n",
      element_name: "opkinfer0",
      name: "YoloV11",
      providedOutputs: [{payloadType: "open_perception_kit.metadata.Classifications", producerId: "opkinfer0/output"}],
    },
  ]);

  const dependentItem = modelsContainer.children.find(
    (item) => item.getAttribute("data-model-name") === "GazeDetection");
  const actions = dependentItem.querySelector(".model-actions");
  const dependencyInfo = actions.querySelector(".model-dependency-info");
  const dependencyPopup = dependencyInfo.querySelector(".model-dependency-popup");

  const dependencyIcon = dependencyInfo.querySelector(".model-dependency-icon");
  assert.equal(dependencyIcon.tagName, "SPAN");
  assert.equal(dependencyIcon.getAttribute("data-icon"), "info");
  assert.equal(dependencyIcon.getAttribute("aria-hidden"), "true");
  assert.equal(dependencyPopup.querySelector(".model-dependency-heading").textContent, "Depends on:");
  assert.equal(dependencyPopup.querySelector("li").textContent, "Face detection");
  assert.equal(dependencyInfo.getAttribute("aria-label"), "Depends on: Face detection");
  assert.equal(actions.children[0], dependencyInfo);
  assert.equal(actions.children[1].tagName, "LABEL");
});

test("dependent models identify missing providers", () => {
  modelsManager.render([{
    active: false,
    element_name: "opkinfer0",
    name: "GazeDetection",
    providedOutputs: "humanFace",
    requiredInputs: [null, {}, {consumerId: "roi", inputId: "input", payloadType: "open_perception_kit.metadata.BoxDetections", producerId: "missing/output"}],
  }]);

  const dependencyInfo = modelsContainer.children[0].querySelector(".model-dependency-info");
  assert.equal(dependencyInfo.getAttribute("aria-label"), "Depends on: Selected source unavailable");
  assert.equal(dependencyInfo.querySelector("li").textContent, "Selected source unavailable");
});

test("input choices use compatible observed upstream outputs and submit concrete bindings", () => {
  const payloadType = "open_perception_kit.metadata.BoxDetections";
  const first = {payloadType, producerId: "first/output"};
  const second = {payloadType, producerId: "second/output"};
  const binding = {consumerId: "crop", inputId: "input", ...first};
  const consumer = {name: "Consumer", element_name: "consumer", requiredInputs: [binding],
    availableInputs: [first, second]};
  modelsManager.render([
    {name: "First", element_name: "first", providedOutputs: [first]},
    {name: "Second", element_name: "second", providedOutputs: [second]},
    {name: "Downstream", element_name: "downstream", providedOutputs: [{payloadType, producerId: "downstream/output"}]},
    consumer,
  ]);
  const item = modelsContainer.children.find(element => element.getAttribute("data-model-name") === "Consumer");
  const select = item.querySelector("select");
  assert.ok(select.getAttribute("aria-label"));
  assert.deepEqual(select.children.map(option => option.value), ["", first.producerId, second.producerId]);
  select.value = second.producerId;
  select.dispatchEvent({type: "change"});
  assert.deepEqual(sentMessages.at(-1), {type: "model_inputs", name: "consumer",
    bindings: [{...binding, producerId: second.producerId}]});
});

test("successive edits send both current input selections before a server update", () => {
  const payloadType = "open_perception_kit.metadata.BoxDetections";
  const first = {payloadType, producerId: "first/output"};
  const second = {payloadType, producerId: "second/output"};
  const requiredInputs = [{consumerId: "reader", inputId: "left", ...first},
    {consumerId: "reader", inputId: "right", ...second}];
  modelsManager.render([
    {name: "First", element_name: "first", providedOutputs: [first]},
    {name: "Second", element_name: "second", providedOutputs: [second]},
    {name: "Consumer", element_name: "consumer", requiredInputs, availableInputs: [first, second]},
  ]);
  const item = modelsContainer.children.find(element => element.getAttribute("data-model-name") === "Consumer");
  const selectors = item.querySelector(".model-copy").children
    .filter(child => child.classList.values.has("model-input-binding"))
    .map(label => label.querySelector("select"));
  selectors[0].value = second.producerId;
  selectors[0].dispatchEvent({type: "change"});
  selectors[1].value = first.producerId;
  selectors[1].dispatchEvent({type: "change"});
  assert.deepEqual(sentMessages.at(-1), {type: "model_inputs", name: "consumer", bindings: [
    {...requiredInputs[0], producerId: second.producerId}, {...requiredInputs[1], producerId: first.producerId},
  ]});
  assert.deepEqual(requiredInputs.map(input => input.producerId), [first.producerId, second.producerId]);
});

test("duplicate descriptor names retain unique element identities", () => {
  modelsManager.render([
    {
      active: false,
      displayName: "MobileNetV2",
      element_name: "opkinfer-onnx",
      name: "ImageNet",
      runtime: "ONNX",
      task: "Image classification",
    },
    {
      active: false,
      displayName: "MobileNetV2",
      element_name: "opkinfer-executorch",
      name: "ImageNet",
      runtime: "ExecuTorch",
      task: "Image classification",
    },
  ]);

  assert.equal(modelsContainer.children.length, 2);
  assert.equal(modelsContainer.children[0].getAttribute("data-model-name"), "ImageNet");
  assert.equal(modelsContainer.children[1].getAttribute("data-model-name"), "ImageNet");
  assert.equal(modelsContainer.children[0].getAttribute("data-model-element-name"), "opkinfer-onnx");
  assert.equal(modelsContainer.children[1].getAttribute("data-model-element-name"), "opkinfer-executorch");
  assert.equal(modelsContainer.children[0].querySelector(".model-details").textContent,
    "MobileNetV2 (ONNX)");
  assert.equal(modelsContainer.children[1].querySelector(".model-details").textContent,
    "MobileNetV2 (ExecuTorch)");

  const secondToggle = modelsContainer.children[1].querySelector("input");
  secondToggle.checked = true;
  secondToggle.dispatchEvent({type: "change"});

  assert.deepEqual(sentMessages.at(-1), {
    type: "model_toggle",
    name: "opkinfer-executorch",
  });
});

test("duplicate output addresses remain visibly ambiguous instead of selecting a provider", () => {
  const output = {payloadType: "open_perception_kit.metadata.BoxDetections", producerId: "duplicate/output"};
  modelsManager.render([
    {name: "First", element_name: "one", providedOutputs: [output]},
    {name: "Second", element_name: "two", providedOutputs: [output]},
    {name: "Consumer", element_name: "consumer", availableInputs: [output],
      requiredInputs: [{consumerId: "crop", inputId: "input", ...output}]},
  ]);
  const item = modelsContainer.children.find(element => element.getAttribute("data-model-name") === "Consumer");
  const select = item.querySelector("select");
  assert.equal(select.value, output.producerId);
  assert.equal(select.children[1].disabled, true);
  assert.match(select.children[1].textContent, /ambiguous/);
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

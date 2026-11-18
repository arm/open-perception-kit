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

  contains(name) {
    return this.values.has(name);
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
    child.parentElement = this;
    this.children.push(child);
    return child;
  }

  append(...children) {
    children.forEach((child) => this.appendChild(child));
  }

  cloneNode(deep) {
    const copy = new FakeElement(this.tagName);
    copy.className = this.className;
    copy.attributes = new Map(this.attributes);
    if (deep) copy.children = this.children.map(child => child.cloneNode(true));
    return copy;
  }

  remove() { this.removed = true; }

  querySelectorAll(selector) {
    const matches = [];
    for (const child of this.children) {
      if (child.tagName === selector.toUpperCase()) matches.push(child);
      matches.push(...child.querySelectorAll(selector));
    }
    return matches;
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

  closest(selector) {
    if (selector.startsWith(".") && this.classList.contains(selector.slice(1))) return this;
    return this.parentElement?.closest(selector) ?? null;
  }

  getBoundingClientRect() {
    return {left: 0, top: 0};
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

test("selector preserves the reported pipeline order", () => {
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
    [...models].reverse().map((model) => model.name),
  );
  const stages = models.map((model, index) => ({...model, element_name: `model-${index}`, modelGroup: 'inference'}));
  const editor = {modelGroups: [{id: 'inference'}], revision: 7};
  const stopped = {state: 'READY', pending: '', busy: false};
  const editButtons = () => modelsContainer.children.flatMap(item => item.querySelector('.model-edit-actions').children);
  for (const state of [null, {...stopped, state: 'PLAYING'}, {...stopped, state: 'PAUSED'},
    {...stopped, pending: 'PLAYING'}, {...stopped, busy: true}]) {
    modelsManager.render(stages, editor, state);
    assert.ok(editButtons().every(button => button.disabled));
    assert.ok(editButtons().every(button => button.hidden));
    assert.ok(modelsContainer.children.every(item => item.querySelector('.model-edit-actions').hidden));
  }
  modelsManager.render(stages, editor, stopped);
  assert.ok(editButtons().every(button => button.hidden), 'stopped does not imply editing');
  modelsManager.beginEditing();
  const items = () => modelsContainer.children.filter(item => item.classList.contains('model-item'));
  const handle = items()[0].querySelector('.model-drag-handle');
  assert.equal(handle.disabled, false);
  assert.equal(handle.hidden, false);
  const before = sentMessages.length;
  handle.dispatchEvent({type: 'keydown', key: 'ArrowDown', preventDefault() {}});
  assert.equal(sentMessages.length, before, 'draft moves do not change the server');
  assert.equal(modelsManager.models[1].element_name, stages[0].element_name);
  modelsManager.discardEdits();
  assert.deepEqual(modelsManager.models.map(model => model.element_name), stages.map(model => model.element_name));

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
  assert.equal(dependencyPopup.querySelector("li").textContent, "Face detection (off)");
  assert.equal(dependencyInfo.getAttribute("aria-label"), "Depends on: Face detection (off)");
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


test("only the drag handle starts a model move, and editing state gates it", () => {
  const models = ["first", "second"].map(name => ({name, element_name: name, modelGroup: "main"}));
  const editor = {revision: 7, modelGroups: [{id: "main"}]};
  modelsManager.render(models, editor, {state: "READY"});
  modelsManager.beginEditing();
  const item = modelsContainer.children.find(item => item.classList.contains('model-item'));
  const handle = item.querySelector('.model-drag-handle');
  assert.equal(handle.draggable, true);
  assert.equal(Boolean(item.draggable), false);
  assert.equal(item.listeners.has('dragstart'), false);
  item.getBoundingClientRect = () => ({left: 0, top: 0, width: 339, height: 240});
  let payload;
  const event = {type: 'dragstart', clientX: 5, clientY: 5,
    preventDefault() { throw new Error('Enabled handle should allow dragging'); },
    dataTransfer: {setDragImage(preview) {
      assert.notEqual(preview, item);
      assert.ok(preview.classList.contains('model-drag-preview'));
      assert.equal(preview.style.width, '339px');
      assert.equal(preview.style.height, '240px');
      assert.equal(preview.querySelector('.model-insertion'), null);
    }, setData(type, value) { payload = value; }}};
  handle.dispatchEvent(event);
  assert.equal(payload, 'first');
  assert.equal(modelsManager.draggedModel.name, 'first');
  handle.dispatchEvent({type: 'dragend'});
  assert.equal(modelsManager.draggedModel, null);
  assert.equal(modelsManager.dragPreview, null);
  modelsManager.pipelineState = {state: 'PLAYING'};
  modelsManager.updateEditorControls();
  assert.equal(handle.draggable, false);
  let blocked = false;
  handle.dispatchEvent({type: 'dragstart', preventDefault() { blocked = true; }});
  assert.equal(blocked, true);
  modelsManager.discardEdits();
});

test('drop markers identify the insertion boundary and submit the matching final position', () => {
  const models = ['a', 'b', 'c', 'd'].map(name => ({name, element_name: name, modelGroup: 'main'}));
  for (const [source, target, y, expected, marker] of [
    [0, 2, 10, 1, 'model-drop-before'],
    [0, 2, 90, 2, 'model-drop-after'],
    [3, 1, 10, 1, 'model-drop-before'],
    [3, 1, 90, 2, 'model-drop-after'],
    [0, 1, 10, null, null],
  ]) {
    modelsManager.render(models, {revision: 8, modelGroups: [{id: 'main'}]}, {state: 'READY'});
    modelsManager.beginEditing();
    const item = modelsContainer.children.filter(item => item.classList.contains('model-item'))[target];
    item.getBoundingClientRect = () => ({top: 0, height: 100});
    modelsManager.draggedModel = {name: models[source].name, modelGroup: 'main', revision: 8, position: source};
    const before = sentMessages.length;
    item.dispatchEvent({type: 'dragover', clientY: y, preventDefault() {}, dataTransfer: {}});
    const boundary = target + (y >= 50 ? 1 : 0);
    const divider = modelsManager.insertionTargets.get(JSON.stringify(['main', boundary]));
    assert.equal(modelsManager.dropTarget, marker ? divider : null);
    if (marker) assert.ok(divider.classList.contains('model-drop-target'));
    item.dispatchEvent({type: 'drop', clientY: y, preventDefault() {}});
    assert.equal(modelsManager.dropTarget, null);
    assert.equal(item.classList.contains('model-drop-before'), false);
    assert.equal(item.classList.contains('model-drop-after'), false);
    assert.equal(sentMessages.length, before);
    if (expected !== null) assert.equal(modelsManager.models[expected].element_name, models[source].element_name);
    modelsManager.discardEdits();
  }
});

test("edit mode provides every insertion boundary and preserves model group positions", () => {
  const models = [
    {name: 'First', element_name: 'first', modelGroup: 'a'},
    {name: 'Second', element_name: 'second', modelGroup: 'a'},
  ];
  const editor = {revision: 7, modelGroups: [{id: 'a'}, {id: 'empty'}]};
  modelsManager.render(models, editor, {state: 'READY'});
  modelsManager.beginEditing();
  assert.equal(modelsManager.insertionButtons.length, 4);
  const originalPicker = modelsManager.openPicker;
  const selections = [];
  modelsManager.openPicker = (model, insertion) => selections.push(insertion);
  for (const button of modelsManager.insertionButtons) button.dispatchEvent({type: 'click'});
  assert.deepEqual(selections, [
    {modelGroup: 'a', position: 0}, {modelGroup: 'a', position: 1},
    {modelGroup: 'a', position: 2}, {modelGroup: 'empty', position: 0},
  ]);
  modelsManager.openPicker = originalPicker;
  modelsManager.render(models, editor, {state: 'PLAYING'});
  assert.equal(modelsManager.insertionButtons.length, 0);
  assert.equal(modelsManager.editMode, false);
});


test('saving a draft waits for server acknowledgements and maps newly added model names', () => {
  const models = [{name: 'Original', element_name: 'original', modelGroup: 'a'}];
  const editor = {revision: 20, modelGroups: [{id: 'a'}], availableOpChains: [{name: 'Added', path: '/added.json'}]};
  const state = {state: 'READY', busy: false, pending: '', error: ''};
  modelsManager.render(models, editor, state);
  modelsManager.beginEditing();
  const before = sentMessages.length;
  modelsManager.submitEdit({action: 'add', modelGroup: 'a', path: '/added.json', position: 0});
  const draftName = modelsManager.models[0].element_name;
  modelsManager.submitEdit({action: 'move', modelGroup: 'a', name: draftName, position: 1});
  assert.equal(sentMessages.length, before);
  modelsManager.saveEdits();
  assert.deepEqual(sentMessages.at(-1), {type:'model_edit', action:'add', modelGroup:'a', path:'/added.json', position:0, revision:20});
  modelsManager.render(models, editor, {...state, busy:true});
  assert.equal(sentMessages.length, before + 1);
  const added = {name: 'Added', element_name:'server-added', modelGroup:'a'};
  modelsManager.render([added, ...models], {...editor, revision:21}, state);
  assert.deepEqual(sentMessages.at(-1), {type:'model_edit', action:'move', modelGroup:'a', name:'server-added', position:1, revision:21});
  modelsManager.render([...models, added], {...editor, revision:22}, state);
  assert.equal(modelsManager.saving, false);
  assert.equal(modelsManager.editMode, false);
  assert.deepEqual(modelsManager.models.map(model=>model.element_name), ['original','server-added']);
});

test('discarding add, replace and remove drafts never sends changes', () => {
  const models = [{name:'First', element_name:'first', modelGroup:'a'}, {name:'Second', element_name:'second', modelGroup:'a'}];
  modelsManager.render(models, {revision:30, modelGroups:[{id:'a'}], availableOpChains:[{name:'New',path:'/new.json'}]}, {state:'READY'});
  modelsManager.beginEditing();
  const before = sentMessages.length;
  modelsManager.submitEdit({action:'replace', modelGroup:'a', name:'first', path:'/new.json'});
  modelsManager.submitEdit({action:'remove', modelGroup:'a', name:'second'});
  modelsManager.submitEdit({action:'add', modelGroup:'a', path:'/new.json', position:1});
  modelsManager.discardEdits();
  assert.equal(sentMessages.length,before);
  assert.deepEqual(modelsManager.models,models);
});

test('a changed server revision invalidates an unsaved draft without sending it', () => {
  const models = [{name:'First', element_name:'first', modelGroup:'a'}];
  const editor = {revision:40, modelGroups:[{id:'a'}]};
  modelsManager.render(models, editor, {state:'READY'});
  modelsManager.beginEditing();
  const before = sentMessages.length;
  modelsManager.submitEdit({action:'remove',modelGroup:'a',name:'first'});
  modelsManager.render(models,{...editor,revision:41},{state:'READY'});
  assert.equal(modelsManager.editMode,false);
  assert.deepEqual(modelsManager.models,models);
  assert.equal(sentMessages.length,before);
});


test('dragging over either half of an Add control chooses the same insertion boundary', () => {
  const models = ['a', 'b', 'c'].map(name => ({name,element_name:name,modelGroup:'main'}));
  for (const y of [1,31]) {
    modelsManager.render(models,{revision:50,modelGroups:[{id:'main'}]},{state:'READY'});
    modelsManager.beginEditing();
    const divider = modelsManager.insertionTargets.get(JSON.stringify(['main',2]));
    modelsManager.draggedModel = {name:'a',modelGroup:'main',position:0};
    divider.dispatchEvent({type:'dragover',clientY:y,preventDefault(){},dataTransfer:{}});
    assert.equal(modelsManager.dropTarget,divider);
    assert.ok(divider.classList.contains('model-drop-target'));
    divider.dispatchEvent({type:'drop',clientY:y,preventDefault(){}});
    assert.deepEqual(modelsManager.models.map(model=>model.element_name),['b','a','c']);
    assert.equal(divider.classList.contains('model-drop-target'),false);
    modelsManager.discardEdits();
  }
});


test('dependency tooltip follows the selected source and its activation state', () => {
  const payloadType = 'boxes';
  const face = {name:'Face', task:'Face detection', element_name:'face', active:true, providedOutputs:[{payloadType,producerId:'face/output'}]};
  const objects = {name:'Objects', task:'Object detection', element_name:'objects', active:true, providedOutputs:[{payloadType,producerId:'objects/output'}]};
  const consumer = {name:'Gaze', element_name:'gaze', requiredInputs:[]};
  const check = (producerId, expected) => {
    consumer.requiredInputs = [{consumerId:'crop',inputId:'input',payloadType,producerId}];
    modelsManager.render([face,objects,consumer]);
    assert.equal(modelsContainer.children[2].querySelector('.model-dependency-info').getAttribute('aria-label'), `Depends on: ${expected}`);
  };
  check('face/output','Face detection');
  check('objects/output','Object detection');
  objects.active = false;
  check('objects/output','Object detection (off)');
  check('','No input connected');
  check('missing/output','Selected source unavailable');
});


test('dependency icon warns only for an enabled consumer with a selected disabled provider', () => {
  const source = {name:'Face',element_name:'face',active:false,providedOutputs:[{payloadType:'boxes',producerId:'face/output'}]};
  const consumer = {name:'Gaze',element_name:'gaze',active:true,requiredInputs:[{consumerId:'crop',inputId:'input',payloadType:'boxes',producerId:'face/output'}]};
  const warns = () => {
    modelsManager.render([source,consumer]);
    return modelsContainer.children[1].querySelector('.model-dependency-info').classList.contains('model-dependency-warning');
  };
  assert.equal(warns(),true);
  consumer.active = false;
  assert.equal(warns(),false);
  consumer.active = true;
  source.active = true;
  assert.equal(warns(),false);
  source.active = false;
  consumer.requiredInputs[0].producerId = 'different/output';
  assert.equal(warns(),false);
});


test('dropdown updates dependency text and warning before any server report arrives', () => {
  const payloadType = 'boxes';
  const face = {name:'Face',element_name:'face',active:true,providedOutputs:[{payloadType,producerId:'face/output'}]};
  const objects = {name:'Objects',element_name:'objects',active:false,providedOutputs:[{payloadType,producerId:'objects/output'}]};
  const consumer = {name:'Gaze',element_name:'gaze',active:true,requiredInputs:[{consumerId:'crop',inputId:'input',payloadType,producerId:'face/output'}],availableInputs:[...face.providedOutputs,...objects.providedOutputs]};
  modelsManager.render([face,objects,consumer]);
  const item = modelsContainer.children[2];
  const dropdown = item.querySelector('select');
  const info = item.querySelector('.model-dependency-info');
  dropdown.value = 'objects/output';
  dropdown.dispatchEvent({type:'change'});
  assert.equal(info.getAttribute('aria-label'),'Depends on: Objects (off)');
  assert.equal(info.classList.contains('model-dependency-warning'),true);
  dropdown.value = '';
  dropdown.dispatchEvent({type:'change'});
  assert.equal(info.getAttribute('aria-label'),'Depends on: No input connected');
  assert.equal(info.classList.contains('model-dependency-warning'),false);
});

// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

import assert from "node:assert/strict";
import test from "node:test";

import {
  calculateContainedRect,
  classificationHeading,
  classificationPanelHeight,
  classificationTextX,
  collectRectsByContentType,
  createCoordinateMapper,
  findParentRect,
  findVideoFrame,
  gazeEndpoint,
  renderOsd,
} from "../src/osd-renderer.js";

function createCanvasContext() {
  const calls = [];
  return {
    calls,
    setTransform() {},
    clearRect() {},
    save() {},
    restore() {},
    measureText(text) {
      return {width: text.length * 8};
    },
    fillRect() {},
    fillText(text, x, y) {
      calls.push({text, x, y});
    },
  };
}

test("gaze projection does not reverse across 90 degrees", () => {
  const start = {x: 160, y: 120};
  const before = gazeEndpoint(start, 88, 0, 120);
  const after = gazeEndpoint(start, 92, 0, 120);

  assert.ok(before.x < start.x);
  assert.ok(after.x < start.x);
  assert.ok(Math.abs(before.x - after.x) < 1e-9);
});

test("classification heading identifies the payload producer implementation", () => {
  assert.equal(classificationHeading({
    producer: {implementation: "ImageNetClassificationParser"},
  }), "ImageNetClassificationParser");
  assert.equal(classificationHeading({
    producer: {implementation: "python_classification.py"},
  }), "python_classification.py");
  assert.equal(classificationHeading({}), "");
});

test("classification text supports explicit lower-right alignment", () => {
  const display = {x: 20, y: 0, width: 1000, height: 300};

  assert.equal(classificationTextX(display, 10, false), 30);
  assert.equal(classificationTextX(display, 10, true), 1010);
});

test("classification panels reserve deterministic vertical space", () => {
  assert.equal(classificationPanelHeight(5, "producer"), 146);
  assert.equal(classificationPanelHeight(5, ""), 125);
});

test("classification layers render labelled left and right columns", () => {
  globalThis.window = {devicePixelRatio: 1};
  const context = createCanvasContext();
  const canvas = {
    clientWidth: 1000,
    clientHeight: 500,
    width: 0,
    height: 0,
    getContext: () => context,
  };
  const candidates = [
    {text: "nematode", classId: 111, confidence: 0.75},
    {classId: 222, confidence: 0.25},
  ];
  const perception = {
    layers: [
      {
        contentType: "classification",
        compositingMode: "bottomLeft",
        producer: {implementation: "ImageNetClassificationParser"},
        detections: [{type: "Classification", data: {candidates}}],
      },
      {
        contentType: "classification",
        compositingMode: "bottomRight",
        producer: {implementation: "python_classification.py"},
        detections: [{type: "Classification", data: {candidates}}],
      },
      {
        contentType: "classification",
        detections: [{type: "Classification", data: {candidates: []}}],
      },
    ],
  };

  renderOsd(canvas, {videoWidth: 1000, videoHeight: 500}, perception, {
    objects: false,
    faces: false,
    gaze: false,
    cameraContact: false,
    trackTraces: false,
    personStatus: false,
    performance: false,
  });

  assert.deepEqual(context.calls.map(({text}) => text), [
    "ImageNetClassificationParser",
    "#1: nematode (75.0%)",
    "#2: 222 (25.0%)",
    "python_classification.py",
    "#1: nematode (75.0%)",
    "#2: 222 (25.0%)",
  ]);
  assert.ok(context.calls[0].x < context.calls[3].x);
});

test("classification columns stay separated on a typical narrow video", () => {
  globalThis.window = {devicePixelRatio: 1};
  const context = createCanvasContext();
  const canvas = {
    clientWidth: 640,
    clientHeight: 360,
    width: 0,
    height: 0,
    getContext: () => context,
  };
  const candidates = [
    {text: "chainlink fence", classId: 489, confidence: 0.56},
  ];
  const perception = {
    layers: [
      {
        contentType: "classification",
        compositingMode: "bottomLeft",
        producer: {implementation: "ImageNetClassificationParser"},
        detections: [{type: "Classification", data: {candidates}}],
      },
      {
        contentType: "classification",
        compositingMode: "bottomRight",
        producer: {implementation: "python_classification.py"},
        detections: [{type: "Classification", data: {candidates}}],
      },
    ],
  };

  renderOsd(canvas, {videoWidth: 1280, videoHeight: 720}, perception, {
    objects: false,
    faces: false,
    gaze: false,
    cameraContact: false,
    trackTraces: false,
    personStatus: false,
    performance: false,
  });

  const leftHeading = context.calls.find(
    ({text}) => text === "ImageNetClassificationParser",
  );
  const rightHeading = context.calls.find(
    ({text}) => text === "python_classification.py",
  );
  assert.ok(leftHeading.x < 20);
  assert.ok(rightHeading.x > 400);
});

test("video frame metadata is preferred when present", () => {
  const perception = {
    layers: [
      {
        contentType: "videoFrame",
        detections: [
          {
            type: "VideoFrame",
            data: {originalWidth: 640, originalHeight: 480},
          },
        ],
      },
    ],
  };

  assert.deepEqual(findVideoFrame(perception), {originalWidth: 640, originalHeight: 480});
});

test("contained rect accounts for horizontal letterboxing", () => {
  assert.deepEqual(calculateContainedRect(1000, 500, 500, 500), {
    x: 250,
    y: 0,
    width: 500,
    height: 500,
  });
});

test("coordinate mapper scales into the displayed video rect", () => {
  const mapper = createCoordinateMapper({
    canvasWidth: 1000,
    canvasHeight: 500,
    videoWidth: 500,
    videoHeight: 500,
    perception: {layers: []},
  });

  assert.deepEqual(mapper.point(250, 250), {x: 500, y: 250});
  assert.equal(mapper.lengthX(100), 100);
});

test("parent face rect is found by uuid and content type", () => {
  const perception = {
    layers: [
      {
        contentType: "genericObject",
        detections: [{type: "Rect", data: {uuid: 1}}],
      },
      {
        contentType: "humanFace",
        detections: [{type: "Rect", data: {uuid: 2}}],
      },
    ],
  };

  assert.deepEqual(collectRectsByContentType(perception, "humanFace"), [{uuid: 2}]);
  assert.deepEqual(findParentRect(perception, "humanFace", 2), {uuid: 2});
  assert.equal(findParentRect(perception, "humanFace", 1), null);
});

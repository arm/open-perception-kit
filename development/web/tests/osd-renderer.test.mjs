import assert from "node:assert/strict";
import test from "node:test";

import {
  calculateContainedRect,
  classificationHeading,
  classificationTextX,
  collectRectsByContentType,
  createCoordinateMapper,
  findParentRect,
  findVideoFrame,
} from "../src/osd-renderer.js";

test("classification heading identifies the payload producer implementation", () => {
  assert.equal(classificationHeading({
    producer: {implementation: "ImageNetClassificationParser"},
  }), "ImageNetClassificationParser");
  assert.equal(classificationHeading({
    producer: {implementation: "tensor_metrics_overlay.py"},
  }), "tensor_metrics_overlay.py");
  assert.equal(classificationHeading({}), "");
});

test("classification text supports explicit lower-right alignment", () => {
  const display = {x: 20, y: 0, width: 1000, height: 300};

  assert.equal(classificationTextX(display, 10, false), 30);
  assert.equal(classificationTextX(display, 10, true), 410);
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

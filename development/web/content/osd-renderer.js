const DEFAULT_COLORS = {
  objects: "#ff3030",
  faces: "#2600ff",
  text: "#ffffff",
  textBg: "rgba(0, 0, 0, 0.78)",
  performance: "#66ff00",
  cameraContact: "#66ff00",
  noContact: "#ff4040",
  gaze: "#fafad2",
  trackTraces: "#ffff00",

};

const DEFAULT_RENDER_OPTIONS = {
  objects: true,
  faces: true,
  gaze: true,
  cameraContact: true,
  trackTraces: true,
  classification: true,
  personStatus: true,
  performance: true,
};

export function resizeCanvasToDisplaySize(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.round(canvas.clientWidth * dpr));
  const height = Math.max(1, Math.round(canvas.clientHeight * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
}

export function renderOsd(canvas, video, perception, options = {}) {
  resizeCanvasToDisplaySize(canvas);

  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const width = canvas.width / dpr;
  const height = canvas.height / dpr;

  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);

  if (!perception || !Array.isArray(perception.layers)) {
    return;
  }

  const mapper = createCoordinateMapper({
    canvasWidth: width,
    canvasHeight: height,
    videoWidth: video.videoWidth,
    videoHeight: video.videoHeight,
    perception,
  });

  const renderOptions = {
    ...DEFAULT_RENDER_OPTIONS,
    ...options,
    colors: {...DEFAULT_COLORS, ...(options.colors || {})},
  };
  const now = options.now ?? Date.now();

  drawLayers(ctx, perception, mapper, renderOptions, now);
  if (renderOptions.gaze) {
    drawGazeVectors(ctx, perception, mapper, renderOptions.colors);
  }
  if (renderOptions.cameraContact) {
    drawCameraContactMarkers(ctx, perception, mapper, renderOptions.colors);
  }
  if (renderOptions.performance) {
    drawPerformance(ctx, perception, renderOptions.colors);
  }
}

export function createCoordinateMapper({canvasWidth, canvasHeight, videoWidth, videoHeight, perception}) {
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
        y: display.y + Number(y || 0) * scaleY,
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
        height: this.lengthY(rect.height ?? rect.h),
      };
    },
  };
}

export function calculateContainedRect(canvasWidth, canvasHeight, videoWidth, videoHeight) {
  if (!positiveNumber(videoWidth) || !positiveNumber(videoHeight)) {
    return {x: 0, y: 0, width: canvasWidth, height: canvasHeight};
  }

  const canvasRatio = canvasWidth / canvasHeight;
  const videoRatio = videoWidth / videoHeight;
  if (canvasRatio > videoRatio) {
    const height = canvasHeight;
    const width = height * videoRatio;
    return {x: (canvasWidth - width) / 2, y: 0, width, height};
  }

  const width = canvasWidth;
  const height = width / videoRatio;
  return {x: 0, y: (canvasHeight - height) / 2, width, height};
}

export function findVideoFrame(perception) {
  for (const layer of perception?.layers || []) {
    for (const detection of layer.detections || []) {
      if (detection?.type === "VideoFrame" && detection.data) {
        return detection.data;
      }
    }
  }
  return null;
}

export function collectRectsByContentType(perception, contentType) {
  const rects = [];
  for (const layer of perception?.layers || []) {
    if (layer.contentType !== contentType) {
      continue;
    }
    for (const detection of layer.detections || []) {
      if (detection?.type === "Rect" && detection.data) {
        rects.push(detection.data);
      }
    }
  }
  return rects;
}

export function findParentRect(perception, contentType, parentUuid) {
  return collectRectsByContentType(perception, contentType).find((rect) => rect.uuid === parentUuid) || null;
}

function layerDetectionData(layer, detectionType) {
  if (!Array.isArray(layer.detections)) {
    return [];
  }

  return layer.detections
    .filter((detection) => detection?.type === detectionType)
    .map((detection) => detection.data);
}

function drawLayerDetections(layer, detectionType, drawDetection) {
  for (const data of layerDetectionData(layer, detectionType)) {
    drawDetection(data);
  }
}

function drawConfiguredLayer(layer, renderer) {
  if (!renderer.enabled || layer.contentType !== renderer.contentType) {
    return;
  }

  drawLayerDetections(layer, renderer.detectionType, renderer.draw);
}

function drawLayers(ctx, perception, mapper, renderOptions, now) {
  const renderers = [
    {
      enabled: renderOptions.trackTraces,
      contentType: "trackTrace",
      detectionType: "TrackTrace",
      draw: (data) => drawTrackTrace(ctx, data, mapper, renderOptions.colors),
    },
    {
      enabled: renderOptions.objects,
      contentType: "genericObject",
      detectionType: "Rect",
      draw: (data) => drawObjectBox(ctx, data, mapper, renderOptions.colors.objects),
    },
    {
      enabled: renderOptions.faces,
      contentType: "humanFace",
      detectionType: "Rect",
      draw: (data) => drawFace(ctx, data, mapper, renderOptions.colors),
    },
    {
      enabled: renderOptions.classification,
      contentType: "classification",
      detectionType: "Classification",
      draw: (data) => drawClassification(ctx, data, mapper.display, renderOptions.colors),
    },
    {
      enabled: renderOptions.personStatus,
      contentType: "personClassification",
      detectionType: "PersonClassification",
      draw: (data) => drawPersonClassification(ctx, data, mapper.display, now, renderOptions.colors),
    },
  ];

  for (const layer of perception.layers) {
    for (const renderer of renderers) {
      drawConfiguredLayer(layer, renderer);
    }
  }
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

function drawFace(ctx, rect, mapper, colors) {
  const box = mapper.rect(rect);
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
  const radius = Math.abs(box.width) / 2;
  ctx.save();
  ctx.strokeStyle = colors.faces;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, Math.PI * 2);
  ctx.stroke();
  ctx.restore();
}

function drawClassification(ctx, classification, display, colors) {
  const candidates = Array.isArray(classification?.candidates) ? classification.candidates : [];
  if (candidates.length === 0) {
    return;
  }

  const fontSize = 14;
  const lineHeight = fontSize * 1.5;
  const padding = 10;
  const startX = display.x + padding;
  const startY = display.y + display.height - candidates.length * lineHeight - padding;
  candidates.forEach((candidate, index) => {
    const text = `#${index + 1}: ${candidate.text || candidate.classId} (${((candidate.confidence || 0) * 100).toFixed(1)}%)`;
    drawTextChip(ctx, text, startX, startY + index * lineHeight, fontSize, colors.classification);
  });
}

function drawPersonClassification(ctx, personClassification, display, now, colors) {
  if (now % 1000 >= 800) {
    return;
  }

  const isPerson = Number(personClassification?.yesConfidence || 0) > Number(personClassification?.noConfidence || 0);
  const label = isPerson ? "PERSON" : "NON-PERSON";
  const color = isPerson ? colors.personStatus : colors.noContact;
  const fontSize = Math.max(28, Math.min(64, display.width / 12));

  ctx.save();
  ctx.font = `700 ${fontSize}px monospace`;
  const metrics = ctx.measureText(label);
  const x = display.x + (display.width - metrics.width) / 2;
  const y = display.y + display.height / 2;
  drawTextChip(ctx, label, x, y, fontSize, color);
  ctx.restore();
}

function drawTrackTrace(ctx, trace, mapper, colors) {
  const points = Array.isArray(trace?.points) ? trace.points : [];
  if (points.length < 2) {
    return;
  }

  const color = colors.trackTraces;
  const segments = points.length - 1;
  for (let i = 0; i < segments; i += 1) {
    const from = mapper.point(points[i].x, points[i].y);
    const to = mapper.point(points[i + 1].x, points[i + 1].y);
    const alpha = 0.35 + 0.65 * ((i + 1) / segments);
    drawArrow(ctx, from, to, hexToRgba(color, alpha), 4, 0);
  }
}

function drawGazeVectors(ctx, perception, mapper, colors) {
  for (const layer of perception.layers) {
    if (layer.contentType !== "eyeYawPitch") {
      continue;
    }

    for (const detection of layer.detections || []) {
      if (detection?.type !== "YawPitch") {
        continue;
      }

      const yp = detection.data;
      const parent = findParentRect(perception, "humanFace", yp.parentUuid);
      if (!parent || nearZero(yp.yaw) && nearZero(yp.pitch)) {
        continue;
      }

      const parentBox = mapper.rect(parent);
      const start = {x: parentBox.x + parentBox.width / 2, y: parentBox.y + parentBox.height / 2};
      const end = gazeEndpoint(start, yp.yaw, yp.pitch, 120);
      drawArrow(ctx, start, end, colors.gaze, 3, 10);
    }
  }
}

function cameraContactClassifications(perception) {
  const classifications = [];
  for (const layer of perception.layers) {
    if (layer.contentType === "cameraContact") {
      classifications.push(...layerDetectionData(layer, "Classification"));
    }
  }
  return classifications;
}

function drawCameraContactMarker(ctx, classification, perception, mapper, colors) {
  const candidate = classification?.candidates?.[0];
  const face = findParentRect(perception, "humanFace", classification?.parentUuid);
  if (!candidate || !face) {
    return;
  }

  const box = mapper.rect(face);
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
  const hasContact = candidate.classId === 1;
  const radiusBase = Math.min(Math.abs(box.width), Math.abs(box.height)) * 0.5;
  const radius = hasContact ? clamp(radiusBase * 0.65, 18, 80) : clamp(radiusBase * 1.15, 28, 140);
  const color = hasContact ? colors.cameraContact : colors.noContact;

  ctx.save();
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = hasContact ? 5 : 8;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, Math.PI * 2);
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(cx, cy, hasContact ? 5 : 7, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function drawCameraContactMarkers(ctx, perception, mapper, colors) {
  for (const classification of cameraContactClassifications(perception)) {
    drawCameraContactMarker(ctx, classification, perception, mapper, colors);
  }
}
function drawPerformance(ctx, perception, colors) {
  const lines = Array.isArray(perception.perfdata) ? perception.perfdata : [];
  let y = 10;
  for (const line of lines) {
    drawTextChip(ctx, String(line), 10, y, 16, colors.performance);
    y += 16;
  }
}

function drawTextChip(ctx, text, x, y, fontSize, color = DEFAULT_COLORS.text) {
  ctx.save();
  ctx.font = `${fontSize}px monospace`;
  ctx.textBaseline = "top";
  const paddingX = 4;
  const paddingY = 2;
  const metrics = ctx.measureText(text);
  const width = metrics.width + paddingX * 2;
  const height = fontSize + paddingY * 2;
  ctx.fillStyle = DEFAULT_COLORS.textBg;
  ctx.fillRect(x, y, width, height);
  ctx.fillStyle = color;
  ctx.fillText(text, x + paddingX, y + paddingY);
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
  let dx = -Math.tan(yaw);
  let dy = -Math.tan(pitch);
  const norm = Math.hypot(dx, dy);
  if (norm <= 0) {
    return {...start};
  }
  dx /= norm;
  dy /= norm;
  return {x: start.x + dx * lengthPx, y: start.y + dy * lengthPx};
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

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function hexToRgba(hex, alpha) {
  const clean = hex.replace("#", "");
  const r = parseInt(clean.slice(0, 2), 16);
  const g = parseInt(clean.slice(2, 4), 16);
  const b = parseInt(clean.slice(4, 6), 16);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

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

export function drawObjectBox(ctx, rect, mapper, color) {
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

export function drawFace(ctx, rect, mapper, color) {
  const box = mapper.rect(rect);
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

export function drawClassification(
  ctx,
  classification,
  display,
  color,
  heading,
  alignRight = false,
  bottomOffset = 0,
) {
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
      alignRight,
    );
  }
  candidates.forEach((candidate, index) => {
    const text = `#${index + 1}: ${candidate.text || candidate.classId} (${((candidate.confidence || 0) * 100).toFixed(1)}%)`;
    drawTextChip(
      ctx,
      text,
      textX,
      startY + index * lineHeight,
      fontSize,
      color,
      alignRight,
    );
  });
  return classificationPanelHeight(candidates.length, heading);
}

export function classificationHeading(layer) {
  return layer?.producer?.implementation || "";
}

export function classificationTextX(display, padding, alignRight) {
  if (!alignRight) {
    return display.x + padding;
  }

  return display.x + display.width - padding;
}

export function classificationPanelHeight(candidateCount, heading) {
  const fontSize = 14;
  const lineHeight = fontSize * 1.5;
  const padding = 10;
  return candidateCount * lineHeight + (heading ? lineHeight : 0) + 2 * padding;
}

export function drawPersonClassification(ctx, personClassification, display, now, color) {
  if (now % 1000 >= 800) {
    return;
  }

  const isPerson = Number(personClassification?.yesConfidence || 0) > Number(personClassification?.noConfidence || 0);
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

export function drawTrackTrace(ctx, trace, mapper, color) {
  const points = Array.isArray(trace?.points) ? trace.points : [];
  if (points.length < 2) {
    return;
  }

  const segments = points.length - 1;
  for (let i = 0; i < segments; i += 1) {
    const from = mapper.point(points[i].x, points[i].y);
    const to = mapper.point(points[i + 1].x, points[i + 1].y);
    const alpha = 0.35 + 0.65 * ((i + 1) / segments);
    drawArrow(ctx, from, to, hexToRgba(color, alpha), 4, 0);
  }
}

export function drawGazeVector(ctx, yp, parent, mapper, color) {
  if (nearZero(yp.yaw) && nearZero(yp.pitch)) return;
  const parentBox = mapper.rect(parent);
  const start = {x: parentBox.x + parentBox.width / 2, y: parentBox.y + parentBox.height / 2};
  drawArrow(ctx, start, gazeEndpoint(start, yp.yaw, yp.pitch, 120), color, 3, 10);
}

export function drawCameraContactMarker(ctx, classification, face, mapper, color) {
  const candidate = classification?.candidates?.[0];
  if (!candidate || !face) {
    return;
  }

  const box = mapper.rect(face);
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;
  const hasContact = candidate.classId === 1;
  const radiusBase = Math.min(Math.abs(box.width), Math.abs(box.height)) * 0.5;
  const radius = hasContact ? clamp(radiusBase * 0.65, 18, 80) : clamp(radiusBase * 1.15, 28, 140);

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

export function drawTextChip(
  ctx,
  text,
  x,
  y,
  fontSize,
  color = '#ffffff',
  alignRight = false,
) {
  ctx.save();
  ctx.font = `${fontSize}px monospace`;
  ctx.textBaseline = "top";
  const paddingX = 4;
  const paddingY = 2;
  const metrics = ctx.measureText(text);
  const width = metrics.width + paddingX * 2;
  const height = fontSize + paddingY * 2;
  const drawX = alignRight ? x - width : x;
  ctx.fillStyle = 'rgba(0, 0, 0, 0.78)';
  ctx.fillRect(drawX, y, width, height);
  ctx.fillStyle = color;
  ctx.fillText(text, drawX + paddingX, y + paddingY);
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

export function gazeEndpoint(start, yawDeg, pitchDeg, lengthPx) {
  const yaw = deg2rad(yawDeg);
  const pitch = deg2rad(pitchDeg);
  return {
    x: start.x - Math.sin(yaw) * Math.cos(pitch) * lengthPx,
    y: start.y - Math.sin(pitch) * lengthPx,
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

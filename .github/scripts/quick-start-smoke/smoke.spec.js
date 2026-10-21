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

const { expect, test } = require('@playwright/test');

const MIN_OBSERVED_FRAMES = 3;
const MIN_FRAMES_DURING_ICE_SETUP = 10;
const NORMAL_WEBSOCKET_CLOSE = 1000;

test.beforeEach(async ({ page }) => {
  await page.addInitScript(() => {
    const NativeWebSocket = window.WebSocket;
    window.smokeWebSockets = [];
    window.WebSocket = class extends NativeWebSocket {
      constructor(...args) {
        super(...args);
        const closed = new Promise((resolve) => {
          this.addEventListener('close', (event) => {
            resolve({ code: event.code, wasClean: event.wasClean });
          }, { once: true });
        });
        window.smokeWebSockets.push({ socket: this, closed });
      }
    };
  });
});

test.afterEach(async ({ page }) => {
  // Finish close handshakes before disposing the browser context and its TCP connections.
  const closedSockets = await page.evaluate((closeCode) =>
    Promise.all(window.smokeWebSockets.map(({ socket, closed }) => {
      if (socket.readyState !== WebSocket.CLOSED) socket.close(closeCode);
      return closed;
    })), NORMAL_WEBSOCKET_CLOSE);
  expect(closedSockets.length).toBeGreaterThan(0);
  for (const closed of closedSockets) {
    expect(closed).toEqual({ code: NORMAL_WEBSOCKET_CLOSE, wasClean: true });
  }
});

test('signaling diagnostics render as text', async ({ page }) => {
  const messages = ['camera ready', '<em>camera & light</em>'];
  await page.routeWebSocket('**/ws', async (ws) => {
    const signalingUrl = new URL(ws.url());
    signalingUrl.port = await page.evaluate(() => window.OPK_CONFIG.wsPort);
    if (ws.url() !== signalingUrl.href) {
      ws.connectToServer();
      return;
    }
    for (const type of messages) ws.send(JSON.stringify({ type }));
  });

  await page.goto('/', { waitUntil: 'domcontentloaded' });

  for (const message of messages) {
    await expect(page.locator('#log')).toContainText(`Received signaling message: ${message}`);
  }
});

test('quick-start plays video and displays an object detection', async ({ page }) => {
  let dropFrameMetadata = false;
  let holdIceCandidates = true;
  let answerReceived = false;
  let framesDuringIceSetup = 0;
  let metadataBeforeIce = 0;
  const pendingCandidates = [];
  await page.routeWebSocket('**/ws', (ws) => {
    const server = ws.connectToServer();
    ws.onMessage((message) => {
      if (holdIceCandidates && JSON.parse(message).type === 'candidate') {
        pendingCandidates.push(() => server.send(message));
        return;
      }
      server.send(message);
    });
    server.onMessage((message) => {
      const data = JSON.parse(message);
      if (data.type === 'answer') answerReceived = true;
      if (holdIceCandidates && data.type === 'candidate') {
        pendingCandidates.push(() => ws.send(message));
        return;
      }
      if (data.type === 'frame-metadata') {
        if (holdIceCandidates) metadataBeforeIce++;
        if (dropFrameMetadata) return;
      } else if (holdIceCandidates && answerReceived && data.frame_results_packet_b64) {
        framesDuringIceSetup++;
      }
      ws.send(message);
    });
  });

  await page.addInitScript(() => {
    const RGBA_COMPONENTS = 4;
    const ALPHA_COMPONENT = 3;
    const observed = window.overlaySmoke = {
      matchedFrames: 0,
      mismatchedFrames: 0,
      framesWithOverlay: 0,
      consecutiveClearFrames: 0,
    };
    let currentTimestamp;
    let frameMetadata;
    window.addEventListener('video-frame-metadata', (event) => {
      if (currentTimestamp === undefined) return;
      frameMetadata = event.detail;
      if (frameMetadata) {
        if (frameMetadata.rtp_timestamp === currentTimestamp) observed.matchedFrames++;
        else observed.mismatchedFrames++;
      }
    });

    const requestFrame = HTMLVideoElement.prototype.requestVideoFrameCallback;
    HTMLVideoElement.prototype.requestVideoFrameCallback = function (callback) {
      return requestFrame.call(this, (now, frame) => {
        currentTimestamp = frame.rtpTimestamp;
        frameMetadata = undefined;
        callback(now, frame);
        const canvas = document.getElementById('clientOsdCanvas');
        if (canvas?.width && canvas?.height) {
          const pixels = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
          const hasOverlay = pixels.some((value, index) =>
            index % RGBA_COMPONENTS === ALPHA_COMPONENT && value !== 0);
          if (frameMetadata && hasOverlay) observed.framesWithOverlay++;
          observed.consecutiveClearFrames = frameMetadata === null && !hasOverlay
            ? observed.consecutiveClearFrames + 1 : 0;
        }
        currentTimestamp = undefined;
      });
    };
  });

  const video = page.locator('#video');
  const objects = page.locator('#inferenceOutputBody .inference-layer').filter({
    has: page.locator('.inference-layer-kind', { hasText: /^genericObject$/ }),
  });

  await page.goto('/', { waitUntil: 'domcontentloaded' });

  // Keep ICE incomplete while the pipeline runs: metadata must not delay signaling.
  await expect.poll(() => framesDuringIceSetup).toBeGreaterThanOrEqual(MIN_FRAMES_DURING_ICE_SETUP);
  expect(metadataBeforeIce).toBe(0);
  holdIceCandidates = false;
  for (const send of pendingCandidates) send();

  await expect.poll(() => video.evaluate((element) =>
    element.readyState >= 2 && element.videoWidth > 0 && element.currentTime > 0
  )).toBe(true);
  await expect(objects.locator('.inference-detection').first()).toBeVisible();
  await expect.poll(() => page.evaluate(() => window.overlaySmoke.matchedFrames))
    .toBeGreaterThanOrEqual(MIN_OBSERVED_FRAMES);
  await expect.poll(() => page.evaluate(() => window.overlaySmoke.framesWithOverlay))
    .toBeGreaterThan(0);
  expect(await page.evaluate(() => window.overlaySmoke.mismatchedFrames)).toBe(0);

  // Keep real video flowing but withhold its metadata: stale overlays must disappear.
  dropFrameMetadata = true;
  await page.evaluate(() => { window.overlaySmoke.consecutiveClearFrames = 0; });
  await expect.poll(() => page.evaluate(() => window.overlaySmoke.consecutiveClearFrames))
    .toBeGreaterThanOrEqual(MIN_OBSERVED_FRAMES);
});

test('Wafer controls work in both themes with keyboard and persisted overlays', async ({ page }, testInfo) => {
  await page.emulateMedia({ colorScheme: 'dark' });
  await page.goto('/', { waitUntil: 'domcontentloaded' });
  await page.evaluate(() => document.fonts.ready);
  await expect(page.locator('body')).toHaveCSS('font-family', /Lato/);
  expect(await page.evaluate(() => [...document.fonts].some(font =>
    font.family === 'Lato' && font.status === 'loaded'))).toBe(true);
  await expect(page.locator('#clientOsdControlsBtn [data-icon]')).toHaveCSS('mask-image', /layers\.svg/);
  await page.getByRole('button', { name: 'Overlays', exact: true }).click();
  await page.locator('#addOverlayBtn').click();
  await page.locator('#overlayFeature').selectOption('background-replacement');
  await page.locator('#saveOverlayBtn').click();
  const toggle = page.getByRole('switch', { name: /^Show Background replacement/ });
  await toggle.focus();
  await page.keyboard.press('Space');
  await expect(toggle).not.toBeChecked();
  await expect(page.locator('.overlay-visible .model-toggle-track')).toHaveCSS('outline-style', 'solid');
  await page.locator('.overlay-toggle').click();
  const card = page.locator('.overlay-card');
  await card.getByLabel('Background image', { exact: true }).setInputFiles({
    name: 'invalid.png', mimeType: 'image/png', buffer: Buffer.from('not an image'),
  });
  await expect(card.locator('input[type=file]')).toHaveAttribute('aria-invalid', 'true');
  await expect(card.locator('[data-state=danger]')).toContainText('Could not prepare');
  await card.getByRole('button', { name: 'Clear image', exact: true }).click();
  await expect(card.locator('input[type=file]')).toHaveAttribute('aria-invalid', 'false');

  const surface = () => page.locator('.sidebar').evaluate(el => getComputedStyle(el).backgroundColor);
  const darkSurface = await surface();
  for (const theme of ['dark', 'light']) {
    await page.emulateMedia({ colorScheme: theme });
    if (theme === 'light') await expect.poll(surface).not.toBe(darkSurface);
    expect(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth)).toBe(false);
    await testInfo.attach(`wafer-${theme}`, { body: await page.screenshot(), contentType: 'image/png' });
  }
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.getByRole('button', { name: 'Overlays', exact: true }).click();
  await expect(toggle).not.toBeChecked();
  await page.locator('#addOverlayBtn').click();
  await expect(page.locator('#overlayFeature')).toBeFocused();
  await page.keyboard.press('Escape');
  await expect(page.locator('#overlayDialog')).not.toBeVisible();
  await expect(page.locator('#addOverlayBtn')).toBeFocused();
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth)).toBe(false);
  await page.locator('.overlay-remove').click();
  await expect(page.locator('.overlay-card')).toHaveCount(0);
});

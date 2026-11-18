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

// These transport assertions apply to real connections, not the UI fixtures below.
test.describe('live WebRTC', () => {
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
    has: page.locator('.inference-layer-kind', { hasText: /^BoxDetections$/ }),
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
  await page.getByRole('button', { name: 'Show performance metrics', exact: true }).click();
  const fpsRow = page.locator('#performanceMetricsBody tr').filter({ hasText: /^Pipeline/ });
  await expect(fpsRow.locator('td').nth(1)).toHaveText(/^\d+\.\d FPS$/);
  // A fresh browser gets editable defaults without using Add overlay.
  await page.getByRole('button', { name: 'Overlays', exact: true }).click();
  await expect(page.getByRole('switch', { name: /^Show Bounding boxes/ }).first()).toBeChecked();
  await page.getByRole('button', { name: 'Overlays', exact: true }).click();
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

});

test('smoke server survives the former 30-second frame limit', async ({request}) => {
  // This wait exercises server lifetime, rather than waiting for a UI condition.
  await new Promise(resolve => setTimeout(resolve, 31000));
  const response = await request.get('/opk-config.js');
  expect(response.ok()).toBe(true);
  expect(await response.text()).toContain('window.OPK_CONFIG');
});

for (const activity of ['keyboard editing', 'image decoding']) {
  test(`new default overlays preserve existing controls during ${activity}`, async ({page}) => {
    await page.routeWebSocket('**/ws', () => {});
    await page.addInitScript(() => {
      const decode = window.createImageBitmap.bind(window);
      window.createImageBitmap = async (...args) => {
        const image = await decode(...args);
        await new Promise(resolve => { window.finishOverlayImage = resolve; });
        return image;
      };
    });
    await page.goto('/', {waitUntil: 'domcontentloaded'});
    const emit = payloadType => page.evaluate(payloadType => {
      window.dispatchEvent(new CustomEvent('metadata-message', {detail: {frame_results: {layers: [{
        payloadType, model: payloadType, producer: {instanceId: `test/${payloadType}`, component: 'test'}, detections: [],
      }]}}}));
    }, payloadType);
    await emit('SegmentationMasks');
    await page.getByRole('button', {name: 'Overlays', exact: true}).click();
    const card = page.locator('.overlay-card').filter({hasText: 'Background replacement'});
    await card.locator('.overlay-toggle').click();
    const original = await card.elementHandle();
    const opacity = card.getByLabel('Opacity', {exact: true});
    if (activity === 'keyboard editing') {
      await opacity.fill('0.42');
      await opacity.focus();
    } else {
      const image = await page.evaluate(() => {
        const canvas = document.createElement('canvas');
        canvas.width = canvas.height = 2;
        canvas.getContext('2d').fillRect(0, 0, 2, 2);
        return canvas.toDataURL('image/png').split(',')[1];
      });
      await card.getByLabel('Background image', {exact: true}).setInputFiles({
        name: 'background.png', mimeType: 'image/png', buffer: Buffer.from(image, 'base64'),
      });
      await expect(card).toContainText('Preparing image');
      await expect.poll(() => page.evaluate(() => typeof window.finishOverlayImage)).toBe('function');
    }
    await emit('BoxDetections');
    await expect(page.locator('.overlay-card')).toHaveCount(2);
    expect(await original.evaluate(element => element.isConnected)).toBe(true);
    await expect(card.locator('.overlay-toggle')).toHaveAttribute('aria-expanded', 'true');
    if (activity === 'keyboard editing') {
      await expect(opacity).toBeFocused();
      await expect(opacity).toHaveValue('0.42');
      await page.keyboard.press('ArrowRight');
      await expect(opacity).toHaveValue('0.43');
    } else {
      await page.evaluate(() => window.finishOverlayImage());
      await expect(card).toContainText('Image prepared locally');
      await expect.poll(() => page.evaluate(() => JSON.parse(localStorage.getItem('opk-overlays:v3'))
        .overlays.find(overlay => overlay.featureId === 'background-replacement').settings.image)).toMatch(/^data:image\//);
    }
    await emit('BoxDetections');
    await expect(page.locator('.overlay-card')).toHaveCount(2);
    expect(await original.evaluate(element => element.isConnected)).toBe(true);
    await page.evaluate(() => window.dispatchEvent(new CustomEvent('metadata-connection', {detail: {connected: false}})));
    await emit('SegmentationMasks');
    await emit('BoxDetections');
    await expect(page.locator('.overlay-card')).toHaveCount(2);
    expect(await original.evaluate(element => element.isConnected)).toBe(true);
  });
}

test('default overlays are editable, removable and persistent without adding an overlay', async ({page}) => {
  await page.routeWebSocket('**/ws', () => {});
  await page.goto('/', {waitUntil: 'domcontentloaded'});
  const emit = (producer = 'detector/boxes', matched = true) => page.evaluate(({producer, matched}) => {
    const frame = {layers: [{payloadType: 'BoxDetections', model: 'Example detector',
      producer: {instanceId: producer, component: 'example/parser', implementation: 'Example'},
      detections: [{type: 'Rect', data: {uuid: 'box', x: 20, y: 30, width: 40, height: 50}}]}]};
    Object.defineProperties(document.getElementById('video'), {
      videoWidth: {configurable: true, value: 100}, videoHeight: {configurable: true, value: 100},
    });
    Object.defineProperties(document.getElementById('clientOsdCanvas'), {
      clientWidth: {configurable: true, value: 100}, clientHeight: {configurable: true, value: 100},
    });
    window.dispatchEvent(new CustomEvent(matched ? 'video-frame-results' : 'metadata-message', {
      detail: matched ? frame : {frame_results: frame},
    }));
  }, {producer, matched});
  const painted = () => page.locator('#clientOsdCanvas').evaluate(canvas =>
    canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data.some((value, i) => i % 4 === 3 && value > 0));
  await emit('detector/boxes', false);
  await page.getByRole('button', {name: 'Overlays', exact: true}).click();
  const card = page.locator('.overlay-card');
  await expect(card).toHaveCount(1);
  await expect(card).toContainText('Bounding boxes · Example detector');
  await expect.poll(painted).toBe(false); // Live discovery does not draw unmatched metadata.
  await emit();
  await expect.poll(painted).toBe(true);
  await card.locator('.overlay-toggle').click();
  await card.getByLabel('Color', {exact: true}).fill('#00ff00');
  await card.getByLabel('Color', {exact: true}).dispatchEvent('change');
  await expect.poll(() => page.locator('#clientOsdCanvas').evaluate(canvas => {
    const pixels = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
    return pixels.some((value, i) => i % 4 === 1 && value === 255 && pixels[i - 1] === 0 && pixels[i + 2] > 0);
  })).toBe(true);
  await card.getByRole('switch').focus();
  await page.keyboard.press('Space');
  await expect.poll(painted).toBe(false);
  await page.reload({waitUntil: 'domcontentloaded'});
  await emit();
  await page.getByRole('button', {name: 'Overlays', exact: true}).click();
  await expect(card).toHaveCount(1);
  await expect(card.getByRole('switch')).not.toBeChecked();
  await card.locator('.overlay-toggle').click();
  await expect(card.getByLabel('Color', {exact: true})).toHaveValue('#00ff00');
  await card.getByRole('switch').focus();
  await page.keyboard.press('Space');
  await expect.poll(painted).toBe(true);
  await card.locator('.overlay-remove').click();
  await emit();
  await expect(card).toHaveCount(0);
  await expect.poll(painted).toBe(false);
  await page.reload({waitUntil: 'domcontentloaded'});
  await emit();
  await page.getByRole('button', {name: 'Overlays', exact: true}).click();
  await expect(card).toHaveCount(0);
  await emit('another/boxes');
  await expect(card).toHaveCount(1);
  await expect(card.getByRole('switch')).toBeChecked();
});

test('media actions stay visible and distinguish capture from playback', async ({page}) => {
  await page.addInitScript(() => {
    navigator.mediaDevices.getUserMedia = () => new Promise((_resolve, reject) => { window.rejectCapture = reject; });
  });
  await page.routeWebSocket('**/ws', async ws => {
    if (new URL(ws.url()).port === String(await page.evaluate(() => window.OPK_CONFIG.ctrlPort)))
      ws.send(JSON.stringify({pipeline_state: {audio: true, microphone: true, playing: true}, models: []}));
  });
  await page.goto('/', {waitUntil: 'domcontentloaded'});
  const mic = page.locator('#microphoneBtn');
  const audio = page.locator('#muteUnmuteBtn');
  await expect(mic).toHaveText('Unmute Mic');
  await expect(audio).toHaveText('Unmute Audio');
  await expect(page.locator('#microphoneStatus')).toBeHidden();
  await expect(page.locator('#cameraStatus')).toBeHidden();
  for (const width of [1366, 390]) {
    await page.setViewportSize({width, height: 768});
    await expect.poll(() => page.evaluate(() => {
      const sidebar = document.getElementById('sidePanel').getBoundingClientRect();
      const buttons = [...document.querySelectorAll('.controls-actions button')].map(button => button.getBoundingClientRect());
      return buttons.every(rect => rect.width > 0 && rect.left >= sidebar.left && rect.right <= sidebar.right
        && rect.top >= sidebar.top && rect.bottom <= sidebar.bottom)
        && buttons.every((a, i) => buttons.slice(i + 1).every(b =>
          a.right <= b.left || b.right <= a.left || a.bottom <= b.top || b.bottom <= a.top));
    })).toBe(true);
  }
  await audio.click();
  await expect(audio).toHaveAccessibleName('Mute Audio');
  await expect(page.locator('#muteUnmuteIcon')).toHaveAttribute('data-icon', 'volume-2');
  await mic.click();
  await expect(mic).toHaveText('Mute Mic');
  await expect(mic).toHaveAccessibleName('Mute Mic');
  await expect(page.locator('#microphoneIcon')).toHaveAttribute('data-icon', 'mic');
  await expect(page.locator('#microphoneStatus')).toContainText('permission');
  await expect(page.locator('#microphoneStatus')).toBeVisible();
  await mic.click();
  await expect(mic).toHaveText('Unmute Mic');
  await expect(page.locator('#microphoneStatus')).toBeHidden();
  await expect(audio).toHaveText('Mute Audio');
  await mic.click();
  await page.evaluate(() => window.rejectCapture(new Error('Permission denied')));
  await expect(mic).toHaveText('Unmute Mic');
  await expect(page.locator('#microphoneIcon')).toHaveAttribute('data-icon', 'mic-off');
  await expect(page.locator('#microphoneStatus')).toContainText('Permission denied');
  await expect(page.locator('#microphoneStatus')).toBeVisible();
  await audio.click();
  await expect(audio).toHaveText('Unmute Audio');
  await expect(page.locator('#muteUnmuteIcon')).toHaveAttribute('data-icon', 'volume-x');
});

test('model and overlay switches share compact geometry and accessible interaction', async ({page}) => {
  await page.emulateMedia({reducedMotion: 'reduce'});
  const sent = [];
  await page.routeWebSocket('**/ws', async ws => {
    if (new URL(ws.url()).port !== String(await page.evaluate(() => window.OPK_CONFIG.ctrlPort))) return;
    ws.onMessage(message => sent.push(JSON.parse(message)));
    ws.send(JSON.stringify({pipeline_state: {audio: false, microphone: false, playing: true}, models: [
      {name: 'Example', element_name: 'example', active: false},
    ]}));
  });
  await page.goto('/', {waitUntil: 'domcontentloaded'});
  await page.getByRole('button', {name: 'Overlays', exact: true}).click();
  await page.locator('#addOverlayBtn').click();
  await page.locator('#overlayFeature').selectOption('background-replacement');
  await page.locator('#saveOverlayBtn').click();
  const model = page.locator('#models-container [role=switch]');
  const overlay = page.locator('.overlay-visible [role=switch]');
  const geometry = selector => page.locator(selector).evaluate(input => {
    const track = input.nextElementSibling;
    const thumb = track.firstElementChild.getBoundingClientRect();
    const bounds = track.getBoundingClientRect();
    const hit = input.getBoundingClientRect();
    return {width: bounds.width, height: bounds.height, thumbWidth: thumb.width, thumbHeight: thumb.height,
      offset: thumb.left - bounds.left, contained: thumb.left >= bounds.left && thumb.right <= bounds.right,
      hitHeight: hit.height, color: getComputedStyle(track).backgroundColor};
  });
  for (const colorScheme of ['light', 'dark']) {
    await page.emulateMedia({colorScheme, reducedMotion: 'reduce'});
    const offsets = [];
    for (const checked of [false, true]) {
      await model.setChecked(checked);
      await overlay.setChecked(checked);
      await page.mouse.move(0, 0);
      const sidebar = await geometry('#models-container [role=switch]');
      offsets.push(sidebar.offset);
      expect(sidebar).toEqual(await geometry('.overlay-visible [role=switch]'));
      expect(sidebar.contained).toBe(true);
      expect(sidebar.width).toBeGreaterThan(sidebar.height);
      expect(sidebar.hitHeight).toBeGreaterThan(sidebar.height);
    }
    expect(offsets[1]).toBeGreaterThan(offsets[0]);
  }
  expect(sent.some(message => message.type === 'model_toggle' && message.name === 'example')).toBe(true);
  await model.focus();
  await page.keyboard.press('Space');
  await expect(model).not.toBeChecked();
  await expect(page.locator('#models-container .model-toggle-track')).toHaveCSS('outline-style', 'solid');
  await model.evaluate(input => { input.disabled = true; });
  await expect(model).toBeDisabled();
  await page.keyboard.press('Space');
  await expect(model).not.toBeChecked();
});

test('background replacement retains graded masks through bounded gaps without timing drift', async ({page}) => {
  await page.routeWebSocket('**/ws', () => {});
  await page.goto('/', {waitUntil: 'domcontentloaded'});
  const result = await page.evaluate(() => {
    let now = 0;
    performance.now = () => now;
    const video = document.getElementById('video');
    const canvas = document.getElementById('clientOsdCanvas');
    Object.defineProperties(video, {videoWidth: {value: 3}, videoHeight: {value: 1}});
    Object.defineProperties(canvas, {clientWidth: {value: 300}, clientHeight: {value: 100}});
    const frame = (pixels, empty = false) => ({layers: [{payloadType: 'SegmentationMasks', model: 'RVM fixture',
      producer: {instanceId: 'rvm/mask', component: 'test', implementation: 'RVM'},
      detections: empty ? [] : [{type: 'SegmentationMask', data: {
        width: 3, height: 1, encoding: 1, meaning: 1, pixels: Uint8Array.from(pixels),
        space: {frameId: 1n, width: 3, height: 1, originX: 0, originY: 0, extentWidth: 3, extentHeight: 1},
      }}]}]});
    const emit = detail => window.dispatchEvent(new CustomEvent('video-frame-results', {detail}));
    const sample = () => [50, 150, 250].map(x => canvas.getContext('2d').getImageData(
      Math.floor(x * devicePixelRatio), Math.floor(50 * devicePixelRatio), 1, 1).data[3]);
    const failures = [];
    const check = (step, expected) => {
      const actual = sample();
      if (actual.some((value, i) => value !== expected[i])) failures.push({step, actual, expected});
    };
    const clear = [0, 0, 0];
    // Exercise more than ten minutes of monotonic presentation time without waiting in real time.
    for (let cycle = 0; cycle < 600; cycle++) {
      const pixels = cycle % 2 ? [255, 128, 0] : [0, 128, 255];
      now += 1000;
      emit(frame(pixels));
      check(`${cycle}: fresh`, pixels);
      emit(null);
      now += 99;
      emit(null);
      check(`${cycle}: held`, pixels);
      // Live-channel masks must neither replace the held mask nor renew its deadline.
      window.dispatchEvent(new CustomEvent('metadata-message', {detail: {frame_results: frame([255, 255, 255])}}));
      check(`${cycle}: live`, pixels);
      now += 1;
      emit(null);
      check(`${cycle}: expired`, clear);
    }
    emit(frame([0, 128, 255]));
    emit(null);
    now += 50;
    emit(frame([255, 128, 0]));
    now += 60;
    check('fresh match cancels expiry', [255, 128, 0]);
    emit(frame([], true));
    check('valid empty clears immediately', clear);
    emit(frame([0, 128, 255]));
    emit(null);
    window.dispatchEvent(new CustomEvent('video-frame-metadata', {detail: {frame_results_encoding: 'invalid'}}));
    check('invalid matched packet clears immediately', clear);
    emit(frame([0, 128, 255]));
    emit(null);
    video.dispatchEvent(new Event('emptied'));
    check('stream reset clears immediately', clear);
    return {failures, elapsedMs: now};
  });
  expect(result.failures).toEqual([]);
  expect(result.elapsedMs).toBeGreaterThan(600000);
});

test('signaling diagnostics render as text', async ({ page }) => {
  const messages = ['camera ready', '<em>camera & light</em>'];
  await page.routeWebSocket('**/ws', async (ws) => {
    const signalingUrl = new URL(ws.url());
    signalingUrl.port = await page.evaluate(() => window.OPK_CONFIG.wsPort);
    if (ws.url() !== signalingUrl.href) {
      return;
    }
    for (const type of messages) ws.send(JSON.stringify({ type }));
  });

  await page.goto('/', { waitUntil: 'domcontentloaded' });

  for (const message of messages) {
    await expect(page.locator('#log')).toContainText(`Received signaling message: ${message}`);
  }
});

test('Wafer controls work in both themes with keyboard and persisted overlays', async ({ page }, testInfo) => {
  await page.routeWebSocket('**/ws', () => {});
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
  await expect(page.locator('#addOverlayBtn')).toBeFocused();
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

test('cancelled source choices do not bind a waiting overlay', async ({ page }) => {
  await page.routeWebSocket('**/ws', () => {});
  await page.goto('/', { waitUntil: 'domcontentloaded' });
  await page.getByRole('button', { name: 'Overlays', exact: true }).click();
  await page.locator('#addOverlayBtn').click();
  await page.locator('#overlayFeature').selectOption('gaze');
  await page.locator('#saveOverlayBtn').click();
  await page.evaluate(() => window.dispatchEvent(new CustomEvent('metadata-message', {
    detail: { frame_results: { layers: ['gaze A', 'gaze B'].map(model => ({
      payloadType: 'GazeMeasurements', contentType: 'eyeYawPitch', model, detections: [],
    })) } },
  })));
  await page.locator('#addOverlayBtn').click();
  await page.locator('#overlayFeature').selectOption('gaze');
  await page.locator('#overlayOutput').selectOption({ label: 'gaze B' });
  await page.locator('#cancelOverlayBtn').click();
  await page.locator('.overlay-toggle').click();
  await page.getByRole('button', { name: 'Change source…', exact: true }).click();
  await expect(page.locator('#overlayOutput')).toHaveValue('');
  await expect(page.locator('#saveOverlayBtn')).toBeDisabled();
  await page.locator('#overlayOutput').selectOption({ label: 'gaze A' });
  await page.locator('#saveOverlayBtn').click();
  expect(await page.evaluate(() => JSON.parse(localStorage.getItem('opk-overlays:v3'))
    .overlays[0].selection.primary.model)).toBe('gaze A');
});

test('continuous controls preview without rewriting saved background images', async ({ page }) => {
  await page.routeWebSocket('**/ws', () => {});
  await page.addInitScript(() => {
    window.overlayWrites = 0;
    const setItem = Storage.prototype.setItem;
    Storage.prototype.setItem = function (key, value) {
      if (key === 'opk-overlays:v3') window.overlayWrites++;
      return setItem.call(this, key, value);
    };
    const strokeRect = CanvasRenderingContext2D.prototype.strokeRect;
    CanvasRenderingContext2D.prototype.strokeRect = function (...args) {
      if (this.canvas.id === 'clientOsdCanvas') window.drawnOpacity = this.globalAlpha;
      return strokeRect.apply(this, args);
    };
  });
  await page.goto('/', { waitUntil: 'domcontentloaded' });
  await page.getByRole('button', { name: 'Overlays', exact: true }).click();
  await page.locator('#addOverlayBtn').click();
  await page.locator('#overlayFeature').selectOption('background-replacement');
  await page.locator('#saveOverlayBtn').click();
  await page.locator('.overlay-toggle').click();
  const png = await page.evaluate(() => {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 1024;
    const ctx = canvas.getContext('2d');
    const image = ctx.createImageData(1024, 1024);
    const bytes = new Uint8Array(image.data.buffer);
    for (let i = 0; i < bytes.length; i += 65536) crypto.getRandomValues(bytes.subarray(i, i + 65536));
    for (let i = 3; i < bytes.length; i += 4) bytes[i] = 255;
    ctx.putImageData(image, 0, 0);
    return canvas.toDataURL('image/png').split(',')[1];
  });
  await page.locator('.overlay-card').getByLabel('Background image', { exact: true })
    .setInputFiles({ name: 'noise.png', mimeType: 'image/png', buffer: Buffer.from(png, 'base64') });
  await expect.poll(() => page.evaluate(() => JSON.parse(localStorage.getItem('opk-overlays:v3'))
    .overlays[0].settings.image.length)).toBeGreaterThan(250000);
  await page.evaluate(() => {
    window.savedImage = JSON.parse(localStorage.getItem('opk-overlays:v3')).overlays[0].settings.image;
    const frame = { layers: [{
      payloadType: 'BoxDetections', contentType: 'genericObject', model: 'fixture',
      detections: [{ type: 'Rect', data: { x: 10, y: 10, width: 50, height: 50 } }],
    }] };
    window.dispatchEvent(new CustomEvent('video-frame-results', { detail: frame }));
  });
  await page.locator('#addOverlayBtn').click();
  await page.locator('#overlayFeature').selectOption('bounding-boxes');
  await page.locator('#saveOverlayBtn').click();
  const card = page.locator('.overlay-card').last();
  await card.locator('.overlay-toggle').click();
  const opacity = card.getByLabel('Opacity', { exact: true });
  await page.evaluate(() => { window.overlayWrites = 0; });
  await opacity.evaluate(input => {
    for (let i = 1; i <= 60; i++) {
      input.value = String(i / 100);
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
  });
  expect(await page.evaluate(() => window.overlayWrites)).toBe(0);
  await expect.poll(() => page.evaluate(() => window.drawnOpacity)).toBe(0.6);
  expect(await page.evaluate(() => JSON.parse(localStorage.getItem('opk-overlays:v3'))
    .overlays[1].settings.opacity)).toBe(1);
  await opacity.dispatchEvent('change');
  expect(await page.evaluate(() => window.overlayWrites)).toBe(1);
  expect(await page.evaluate(() => JSON.parse(localStorage.getItem('opk-overlays:v3'))
    .overlays[1].settings.opacity)).toBe(0.6);
  const color = card.getByLabel('Color', { exact: true });
  await page.evaluate(() => { window.overlayWrites = 0; });
  await color.evaluate(input => {
    for (let i = 0; i < 60; i++) {
      input.value = '#' + i.toString(16).padStart(6, '0');
      input.dispatchEvent(new Event('input', { bubbles: true }));
    }
  });
  expect(await page.evaluate(() => window.overlayWrites)).toBe(0);
  await expect(card.locator('.overlay-swatch')).toHaveCSS('background-color', 'rgb(0, 0, 59)');
  await color.dispatchEvent('change');
  expect(await page.evaluate(() => window.overlayWrites)).toBe(1);
  expect(await page.evaluate(() => JSON.parse(localStorage.getItem('opk-overlays:v3'))
    .overlays[0].settings.image === window.savedImage)).toBe(true);
});

test('hiding an empty output dock gives the full height to the video', async ({ page }) => {
  await page.routeWebSocket('**/ws', () => {});
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await page.goto('/', { waitUntil: 'domcontentloaded' });
  await page.locator('#toggleInferenceOutput').click();
  await expect(page.locator('body')).toHaveClass(/output-panels-empty/);
  await page.locator('#outputsVisibilityBtn').click();
  await expect(page.locator('body')).toHaveClass(/outputs-hidden/);
  await expect.poll(() => page.locator('.main-content').evaluate(el =>
    el.style.getPropertyValue('--bottom-dock-current-height'))).toBe('');
  const heights = await page.evaluate(() => ({
    video: document.querySelector('.video-wrapper').getBoundingClientRect().height,
    main: document.querySelector('.main-content').getBoundingClientRect().height,
  }));
  expect(heights.video).toBeCloseTo(heights.main, 1);
  await page.locator('#outputsVisibilityBtn').click();
  await expect(page.locator('.bottom-dock')).toBeVisible();
  await expect.poll(() => page.locator('.bottom-dock').evaluate(el => el.getBoundingClientRect().height)).toBe(48);
});

test.describe('semantic class maps', () => {
  test.use({ deviceScaleFactor: 2 });

  test('SDK Uint32 labels and void values paint only the declared image region', async ({page}) => {
    await page.routeWebSocket('**/ws', () => {});
    await page.goto('/', {waitUntil: 'domcontentloaded'});
    await page.evaluate(() => {
      const pixels = new Uint8Array(8);
      new DataView(pixels.buffer).setUint32(4, 0xf2345678, true);
      const frame = {layers: [{payloadType: 'SemanticSegmentationMasks', model: 'wide-crop', contentType: 'custom', detections: [
        {type: 'SemanticSegmentationMask', data: {width: 2, height: 1, encoding: 2, pixels, labelSet: 'test:wide', voidValue: 0,
          space: {frameId: 42n, width: 6, height: 4, originX: 1, originY: 1, extentWidth: 3, extentHeight: 2}}},
      ]}]};
      Object.defineProperties(document.getElementById('video'), {videoWidth: {configurable: true, value: 6}, videoHeight: {configurable: true, value: 4}});
      Object.defineProperties(document.getElementById('clientOsdCanvas'), {clientWidth: {configurable: true, value: 600}, clientHeight: {configurable: true, value: 600}});
      window.dispatchEvent(new CustomEvent('metadata-message', {detail: {frame_results: frame}}));
      window.dispatchEvent(new CustomEvent('video-frame-results', {detail: frame}));
    });
    await page.getByRole('button', {name: 'Overlays', exact: true}).click();
    await page.locator('#addOverlayBtn').click();
    await page.locator('#overlayFeature').selectOption('semantic-segmentation');
    await page.locator('#saveOverlayBtn').click();
    const card = page.locator('.overlay-card');
    await card.locator('.overlay-toggle').click();
    await card.getByLabel('Opacity', {exact: true}).fill('1');
    await card.getByLabel('Opacity', {exact: true}).dispatchEvent('change');
    const color = card.getByLabel('Class 4063516280 (4063516280) color', {exact: true});
    await color.fill('#00ff00');
    await color.dispatchEvent('change');
    await expect(card.locator('.overlay-class-row')).toHaveCount(1);
    await expect.poll(() => page.locator('#clientOsdCanvas').evaluate(canvas =>
      [[300, 500], [600, 500], [1000, 500], [600, 40]].map(([x, y]) => [...canvas.getContext('2d').getImageData(x, y, 1, 1).data])))
      .toEqual([[0, 0, 0, 0], [0, 255, 0, 255], [0, 0, 0, 0], [0, 0, 0, 0]]);
  });

  test('palette pixels, legend and saved opacity follow only the matched source', async ({ page }) => {
    await page.routeWebSocket('**/ws', () => {});
    await page.goto('/', { waitUntil: 'domcontentloaded' });
    const emit = (type = 'video-frame-results', model = 'semantic', pixels = [0, 13, 18]) => page.evaluate(({ type, model, pixels }) => {
      const frame = { layers: [
        { payloadType: 'FrameContext', detections: [{ type: 'VideoFrame', data: {
          uuid: '42', originalWidth: 3, originalHeight: 1,
        } }] },
        { payloadType: 'SemanticSegmentationMasks', contentType: 'semanticSegmentation',
          labelFamily: 'example-classes', model, detections: [{ type: 'SemanticSegmentationMask', data: {
            parentUuid: 'unrelated', width: 3, height: 1, encoding: 1, pixels: Uint8Array.from(pixels), labelSet: 'test:classes',
            space: {frameId: 42n, width: 3, height: 1, originX: 0, originY: 0, extentWidth: 3, extentHeight: 1},
          } }] },
      ] };
      // Square display, wide source, and DPR 2 make incorrect containment/scaling visible.
      Object.defineProperties(document.getElementById('video'), {
        videoWidth: { configurable: true, value: 3 }, videoHeight: { configurable: true, value: 1 },
      });
      Object.defineProperties(document.getElementById('clientOsdCanvas'), {
        clientWidth: { configurable: true, value: 600 }, clientHeight: { configurable: true, value: 600 },
      });
      window.dispatchEvent(new CustomEvent(type, { detail: type === 'metadata-message' ? { frame_results: frame } : frame }));
    }, { type, model, pixels });
    const sample = () => page.locator('#clientOsdCanvas').evaluate(canvas => {
      const ctx = canvas.getContext('2d');
      return [[200, 600], [600, 600], [1000, 600], [600, 100]].map(([x, y]) =>
        [...ctx.getImageData(x, y, 1, 1).data]);
    });
    const clear = [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]];
    await emit('metadata-message');
    await page.getByRole('button', { name: 'Overlays', exact: true }).click();
    await page.locator('#addOverlayBtn').click();
    await page.locator('#overlayFeature').selectOption('semantic-segmentation');
    await page.locator('#saveOverlayBtn').click();
    const card = page.locator('.overlay-card');
    await card.locator('.overlay-toggle').click();
    const legend = card.getByRole('region', { name: 'Class legend' });
    await expect(legend).toContainText('Waiting for matching video metadata');
    await expect.poll(sample).toEqual(clear);
    const opacity = card.getByLabel('Opacity', { exact: true });
    await opacity.fill('1');
    await opacity.dispatchEvent('change');
    await emit();
    const colors = [[64, 64, 64, 255], [184, 88, 64, 255], [112, 112, 64, 255], [0, 0, 0, 0]];
    await expect.poll(sample).toEqual(colors);
    await expect(legend.locator('.overlay-class-row')).toHaveCount(3);
    await expect(legend.locator('.overlay-class-name').first()).toHaveAttribute('placeholder', 'Class 0');
    await expect(legend.locator('.overlay-class-name[aria-description="In frame"]')).toHaveCount(3);
    // Nearest-neighbor sampling leaves the class boundary sharp, not interpolated.
    expect(await page.locator('#clientOsdCanvas').evaluate(canvas =>
      [...canvas.getContext('2d').getImageData(399, 600, 2, 1).data]))
      .toEqual([64, 64, 64, 255, 184, 88, 64, 255]);
    await emit('metadata-message', 'semantic', [1, 1, 1]);
    await expect.poll(sample).toEqual(colors);
    await expect(card.getByRole('textbox', {name: 'Class 0 name', exact: true})).toBeVisible();
    await opacity.fill('0.25');
    await opacity.dispatchEvent('change');
    await expect.poll(async () => (await sample()).map(pixel => pixel[3])).toEqual([64, 64, 64, 0]);
    const classColor = card.getByLabel('Class 0 (0) color', {exact: true});
    await classColor.evaluate(input => {
      input.value = '#00ff00';
      input.dispatchEvent(new Event('input', {bubbles: true}));
      input.dispatchEvent(new Event('change', {bubbles: true}));
    });
    const classOpacity = card.getByLabel('Class 0 (0) opacity', {exact: true});
    await classOpacity.fill('0.5');
    await classOpacity.dispatchEvent('change');
    await expect.poll(async () => (await sample())[0]).toEqual([0, 255, 0, 32]);
    const classVisible = card.getByRole('switch', {name: 'Class 13 (13) visible', exact: true});
    await classVisible.focus();
    await page.keyboard.press('Space');
    await expect.poll(async () => (await sample())[1][3]).toBe(0);
    // Incoming frames preserve the actual control node/focus during adjustments.
    await classOpacity.focus();
    await emit();
    await expect(classOpacity).toBeFocused();
    await expect.poll(async () => (await sample())[0][3]).toBe(32);
    const className = card.getByRole('textbox', {name: 'Class 0 name', exact: true});
    await className.fill('person');
    await emit();
    await expect(className).toBeFocused();
    await expect(className).toHaveValue('person');
    await className.press('Enter');
    await emit('video-frame-results', 'other-source');
    await expect.poll(sample).toEqual(clear);
    await expect(legend).toContainText('Waiting for source');
    await emit('video-frame-results', 'semantic', [0]);
    await expect.poll(sample).toEqual(clear);
    await expect(legend).toContainText('Invalid or oversized');
    await emit();
    const overlayVisible = card.getByRole('switch', {name: /^Show Semantic segmentation/});
    await overlayVisible.focus();
    await page.keyboard.press('Space');
    await expect.poll(sample).toEqual(clear);
    await expect(legend).toContainText('Overlay hidden');
    await expect(legend.locator('.overlay-class-name[aria-description="In frame"]')).toHaveCount(3);
    await emit('video-frame-results', 'semantic', [18, 18, 18]);
    await expect.poll(sample).toEqual(clear);
    await expect(legend.locator('.overlay-class-name[aria-description="In frame"]')).toHaveCount(1);
    expect(await className.evaluate(input => input.parentElement.title)).toContain('Not in frame');
    await page.reload({ waitUntil: 'domcontentloaded' });
    await page.getByRole('button', { name: 'Overlays', exact: true }).click();
    await card.locator('.overlay-toggle').click();
    await expect(opacity).toHaveValue('0.25');
    await expect(classColor).toHaveValue('#00ff00');
    await expect(classOpacity).toHaveValue('0.5');
    await expect(classVisible).not.toBeChecked();
    await expect(className).toHaveValue('person');
    await expect(overlayVisible).not.toBeChecked();
    await overlayVisible.focus();
    await page.keyboard.press('Space');
    await emit();
    await expect.poll(async () => (await sample())[0][3]).toBe(32);
    await page.evaluate(() => window.dispatchEvent(new CustomEvent('video-frame-results', { detail: null })));
    await expect.poll(sample).toEqual(clear);
    await expect(legend).toContainText('Waiting for matching video metadata');
    expect(await className.evaluate(input => input.parentElement.title)).toContain('Unavailable');
    await className.fill('');
    await className.press('Tab');
    expect(await page.evaluate(() => JSON.parse(localStorage.getItem('opk-overlays:v3'))
      .overlays[0].settings.classes[0].label)).toBeUndefined();
    await expect(className).toHaveAttribute('placeholder', 'Class 0');
    await className.fill('person');
    await className.press('Enter');
    const otherName = card.getByRole('textbox', {name: 'Class 13 name', exact: true});
    await otherName.fill('vehicle');
    await otherName.press('Enter');
    await card.getByRole('button', {name: 'Reset labels', exact: true}).click();
    await expect(className).toHaveValue('');
    await expect(otherName).toHaveValue('');
    expect(await page.evaluate(() => JSON.parse(localStorage.getItem('opk-overlays:v3'))
      .overlays[0].settings.classes)).toEqual({
        0: {enabled: true, color: '#00ff00', opacity: 0.5},
        13: {enabled: false, color: '#b85840', opacity: 1},
      });
    await card.locator('.overlay-remove').click();
    await emit();
    await expect.poll(sample).toEqual(clear);
  });
});

test.describe('instance masks', () => {
  test.use({ deviceScaleFactor: 2 });

  test('fine crop masks retain visible cells across paused-frame resize and DPR changes', async ({page}) => {
    await page.routeWebSocket('**/ws', () => {});
    await page.goto('/', {waitUntil: 'domcontentloaded'});
    await page.evaluate(() => {
      const pixels = new Uint8Array(16);
      pixels[0] = 1;
      const frame = {layers: [{payloadType: 'InstanceSegmentationMasks', model: 'fine-crop', detections: [{
        type: 'InstanceSegmentationMask', data: {width: 4, height: 4, encoding: 1, pixels,
          labelSet: 'test:classes', classId: 0, confidence: null, box: null,
          space: {frameId: 42n, width: 4, height: 4, originX: 1, originY: 1, extentWidth: 1, extentHeight: 1}},
      }]}]};
      Object.defineProperties(document.getElementById('video'), {videoWidth: {configurable: true, value: 4}, videoHeight: {configurable: true, value: 4}});
      Object.defineProperties(document.getElementById('clientOsdCanvas'), {clientWidth: {configurable: true, value: 400}, clientHeight: {configurable: true, value: 400}});
      window.dispatchEvent(new CustomEvent('metadata-message', {detail: {frame_results: frame}}));
      window.dispatchEvent(new CustomEvent('video-frame-results', {detail: frame}));
    });
    await page.getByRole('button', {name: 'Overlays', exact: true}).click();
    await page.locator('#addOverlayBtn').click();
    await page.locator('#overlayFeature').selectOption('instance-segmentation');
    await page.locator('#saveOverlayBtn').click();
    for (const [size, dpr] of [[400, 2], [800, 2], [400, 1], [400, 2]]) {
      await page.evaluate(({size, dpr}) => {
        Object.defineProperty(window, 'devicePixelRatio', {configurable: true, value: dpr});
        Object.defineProperties(document.getElementById('clientOsdCanvas'), {
          clientWidth: {configurable: true, value: size}, clientHeight: {configurable: true, value: size},
        });
      }, {size, dpr});
      // The first grid cell covers one quarter of a source pixel, enlarged by the display.
      await expect.poll(() => page.locator('#clientOsdCanvas').evaluate((canvas, {size, dpr}) => {
        const context = canvas.getContext('2d');
        return [.28, .4].map(fraction => {
          const point = Math.round(size * dpr * fraction);
          return context.getImageData(point, point, 1, 1).data[3];
        });
      }, {size, dpr})).toEqual([128, 0]);
    }
  });

  test('different instance grids and crop regions replace overlap alpha without boxes', async ({page}) => {
    await page.routeWebSocket('**/ws', () => {});
    await page.goto('/', {waitUntil: 'domcontentloaded'});
    await page.evaluate(() => {
      const space = {frameId: 42n, width: 6, height: 4, originX: 0, originY: 0, extentWidth: 6, extentHeight: 4};
      const frame = {layers: [{payloadType: 'InstanceSegmentationMasks', model: 'crop-instances', contentType: 'custom', detections: [
        {type: 'InstanceSegmentationMask', data: {width: 3, height: 3, encoding: 1, pixels: new Uint8Array(9).fill(1), space, classId: 0, labelSet: 'test:classes', confidence: null, box: null}},
        {type: 'InstanceSegmentationMask', data: {width: 1, height: 1, encoding: 1, pixels: new Uint8Array([1]),
          space: {...space, originX: 2, originY: 1, extentWidth: 2, extentHeight: 2}, classId: 1, labelSet: 'test:classes', confidence: null, box: null}},
      ]}]};
      Object.defineProperties(document.getElementById('video'), {videoWidth: {configurable: true, value: 6}, videoHeight: {configurable: true, value: 4}});
      Object.defineProperties(document.getElementById('clientOsdCanvas'), {clientWidth: {configurable: true, value: 600}, clientHeight: {configurable: true, value: 600}});
      window.dispatchEvent(new CustomEvent('metadata-message', {detail: {frame_results: frame}}));
      window.dispatchEvent(new CustomEvent('video-frame-results', {detail: frame}));
    });
    await page.getByRole('button', {name: 'Overlays', exact: true}).click();
    await page.locator('#addOverlayBtn').click();
    await page.locator('#overlayFeature').selectOption('instance-segmentation');
    await page.locator('#saveOverlayBtn').click();
    const card = page.locator('.overlay-card');
    await card.locator('.overlay-toggle').click();
    await card.getByLabel('Opacity', {exact: true}).fill('1');
    await card.getByLabel('Opacity', {exact: true}).dispatchEvent('change');
    for (const id of [0, 1]) {
      const opacity = card.getByLabel(`Class ${id} (${id}) opacity`, {exact: true});
      await opacity.fill('0.5');
      await opacity.dispatchEvent('change');
    }
    await expect.poll(() => page.locator('#clientOsdCanvas').evaluate(canvas =>
      [[600, 600], [100, 300], [600, 40]].map(([x, y]) => canvas.getContext('2d').getImageData(x, y, 1, 1).data[3])))
      .toEqual([128, 128, 0]);
  });

  for (const order of [['instance-segmentation', 'semantic-segmentation'], ['semantic-segmentation', 'instance-segmentation']]) {
    test(`instance annotations remain above opaque masks: ${order.join(', ')}`, async ({ page }) => {
      await page.routeWebSocket('**/ws', () => {});
      await page.goto('/', {waitUntil: 'domcontentloaded'});
      await page.getByRole('button', {name: 'Overlays', exact: true}).click();
      for (const feature of order) {
        await page.locator('#addOverlayBtn').click();
        await page.locator('#overlayFeature').selectOption(feature);
        await page.locator('#saveOverlayBtn').click();
        const card = page.locator('.overlay-card').last();
        await card.locator('.overlay-toggle').click();
        await card.getByLabel('Opacity', {exact: true}).fill('1');
        await card.getByLabel('Opacity', {exact: true}).dispatchEvent('change');
      }
      await page.evaluate(() => {
        const pixels = new Uint8Array(35);
        for (let y = 1; y < 4; y++) pixels.fill(1, y * 7 + 1, y * 7 + 5);
        const space = {frameId: 42n, width: 7, height: 5, originX: 0, originY: 0, extentWidth: 7, extentHeight: 5};
        const bitmap = {parentUuid: 'unrelated', width: 7, height: 5, encoding: 1, labelSet: 'test:classes', space};
        Object.defineProperties(document.getElementById('video'), {
          videoWidth: {configurable: true, value: 7}, videoHeight: {configurable: true, value: 5},
        });
        Object.defineProperties(document.getElementById('clientOsdCanvas'), {
          clientWidth: {configurable: true, value: 700}, clientHeight: {configurable: true, value: 700},
        });
        window.dispatchEvent(new CustomEvent('video-frame-results', {detail: {layers: [
          {payloadType: 'FrameContext', detections: [{type: 'VideoFrame', data: {uuid: '42', originalWidth: 7, originalHeight: 5}}]},
          {payloadType: 'InstanceSegmentationMasks', contentType: 'instanceSegmentation', detections: [
            {type: 'InstanceSegmentationMask', data: {...bitmap, pixels, classId: 0, confidence: 1, box: {x: 1, y: 1, width: 4, height: 3, space, units: 0}}},
          ]},
          {payloadType: 'SemanticSegmentationMasks', contentType: 'semanticSegmentation', detections: [
            {type: 'SemanticSegmentationMask', data: {...bitmap, pixels: new Uint8Array(35)}},
          ]},
        ]}}));
      });
      await expect.poll(() => page.locator('#clientOsdCanvas').evaluate(canvas =>
        [...canvas.getContext('2d').getImageData(200, 700, 1, 1).data])).toEqual([255, 255, 255, 255]);
    });
  }

  test('separate binary objects share class controls without losing frame matching', async ({ page }) => {
    await page.routeWebSocket('**/ws', () => {});
    await page.addInitScript(() => {
      window.instanceLabels = new Set();
      const fillText = CanvasRenderingContext2D.prototype.fillText;
      CanvasRenderingContext2D.prototype.fillText = function (text, ...args) {
        if (this.canvas.id === 'clientOsdCanvas') window.instanceLabels.add(text);
        return fillText.call(this, text, ...args);
      };
    });
    await page.goto('/', {waitUntil: 'domcontentloaded'});
    const emit = (options = {}) => page.evaluate(({model = 'instance', classId = 0, byte = 1, empty = false, live = false}) => {
      const mask = (x, width, id) => {
        const pixels = new Uint8Array(35);
        for (let y = 1; y < 4; y++) pixels.fill(byte, y * 7 + x, y * 7 + x + width);
        const space = {frameId: 42n, width: 7, height: 5, originX: 0, originY: 0, extentWidth: 7, extentHeight: 5};
        return {type: 'InstanceSegmentationMask', data: {width: 7, height: 5, encoding: 1, labelSet: 'test:classes', space,
          pixels, parentUuid: 'unrelated', classId: id, confidence: 0.75, box: {x, y: 1, width, height: 3, space, units: 0}}};
      };
      const frame = {layers: [
        {payloadType: 'FrameContext', detections: [{type: 'VideoFrame', data: {uuid: '42', originalWidth: 7, originalHeight: 5}}]},
        {payloadType: 'InstanceSegmentationMasks', contentType: 'instanceSegmentation', model,
          detections: empty ? [] : [mask(1, 4, 0), mask(3, 3, classId)]},
      ]};
      Object.defineProperties(document.getElementById('video'), {
        videoWidth: {configurable: true, value: 7}, videoHeight: {configurable: true, value: 5},
      });
      Object.defineProperties(document.getElementById('clientOsdCanvas'), {
        clientWidth: {configurable: true, value: 700}, clientHeight: {configurable: true, value: 700},
      });
      window.dispatchEvent(new CustomEvent(live ? 'metadata-message' : 'video-frame-results', {
        detail: live ? {frame_results: frame} : frame,
      }));
    }, options);
    const sample = () => page.locator('#clientOsdCanvas').evaluate(canvas =>
      [[500, 700], [700, 700], [900, 700], [100, 100]].map(([x, y]) => [...canvas.getContext('2d').getImageData(x, y, 1, 1).data]));
    const clear = Array.from({length: 4}, () => [0, 0, 0, 0]);
    await emit({live: true});
    await page.getByRole('button', {name: 'Overlays', exact: true}).click();
    await page.locator('#addOverlayBtn').click();
    await page.locator('#overlayFeature').selectOption('instance-segmentation');
    await page.locator('#saveOverlayBtn').click();
    const card = page.locator('.overlay-card');
    await card.locator('.overlay-toggle').click();
    const boxes = card.getByLabel('Boxes and labels', {exact: true});
    await expect(boxes).toBeChecked();
    await boxes.uncheck();
    await card.getByLabel('Opacity', {exact: true}).fill('1');
    await card.getByLabel('Opacity', {exact: true}).dispatchEvent('change');
    await expect.poll(sample).toEqual(clear);
    await emit();
    const colors = [[64, 64, 64, 255], [255, 255, 255, 255], [64, 64, 64, 255], [0, 0, 0, 0]];
    await expect.poll(sample).toEqual(colors);
    await expect(card.locator('.overlay-class-row')).toHaveCount(1);
    await emit({empty: true, live: true});
    await expect.poll(sample).toEqual(colors);
    await boxes.check();
    await expect.poll(() => page.evaluate(() => [...window.instanceLabels])).toContain('#2 · Class 0 · 75%');
    const name = card.getByRole('textbox', {name: 'Class 0 name', exact: true});
    await name.fill('person');
    await emit();
    await expect(name).toBeFocused();
    await name.press('Enter');
    await expect.poll(() => page.evaluate(() => [...window.instanceLabels])).toContain('#1 · person · 75%');
    await boxes.uncheck();
    await card.getByLabel('Class 0 (0) opacity', {exact: true}).fill('0.5');
    await card.getByLabel('Class 0 (0) opacity', {exact: true}).dispatchEvent('change');
    await card.getByLabel('Opacity', {exact: true}).fill('0.5');
    await card.getByLabel('Opacity', {exact: true}).dispatchEvent('change');
    await expect.poll(async () => (await sample()).map(pixel => pixel[3])).toEqual([64, 64, 64, 0]);
    await emit({classId: 500});
    const visible = card.getByRole('switch', {name: 'Class 0 (0) visible', exact: true});
    await visible.focus();
    await page.keyboard.press('Space');
    await expect.poll(async () => (await sample())[0][3]).toBe(0);
    await expect.poll(async () => (await sample())[2][3]).toBe(128);
    const overlayVisible = card.getByRole('switch', {name: /^Show Instance segmentation/});
    await overlayVisible.focus();
    await page.keyboard.press('Space');
    await expect.poll(sample).toEqual(clear);
    await expect(card.locator('.overlay-class-name[aria-description="In frame"]')).toHaveCount(2);
    await overlayVisible.focus();
    await page.keyboard.press('Space');
    await expect.poll(async () => (await sample())[2][3]).toBe(128);
    await emit({classId: 500, model: 'other'});
    await expect.poll(sample).toEqual(clear);
    await emit({byte: 255});
    await expect(card).toContainText('Invalid or oversized mask');
    await expect.poll(sample).toEqual(clear);
    await emit({empty: true});
    await expect.poll(sample).toEqual(clear);
    await page.reload({waitUntil: 'domcontentloaded'});
    await page.getByRole('button', {name: 'Overlays', exact: true}).click();
    await card.locator('.overlay-toggle').click();
    await expect(name).toHaveValue('person');
    await expect(boxes).not.toBeChecked();
    await expect(visible).not.toBeChecked();
    await emit({classId: 500});
    await expect.poll(async () => (await sample())[2][3]).toBe(128);
    await page.evaluate(() => window.dispatchEvent(new CustomEvent('video-frame-results', {detail: null})));
    await expect.poll(sample).toEqual(clear);
    await expect(card).toContainText('Waiting for matching video metadata');
    await card.locator('.overlay-remove').click();
    await emit();
    await expect.poll(sample).toEqual(clear);
  });
});

test.describe('OCR overlays', () => {
  test.use({deviceScaleFactor: 2});

  test('explicit sources, subject-linked Unicode text, geometry and saved OCR controls', async ({page}) => {
    await page.routeWebSocket('**/ws', () => {});
    await page.addInitScript(() => {
      window.ocrText = [];
      const fillText = CanvasRenderingContext2D.prototype.fillText;
      const clearRect = CanvasRenderingContext2D.prototype.clearRect;
      CanvasRenderingContext2D.prototype.clearRect = function (...args) {
        if (this.canvas.id === 'clientOsdCanvas') window.ocrText = [];
        return clearRect.apply(this, args);
      };
      CanvasRenderingContext2D.prototype.fillText = function (text, x, y, ...args) {
        if (this.canvas.id === 'clientOsdCanvas') window.ocrText.push({text, x, y, font: this.font});
        return fillText.call(this, text, x, y, ...args);
      };
    });
    await page.goto('/', {waitUntil: 'domcontentloaded'});
    const emit = (options = {}) => page.evaluate(({live = false, detectorOnly = false, missing = false,
      duplicate = false, text = 'PLATE 1234', score = 0.9, model = 'reader'} = {}) => {
      const detector = {payloadType: 'BoxDetections', model: 'detector', contentType: '', detections: [{type: 'Rect', data: {
        uuid: '200', parentUuid: '42', x: 80, y: 0, width: 160, height: 40, confidence: 0.8,
        spatialValid: true, frameWidth: 640, frameHeight: 480,
      }}]};
      const reader = {payloadType: 'Classifications', model, contentType: '', detections: [{type: 'Classification', data: {
        uuid: '300', parentUuid: 'unrelated', subjectUuid: '200', kind: 2, labelSet: '', candidates: [{text, confidence: score}],
      }}]};
      const frame = {layers: [
        {payloadType: 'FrameContext', detections: [{type: 'VideoFrame', data: {uuid: '42', originalWidth: 640, originalHeight: 480}}]},
        ...(!missing ? [detector] : []), ...(duplicate ? [detector] : []), ...(!detectorOnly ? [reader] : []),
      ]};
      Object.defineProperties(document.getElementById('video'), {
        videoWidth: {configurable: true, value: 640}, videoHeight: {configurable: true, value: 480},
      });
      Object.defineProperties(document.getElementById('clientOsdCanvas'), {
        clientWidth: {configurable: true, value: 800}, clientHeight: {configurable: true, value: 800},
      });
      window.dispatchEvent(new CustomEvent(live ? 'metadata-message' : 'video-frame-results', {
        detail: live ? {frame_results: frame} : frame,
      }));
    }, options);
    const texts = () => page.evaluate(() => window.ocrText.map(item => item.text));
    const samples = () => page.locator('#clientOsdCanvas').evaluate(canvas => {
      const context = canvas.getContext('2d');
      return [[200, 230], [400, 250], [202, 308], [200, 50]].map(([x, y]) => [...context.getImageData(x, y, 1, 1).data]);
    });
    const clear = Array.from({length: 4}, () => [0, 0, 0, 0]);
    await emit({live: true, detectorOnly: true});
    await page.getByRole('button', {name: 'Overlays', exact: true}).click();
    await page.locator('#addOverlayBtn').click();
    await page.locator('#overlayFeature').selectOption('ocr');
    await expect(page.locator('#overlayOutput')).toBeVisible();
    await expect(page.locator('#overlayOutput')).toHaveValue('');
    await expect(page.locator('#saveOverlayBtn')).toBeDisabled();
    await emit({live: true});
    await page.locator('#overlayOutput').selectOption({label: 'reader'});
    await page.locator('#saveOverlayBtn').click();
    const card = page.locator('.overlay-card');
    await card.locator('.overlay-toggle').click();
    await expect.poll(texts).toEqual([]);
    await expect.poll(samples).toEqual(clear);
    await emit();
    await expect.poll(texts).toEqual(['PLATE 1234']);
    await expect.poll(samples).toEqual([[0, 255, 255, 255], [0, 0, 0, 0], [0, 0, 0, 199], [0, 0, 0, 0]]);
    await emit({live: true, text: 'newer, unmatched'});
    await expect.poll(texts).toEqual(['PLATE 1234']);
    await card.getByLabel('Regions', {exact: true}).uncheck();
    await expect.poll(async () => (await samples())[0][3]).toBe(0);
    await expect.poll(texts).toEqual(['PLATE 1234']);
    await card.getByLabel('Recognized text', {exact: true}).uncheck();
    await expect.poll(texts).toEqual([]);
    await card.getByLabel('Text confidence', {exact: true}).check();
    await expect.poll(texts).toEqual(['Text: 90.0%']);
    await card.getByLabel('Region confidence', {exact: true}).check();
    await expect.poll(texts).toEqual(['Region: 80.0% · Text: 90.0%']);
    await emit({text: '', score: 0});
    await expect.poll(texts).toEqual(['Region: 80.0% · Text: 0.0%']);
    await card.getByLabel('Region confidence', {exact: true}).uncheck();
    await expect.poll(texts).toEqual(['Text: 0.0%']);
    await card.getByLabel('Text confidence', {exact: true}).uncheck();
    await card.getByLabel('Recognized text', {exact: true}).check();
    await card.getByLabel('Regions', {exact: true}).check();
    await card.getByLabel('Text size', {exact: true}).selectOption('24');
    await emit({text: '東京 <img src=x onerror="window.injected=true"> ' + '文'.repeat(1000)});
    await expect.poll(() => page.evaluate(() => window.ocrText[0]?.font)).toBe('24px monospace');
    await expect.poll(() => page.evaluate(() => window.ocrText[0]?.text.endsWith('…'))).toBe(true);
    expect(await page.evaluate(() => window.injected)).toBeUndefined();
    await expect(page.locator('img[src="x"]')).toHaveCount(0);
    expect(await page.evaluate(() => window.ocrText.every(item => item.x >= 0 && item.x < 800 && item.y >= 100 && item.y < 700))).toBe(true);
    await emit({missing: true});
    await expect(card).toContainText('Waiting for subject boxes');
    await expect.poll(texts).toEqual([]);
    await expect.poll(samples).toEqual(clear);
    await emit({duplicate: true});
    await expect(card).toContainText('Ambiguous');
    await expect.poll(texts).toEqual([]);
    await emit({model: 'other reader'});
    await expect(card).toContainText('Waiting for source');
    await expect.poll(samples).toEqual(clear);
    await card.getByLabel('Region confidence', {exact: true}).check();
    await page.reload({waitUntil: 'domcontentloaded'});
    await page.getByRole('button', {name: 'Overlays', exact: true}).click();
    await card.locator('.overlay-toggle').click();
    await expect(card.getByLabel('Text size', {exact: true})).toHaveValue('24');
    await expect(card.getByLabel('Region confidence', {exact: true})).toBeChecked();
    await expect(card.getByLabel('Text confidence', {exact: true})).not.toBeChecked();
    await emit();
    await expect.poll(texts).toEqual(['PLATE 1234 · Region: 80.0%']);
    await page.evaluate(() => window.dispatchEvent(new CustomEvent('video-frame-results', {detail: null})));
    await expect.poll(texts).toEqual([]);
    await expect.poll(samples).toEqual(clear);
    await card.getByRole('button', {name: 'Change source…', exact: true}).click();
    await page.locator('#overlayOutput').selectOption({label: 'detector'});
    await page.locator('#saveOverlayBtn').click();
    await emit();
    await expect.poll(texts).toEqual([]);
    await card.getByLabel('Region confidence', {exact: true}).check();
    await card.getByLabel('Text confidence', {exact: true}).check();
    await expect.poll(texts).toEqual(['Region: 80.0%']);
    await card.locator('.overlay-remove').click();
    await expect.poll(samples).toEqual(clear);
  });
});

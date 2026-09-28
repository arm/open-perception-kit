/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
  const video = page.locator('#video');
  const objects = page.locator('#inferenceOutputBody .inference-layer').filter({
    has: page.locator('.inference-layer-kind', { hasText: /^genericObject$/ }),
  });

  await page.goto('/');

  await expect.poll(() => video.evaluate((element) =>
    element.readyState >= 2 && element.videoWidth > 0 && element.currentTime > 0
  )).toBe(true);
  await expect(objects.locator('.inference-detection').first()).toBeVisible();
});

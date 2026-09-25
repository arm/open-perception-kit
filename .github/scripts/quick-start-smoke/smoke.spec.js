const { expect, test } = require('@playwright/test');

test('signaling diagnostics render as text', async ({ page }) => {
  const messages = ['camera ready', '<em>camera & light</em>'];
  await page.routeWebSocket('**/ws', (ws) => {
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

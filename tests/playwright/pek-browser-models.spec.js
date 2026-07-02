const { expect, test } = require('@playwright/test');

const { openPekUi, setModel, waitForVideo } = require('./pek-browser-helpers');

test('PEK browser UI toggles ONNX models', async ({ page }) => {
  await openPekUi(page);

  const modelItems = page.locator('#models-container .model-item');
  await expect(modelItems.first()).toBeVisible({ timeout: 90000 });

  const modelNames = await page.locator('#models-container .model-name').allTextContents();
  expect(modelNames.length).toBeGreaterThan(0);

  for (const name of modelNames) {
    await setModel(page, name, false);
  }

  await page.waitForTimeout(3000);
  await waitForVideo(page);

  for (const name of modelNames) {
    await setModel(page, name, true);
    await page.waitForTimeout(2000);
    await setModel(page, name, false);
  }
});

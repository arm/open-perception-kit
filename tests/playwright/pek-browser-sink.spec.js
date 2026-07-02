const { expect, test } = require('@playwright/test');

const { openPekUi, waitForVideo } = require('./pek-browser-helpers');

test('PEK browser UI receives sink-only local data', async ({ page }) => {
  await openPekUi(page);

  await expect(page.locator('#models-container')).toContainText('No models registered yet', {
    timeout: 30000,
  });

  await waitForVideo(page);
});

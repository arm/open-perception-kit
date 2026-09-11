const { expect, test } = require('./pek-browser-coverage');

const { expectSinkOnlyData, openPekUi } = require('./pek-browser-helpers');

test('PEK browser UI receives sink-only local data', async ({ page }) => {
  await openPekUi(page);
  await expectSinkOnlyData(page);
  await expect(page.locator('#log .log-line.error')).toHaveCount(0);
});

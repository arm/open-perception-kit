const { test } = require('@playwright/test');

const {
  exerciseModelsOneAtATime,
  holdAllModelsOff,
  openPekUi,
  registeredModelNames,
  waitForVideo,
} = require('./pek-browser-helpers');

test('PEK browser UI toggles ONNX models', async ({ page }) => {
  await openPekUi(page);

  const modelNames = await registeredModelNames(page);
  await waitForVideo(page);
  await holdAllModelsOff(page, modelNames);
  await exerciseModelsOneAtATime(page, modelNames);
});

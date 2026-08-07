const { test } = require('@playwright/test');

const {
  exerciseModelsOneAtATime,
  holdAllModelsOff,
  expectVideoKeepsPlaying,
  openPekUi,
  registeredModelNames,
  waitForVideo,
} = require('./pek-browser-helpers');

test('PEK browser UI keeps displaying decoded video', async ({ page }) => {
  await openPekUi(page);
  await waitForVideo(page);
  await expectVideoKeepsPlaying(page);
});

test('PEK browser UI toggles ONNX models', async ({ page }) => {
  await openPekUi(page);

  const modelNames = await registeredModelNames(page);
  await waitForVideo(page);
  await holdAllModelsOff(page, modelNames);
  await exerciseModelsOneAtATime(page, modelNames);
});

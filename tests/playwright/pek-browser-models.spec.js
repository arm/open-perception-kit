const { test } = require('@playwright/test');

const {
  exerciseModelsOneAtATime,
  holdAllModelsOff,
  expectModelLabelsDoNotOverflow,
  expectVideoKeepsPlaying,
  openPekUi,
  registeredModels,
  waitForVideo,
} = require('./pek-browser-helpers');

test('PEK browser UI keeps displaying decoded video', async ({ page }) => {
  await openPekUi(page);
  await waitForVideo(page);
  await expectVideoKeepsPlaying(page);
});

test('PEK browser UI toggles ONNX models', async ({ page }) => {
  await openPekUi(page);

  const models = await registeredModels(page);
  await expectModelLabelsDoNotOverflow(page);
  await waitForVideo(page);
  await holdAllModelsOff(page, models);
  await exerciseModelsOneAtATime(page, models);
});

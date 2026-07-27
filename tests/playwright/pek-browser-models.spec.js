const { expect, test } = require('@playwright/test');

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

test('PEK browser UI toggles the ONNX YOLO model', async ({ page }) => {
  await openPekUi(page);

  const modelNames = await registeredModelNames(page);
  expect(modelNames).toEqual(['YoloV11']);
  await waitForVideo(page);
  await holdAllModelsOff(page, modelNames);
  await exerciseModelsOneAtATime(page, modelNames);
});

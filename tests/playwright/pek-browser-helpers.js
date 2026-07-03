const { expect } = require('@playwright/test');

const MODEL_OUTPUT_VISIBLE_MS = 4000;
const MODELS_OFF_VISIBLE_MS = 3000;
const MODEL_ITEM = '.model-item';
const MODEL_NAME = '.model-name';
const MODELS_CONTAINER = '#models-container';
const NO_MODELS_TEXT = 'No models registered yet';
const STATUS_LINE = '#status-line';

async function openPekUi(page) {
  await expect.poll(async () => {
    try {
      const response = await page.request.get('/pek-config.js');
      return response.ok();
    } catch {
      return false;
    }
  }, { timeout: 60000 }).toBe(true);

  await page.goto('/', { waitUntil: 'domcontentloaded', timeout: 60000 });

  await expect(page.locator(STATUS_LINE)).toBeVisible();
  await expect(page.locator(MODELS_CONTAINER)).toBeVisible();

  const config = await page.evaluate(() => window.PEK_CONFIG);
  expect(config).toEqual(expect.objectContaining({
    wsPort: expect.any(Number),
    ctrlPort: expect.any(Number),
  }));
}

async function waitForVideo(page) {
  await expect(page.locator(STATUS_LINE)).toContainText(/connected|video/i, {
    timeout: 60000,
  });

  await page.waitForFunction(videoTrackIsLive, undefined, { timeout: 60000 });

  await page.waitForTimeout(1000);
}

async function registeredModelNames(page) {
  const modelItems = page.locator(`${MODELS_CONTAINER} ${MODEL_ITEM}`);
  await expect(modelItems.first()).toBeVisible({ timeout: 90000 });

  const names = (await page.locator(`${MODELS_CONTAINER} ${MODEL_NAME}`).allTextContents())
    .map((name) => name.trim())
    .filter(Boolean);
  expect(names.length).toBeGreaterThan(0);
  return names;
}

async function expectSinkOnlyData(page) {
  await expect(page.locator(MODELS_CONTAINER)).toContainText(NO_MODELS_TEXT, {
    timeout: 30000,
  });
  await waitForVideo(page);
}

async function holdAllModelsOff(page, modelNames) {
  await setModels(page, modelNames, false);
  await page.waitForTimeout(MODELS_OFF_VISIBLE_MS);
}

async function exerciseModelsOneAtATime(page, modelNames) {
  for (const name of modelNames) {
    await waitForVideo(page);
    await setModel(page, name, true);
    await waitForVideo(page);
    await page.waitForTimeout(MODEL_OUTPUT_VISIBLE_MS);
    await setModel(page, name, false);
  }
}

async function setModels(page, modelNames, enabled) {
  for (const name of modelNames) {
    await setModel(page, name, enabled);
  }
}

async function setModel(page, name, enabled) {
  const model = page.locator(MODEL_ITEM).filter({
    has: page.locator(MODEL_NAME, { hasText: new RegExp(`^${escapeRegExp(name)}$`) }),
  });
  const toggle = model.getByRole('switch');
  const toggleControl = model.locator('.model-toggle-switch');

  await expect(model).toHaveCount(1);
  await expect(toggleControl).toBeVisible();

  if ((await toggle.isChecked()) !== enabled) {
    await toggleControl.click();
  }

  if (enabled) {
    await expect(toggle).toBeChecked();
  } else {
    await expect(toggle).not.toBeChecked();
  }

  await expect.poll(() => backendModelState(page, name), { timeout: 10000 }).toBe(enabled);
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

async function backendModelState(page, name) {
  return page.evaluate(readBackendModelState, name);
}

function videoTrackIsLive() {
  const video = document.querySelector('#video');
  const track = video?.srcObject?.getVideoTracks?.()[0];
  return track?.readyState === 'live' && !track.muted && video.currentTime > 0;
}

function readBackendModelState(modelName) {
  return new Promise((resolve) => {
    const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
    const port = window.PEK_CONFIG?.ctrlPort ??
      (location.port || (location.protocol === 'https:' ? 443 : 80));
    const socket = new WebSocket(`${protocol}://${location.hostname}:${port}/ws`);
    let done = false;
    let timeout;
    const finish = (value) => {
      if (done) return;
      done = true;
      clearTimeout(timeout);
      socket.close();
      resolve(value);
    };
    timeout = setTimeout(() => finish(null), 4000);

    socket.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        for (const model of data.models ?? []) {
          if (model.name === modelName) {
            finish(model.active);
            return;
          }
        }
      } catch {
        // Ignore unrelated/noisy payloads until the timeout.
      }
    };
    socket.onerror = () => finish(null);
  });
}

module.exports = {
  expectSinkOnlyData,
  exerciseModelsOneAtATime,
  holdAllModelsOff,
  openPekUi,
  registeredModelNames,
  waitForVideo,
};

const { expect } = require('@playwright/test');

const MODEL_OUTPUT_VISIBLE_MS = 4000;
const MODEL_STATE_TIMEOUT_MS = 20000;
const MODELS_OFF_VISIBLE_MS = 3000;
const MODEL_ITEM = '.model-item';
const MODEL_NAME_ATTRIBUTE = 'data-model-name';
const MODEL_ELEMENT_NAME_ATTRIBUTE = 'data-model-element-name';
const MODELS_CONTAINER = '#models-container';
const NO_MODELS_TEXT = 'No models registered yet';
const STATUS_LINE = '#status-line';
const VIDEO_PROGRESS_TIMEOUT_MS = 30000;
const VIDEO_SAMPLE_INTERVAL_MS = 1000;
const VIDEO_SAMPLE_COUNT = 8;

async function openPekUi(page) {
  await page.addInitScript(capturePeerConnections);

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

async function waitForVideo(page, timeout = 60000) {
  await expect(page.locator(STATUS_LINE)).toContainText(/connected|video/i, {
    timeout,
  });

  await waitForHealthyVideoState(page, null, timeout);
}

async function registeredModels(page) {
  const modelItems = page.locator(`${MODELS_CONTAINER} ${MODEL_ITEM}`);
  await expect(modelItems.first()).toBeVisible({ timeout: 90000 });

  const models = await modelItems.evaluateAll((items, attributes) => items.map((item) => ({
    name: item.getAttribute(attributes.name)?.trim() || '<unknown>',
    elementName: item.getAttribute(attributes.elementName)?.trim() || '',
  })), {
    name: MODEL_NAME_ATTRIBUTE,
    elementName: MODEL_ELEMENT_NAME_ATTRIBUTE,
  });
  expect(models.length).toBeGreaterThan(0);
  for (const model of models) {
    expect(model.elementName, `Missing element identity for model ${model.name}`).not.toBe('');
  }
  return models;
}

function modelLabelOverflowProblems(measurement) {
  const epsilon = 0.5;
  const problems = [];
  if (measurement.row.scrollWidth > measurement.row.clientWidth)
    problems.push('row has horizontal overflow');
  if (!measurement.copy || measurement.copy.left < measurement.row.left - epsilon ||
      measurement.copy.right > measurement.row.right + epsilon)
    problems.push('label container extends outside row');
  if (!measurement.actions || measurement.actions.right > measurement.row.right + epsilon)
    problems.push('toggle controls extend outside row');
  if (measurement.copy && measurement.actions &&
      measurement.copy.right > measurement.actions.left + epsilon)
    problems.push('label content overlaps toggle controls');
  if (!measurement.task || measurement.task.scrollWidth > measurement.task.clientWidth)
    problems.push('task label overflows');
  if (measurement.details && measurement.details.scrollWidth > measurement.details.clientWidth &&
      (measurement.details.overflowX !== 'hidden' ||
       measurement.details.textOverflow !== 'ellipsis'))
    problems.push('long model details are not contained by ellipsis');
  return problems;
}

async function expectModelLabelsDoNotOverflow(page) {
  const measurements = await page.locator(`${MODELS_CONTAINER} ${MODEL_ITEM}`)
    .evaluateAll((items, attributes) => items.map((item) => {
      const copy = item.querySelector('.model-copy');
      const task = item.querySelector('.model-task');
      const details = item.querySelector('.model-details');
      const actions = item.querySelector('.model-actions');
      const rowRect = item.getBoundingClientRect();
      const copyRect = copy?.getBoundingClientRect();
      const actionsRect = actions?.getBoundingClientRect();
      const detailsStyle = details ? getComputedStyle(details) : null;

      return {
        name: item.getAttribute(attributes.name) || '<unknown>',
        elementName: item.getAttribute(attributes.elementName) || '<unknown>',
        row: {
          clientWidth: item.clientWidth,
          scrollWidth: item.scrollWidth,
          left: rowRect.left,
          right: rowRect.right,
        },
        copy: copyRect ? {left: copyRect.left, right: copyRect.right} : null,
        actions: actionsRect ? {left: actionsRect.left, right: actionsRect.right} : null,
        task: task ? {clientWidth: task.clientWidth, scrollWidth: task.scrollWidth} : null,
        details: details ? {
          clientWidth: details.clientWidth,
          scrollWidth: details.scrollWidth,
          overflowX: detailsStyle.overflowX,
          textOverflow: detailsStyle.textOverflow,
        } : null,
      };
    }), {
      name: MODEL_NAME_ATTRIBUTE,
      elementName: MODEL_ELEMENT_NAME_ATTRIBUTE,
    });

  const failures = [];
  for (const measurement of measurements) {
    const problems = modelLabelOverflowProblems(measurement);
    if (problems.length > 0)
      failures.push({...measurement, problems});
  }

  expect(failures, `Model label overflow diagnostics:\n${JSON.stringify(failures, null, 2)}`)
    .toEqual([]);
}

async function expectSinkOnlyData(page) {
  await expect(page.locator(MODELS_CONTAINER)).toContainText(NO_MODELS_TEXT, {
    timeout: 30000,
  });
  await waitForVideo(page);
  await expectVideoKeepsPlaying(page);
}

async function holdAllModelsOff(page, models) {
  await setModels(page, models, false);
  await page.waitForTimeout(MODELS_OFF_VISIBLE_MS);
}

async function exerciseModelsOneAtATime(page, models) {
  for (const model of models) {
    await waitForVideo(page);
    await setModel(page, model, true);
    await waitForVideo(page);
    await page.waitForTimeout(MODEL_OUTPUT_VISIBLE_MS);
    await setModel(page, model, false);
  }
}

async function setModels(page, models, enabled) {
  for (const model of models) {
    await setModel(page, model, enabled);
  }
}

async function setModel(page, modelState, enabled) {
  const model = page.locator(
    `${MODEL_ITEM}[${MODEL_ELEMENT_NAME_ATTRIBUTE}="${escapeCssAttribute(modelState.elementName)}"]`);
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

  await expect.poll(() => backendModelState(page, modelState.elementName), {
    timeout: MODEL_STATE_TIMEOUT_MS,
  }).toBe(enabled);
}

function escapeCssAttribute(value) {
  return value.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

async function backendModelState(page, elementName) {
  return page.evaluate(readBackendModelState, elementName);
}

async function expectVideoKeepsPlaying(page, sampleCount = VIDEO_SAMPLE_COUNT) {
  let previousState = await waitForHealthyVideoState(page, null, VIDEO_PROGRESS_TIMEOUT_MS);

  for (let index = 0; index < sampleCount; index += 1) {
    await page.waitForTimeout(VIDEO_SAMPLE_INTERVAL_MS);
    previousState = await waitForHealthyVideoState(
      page, previousState, VIDEO_PROGRESS_TIMEOUT_MS);
  }
}

async function expectVideoSurvivesLoops(page, loopCount, loopTimeoutMs) {
  const state = await waitForVideoLoops(page, loopCount, loopTimeoutMs);
  await waitForHealthyVideoState(page, state, 10000);
  await expectVideoKeepsPlaying(page);
}

async function waitForVideoLoops(page, loopCount, loopTimeoutMs) {
  let state = await page.evaluate(readVideoState);
  expectVideoStateToBeHealthy(state);

  for (let loop = 0; loop < loopCount; loop += 1) {
    const previousStreamId = state.streamId;

    try {
      await expect.poll(async () => {
        state = await page.evaluate(readVideoState);
        return state.streamId !== previousStreamId && videoStreamStateIsHealthy(state);
      }, { timeout: loopTimeoutMs }).toBe(true);
    } catch (error) {
      throw new Error(`${error.message}\nLast video state after loop ${loop + 1}: ${JSON.stringify(state)}`);
    }
  }

  return state;
}

async function waitForHealthyVideoState(page, previousState, timeout) {
  let state;

  try {
    await expect.poll(async () => {
      state = await page.evaluate(readVideoState);
      return videoStateIsHealthy(state) && videoStateHasProgressed(state, previousState);
    }, { timeout }).toBe(true);
  } catch (error) {
    throw new Error(`${error.message}\nLast video state: ${JSON.stringify(state)}`);
  }

  return state;
}

function expectVideoStateToBeHealthy(state) {
  expect(state.trackState).toBe('live');
  expect(state.trackMuted).toBe(false);
  expect(state.paused).toBe(false);
  expect(state.readyState).toBeGreaterThanOrEqual(2);
  expect(state.currentTime).toBeGreaterThan(0);
  expect(state.framesDecoded).toBeGreaterThan(0);
  expect(state.frameWidth).toBeGreaterThan(0);
  expect(state.frameHeight).toBeGreaterThan(0);
  expect(state.feedPaused).toBe(false);
  expect(state.freezeFrameVisible).toBe(false);
  expect(state.videoFeedHidden).toBe(false);
  expect(state.videoOpacity).toBeGreaterThan(0);

  if (state.videoWidth > 0 && state.videoHeight > 0) {
    expect(state.nonBlackRatio).toBeGreaterThan(0.2);
    expect(state.lumaVariance).toBeGreaterThan(20);
  }
}

function videoStateIsHealthy(state) {
  return videoStreamStateIsHealthy(state) &&
    state.feedPaused === false &&
    state.freezeFrameVisible === false &&
    state.videoFeedHidden === false &&
    state.videoOpacity > 0;
}

function videoStreamStateIsHealthy(state) {
  const videoElementIsHealthy = state.videoWidth === 0 || state.videoHeight === 0 ||
    (state.nonBlackRatio > 0.2 && state.lumaVariance > 20);

  return state.trackState === 'live' &&
    state.trackMuted === false &&
    state.paused === false &&
    state.readyState >= 2 &&
    state.currentTime > 0 &&
    state.framesDecoded > 0 &&
    state.frameWidth > 0 &&
    state.frameHeight > 0 &&
    videoElementIsHealthy;
}

function videoStateHasProgressed(state, previousState) {
  if (!previousState)
    return true;

  if (state.streamId !== previousState.streamId)
    return state.framesDecoded > 0 && state.currentTime > 0.25;

  return state.framesDecoded > previousState.framesDecoded &&
    state.currentTime > previousState.currentTime + 0.25;
}

function capturePeerConnections() {
  if (window.__pekPeerConnections)
    return;

  const NativePeerConnection = window.RTCPeerConnection;
  window.__pekPeerConnections = [];
  window.RTCPeerConnection = class extends NativePeerConnection {
    constructor(configuration) {
      super(configuration);
      window.__pekPeerConnections.push(this);
    }
  };
}

async function readVideoState() {
  const video = document.querySelector('#video');
  const freezeFrame = document.querySelector('#videoFreezeFrame');
  const track = video?.srcObject?.getVideoTracks?.()[0];
  const peerConnections = window.__pekPeerConnections ?? [];
  const peerConnection = peerConnections[peerConnections.length - 1];
  let inboundVideo;

  if (peerConnection) {
    const reports = await peerConnection.getStats();
    reports.forEach((report) => {
      if (report.type === 'inbound-rtp' &&
          (report.kind === 'video' || report.mediaType === 'video')) {
        inboundVideo = report;
      }
    });
  }

  const state = {
    currentTime: video?.currentTime ?? 0,
    paused: video?.paused ?? true,
    readyState: video?.readyState ?? 0,
    videoWidth: video?.videoWidth ?? 0,
    videoHeight: video?.videoHeight ?? 0,
    streamId: video?.srcObject?.id ?? null,
    trackState: track?.readyState ?? null,
    trackMuted: track?.muted ?? null,
    framesDecoded: inboundVideo?.framesDecoded ?? 0,
    frameWidth: inboundVideo?.frameWidth ?? 0,
    frameHeight: inboundVideo?.frameHeight ?? 0,
    feedPaused: Boolean(window.PEK_FEED_PAUSED),
    freezeFrameVisible: Boolean(freezeFrame?.classList.contains('is-visible')),
    videoFeedHidden: document.body.classList.contains('video-feed-hidden'),
    videoOpacity: Number.parseFloat(video ? getComputedStyle(video).opacity : '0'),
    nonBlackRatio: 0,
    lumaVariance: 0,
  };

  if (!video || video.readyState < 2 || video.videoWidth === 0 || video.videoHeight === 0)
    return state;

  const canvas = document.createElement('canvas');
  canvas.width = 32;
  canvas.height = 18;
  const context = canvas.getContext('2d', { willReadFrequently: true });

  try {
    context.drawImage(video, 0, 0, canvas.width, canvas.height);
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let nonBlackPixels = 0;
    let lumaTotal = 0;
    let squaredLumaTotal = 0;

    for (let offset = 0; offset < pixels.length; offset += 4) {
      const luma = 0.2126 * pixels[offset] +
        0.7152 * pixels[offset + 1] +
        0.0722 * pixels[offset + 2];
      lumaTotal += luma;
      squaredLumaTotal += luma * luma;
      if (luma > 8)
        nonBlackPixels += 1;
    }

    const pixelCount = pixels.length / 4;
    const meanLuma = lumaTotal / pixelCount;
    state.nonBlackRatio = nonBlackPixels / pixelCount;
    state.lumaVariance = squaredLumaTotal / pixelCount - meanLuma * meanLuma;
  } catch {
    return state;
  }

  return state;
}

function readBackendModelState(elementName) {
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
          if (model.element_name === elementName) {
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
  expectModelLabelsDoNotOverflow,
  expectSinkOnlyData,
  expectVideoKeepsPlaying,
  expectVideoSurvivesLoops,
  exerciseModelsOneAtATime,
  holdAllModelsOff,
  openPekUi,
  registeredModels,
  waitForVideo,
  waitForVideoLoops,
};

const { expect, test } = require('@playwright/test');

const {
  expectVideoKeepsPlaying,
  expectVideoSurvivesLoops,
  openPekUi,
  waitForVideo,
} = require('./pek-browser-helpers');

const STOCK_VIDEO_LOOP_TIMEOUT_MS = Number(process.env.STOCK_VIDEO_LOOP_TIMEOUT_MS || 900000);
const STOCK_VIDEO_LOOPS_TO_SURVIVE = 2;
const PAUSE_OBSERVATION_MS = 2000;
const HEALTH_CHECK_VIDEO_SAMPLES = 2;

test.setTimeout(STOCK_VIDEO_LOOP_TIMEOUT_MS + 180000);

async function openReadyStockVideo(page) {
  await openPekUi(page);

  const playPause = page.locator('#playPauseBtn');
  await expect(page.locator('.model-item').first()).toBeVisible({
    timeout: STOCK_VIDEO_LOOP_TIMEOUT_MS,
  });
  await expect(playPause).toBeEnabled();
  if (await playPause.getAttribute('aria-label') === 'Resume pipeline') {
    await playPause.click();
  }
  await expect(playPause).toHaveAttribute('aria-label', 'Pause pipeline');
  await waitForVideo(page, STOCK_VIDEO_LOOP_TIMEOUT_MS);
  await expectDetections(page, STOCK_VIDEO_LOOP_TIMEOUT_MS);
}

async function expectDetections(page, timeout = 60000) {
  await expect(page.locator('.inference-detection').first()).toBeVisible({ timeout });
}

async function expectHealthyOutput(page) {
  await waitForVideo(page);
  await expectVideoKeepsPlaying(page, HEALTH_CHECK_VIDEO_SAMPLES);
  await expectDetections(page);
  await expectNoWebRtcErrors(page);
}

async function expectNoWebRtcErrors(page) {
  await expect(page.locator('#log .log-line.error')).toHaveCount(0);
}

async function videoStreamId(page) {
  return page.locator('#video').evaluate((video) => video.srcObject?.id ?? null);
}

async function expectRemainsPaused(page, durationMs) {
  const stayedPaused = await page.evaluate((timeoutMs) => new Promise((resolve) => {
    const playPause = document.querySelector('#playPauseBtn');
    let paused = playPause?.getAttribute('aria-label') === 'Resume pipeline' &&
      document.body.classList.contains('is-feed-paused');
    const observer = new MutationObserver(() => {
      paused &&= playPause?.getAttribute('aria-label') === 'Resume pipeline' &&
        document.body.classList.contains('is-feed-paused');
    });

    observer.observe(playPause, { attributes: true, attributeFilter: ['aria-label'] });
    observer.observe(document.body, { attributes: true, attributeFilter: ['class'] });
    setTimeout(() => {
      observer.disconnect();
      resolve(paused);
    }, timeoutMs);
  }), durationMs);

  expect(stayedPaused).toBe(true);
}

test.describe('Stock video pause and loop recovery', () => {
  test.afterEach(async ({ page }) => {
    await expectNoWebRtcErrors(page);
  });

  test('pause, resume, and pause remain stable', async ({ page }) => {
    await openReadyStockVideo(page);

    const playPause = page.locator('#playPauseBtn');
    const initialStreamId = await videoStreamId(page);
    await playPause.click();
    await expect(playPause).toHaveAttribute('aria-label', 'Resume pipeline');

    await page.waitForTimeout(PAUSE_OBSERVATION_MS);
    await playPause.click();
    await expect(playPause).toHaveAttribute('aria-label', 'Pause pipeline');
    await expectHealthyOutput(page);
    expect(await videoStreamId(page)).toBe(initialStreamId);

    await page.waitForTimeout(400);
    await playPause.click();
    await expect(playPause).toHaveAttribute('aria-label', 'Resume pipeline');

    await page.waitForTimeout(PAUSE_OBSERVATION_MS);
    await playPause.click();
    await expect(playPause).toHaveAttribute('aria-label', 'Pause pipeline');
    await expectHealthyOutput(page);
    expect(await videoStreamId(page)).toBe(initialStreamId);
  });

  test('pause hides video and detections', async ({ page }) => {
    await openReadyStockVideo(page);

    const playPause = page.locator('#playPauseBtn');
    await playPause.click();
    await expect(playPause).toHaveAttribute('aria-label', 'Resume pipeline');
    await expect(page.locator('body')).toHaveClass(/is-feed-paused/);
    await expect(page.locator('#inferenceOutputBody')).toContainText('Inference paused.');
    await expect(page.locator('.inference-detection')).toHaveCount(0);
    await expect.poll(() => page.evaluate(() => {
      const wrapper = document.querySelector('.video-wrapper');
      const overlay = getComputedStyle(wrapper, '::before');
      return { background: overlay.backgroundColor, opacity: overlay.opacity };
    })).toEqual({ background: 'rgb(0, 0, 0)', opacity: '1' });
    await page.waitForTimeout(PAUSE_OBSERVATION_MS);

    await playPause.click();
    await expect(playPause).toHaveAttribute('aria-label', 'Pause pipeline');
    await expectHealthyOutput(page);
  });

  test('pause beyond the watchdog keeps the same PeerConnection', async ({ page }) => {
    await openReadyStockVideo(page);

    const playPause = page.locator('#playPauseBtn');
    const peerConnectionCount = await page.evaluate(() => window.__pekPeerConnections.length);
    const initialStreamId = await videoStreamId(page);
    const frameTimeoutMs = await page.evaluate(() => {
      const pekConfig = window.PEK_CONFIG || {};
      return pekConfig.webrtc?.frameTimeoutMs ?? pekConfig.webrtcFrameTimeoutMs ?? 30000;
    });
    expect(frameTimeoutMs).toBeGreaterThan(0);
    const pauseDurationMs = frameTimeoutMs * 2;

    await playPause.click();
    await expect(playPause).toHaveAttribute('aria-label', 'Resume pipeline');
    await expectRemainsPaused(page, pauseDurationMs);
    await expect.poll(
      () => page.evaluate(() => window.__pekPeerConnections.length),
    ).toBe(peerConnectionCount);
    expect(await videoStreamId(page)).toBe(initialStreamId);
  });

  test('visible video survives two stock video loops', async ({ page }) => {
    test.setTimeout(STOCK_VIDEO_LOOP_TIMEOUT_MS * STOCK_VIDEO_LOOPS_TO_SURVIVE + 180000);
    await openReadyStockVideo(page);
    await expectVideoSurvivesLoops(
      page,
      STOCK_VIDEO_LOOPS_TO_SURVIVE,
      STOCK_VIDEO_LOOP_TIMEOUT_MS,
    );
    await expectHealthyOutput(page);
  });
});

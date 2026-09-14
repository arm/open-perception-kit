const { createHash } = require('node:crypto');
const { mkdir, writeFile } = require('node:fs/promises');
const path = require('node:path');

const { expect, test: base } = require('@playwright/test');

const test = base.extend({
  browserCoverage: [async ({ browserName, page }, use, testInfo) => {
    const outputDir = process.env.PLAYWRIGHT_COVERAGE_DIR;
    const selectedTest = process.env.BROWSER_SMOKE_TEST;
    const enabled = Boolean(outputDir) && browserName === 'chromium' &&
      (!selectedTest || testInfo.title === selectedTest);
    let coverageMissing = false;

    if (enabled) {
      await page.coverage.startJSCoverage({ resetOnNavigation: false });
    }

    try {
      await use();
    } finally {
      if (enabled) {
        const coverage = (await page.coverage.stopJSCoverage()).filter((entry) => {
          try {
            return new URL(entry.url).pathname.endsWith('/pek-web.js');
          } catch {
            return false;
          }
        });
        coverageMissing = coverage.length === 0;
        if (!coverageMissing) {
          const id = createHash('sha256')
            .update(`${process.env.PLAYWRIGHT_BLOB_OUTPUT_NAME || ''}:${testInfo.testId}`)
            .digest('hex');
          await mkdir(outputDir, { recursive: true });
          await writeFile(path.join(outputDir, `${id}.json`), JSON.stringify(coverage));
        }
      }
    }

    if (coverageMissing) {
      throw new Error('Chromium did not report coverage for pek-web.js');
    }
  }, { auto: true }],
});

module.exports = { expect, test };

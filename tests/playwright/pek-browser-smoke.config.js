const { defineConfig } = require('@playwright/test');

const supportedBrowsers = ['chromium', 'firefox', 'webkit'];

const project = (browser) => {
  if (supportedBrowsers.includes(browser)) {
    return { name: browser, use: { browserName: browser } };
  }
  throw new Error(`Unsupported BROWSER_SMOKE_BROWSERS entry: ${browser}`);
};

const projects = (process.env.BROWSER_SMOKE_BROWSERS || 'chromium')
  .split(',')
  .map((browser) => browser.trim().toLowerCase())
  .filter(Boolean)
  .map(project);

module.exports = defineConfig({
  testDir: __dirname,
  timeout: 240000,
  workers: 1,
  use: {
    baseURL: process.env.PLAYWRIGHT_BASE_URL || 'http://127.0.0.1:9999',
    screenshot: 'only-on-failure',
    trace: 'off',
    video: 'on',
  },
  projects,
});

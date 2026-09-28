// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
  testDir: __dirname,
  testMatch: 'smoke.spec.js',
  workers: 1,
  retries: 0,
  timeout: 60000,
  expect: { timeout: 30000 },
  outputDir: '../../../test-results/quick-start-smoke',
  use: {
    browserName: 'chromium',
    baseURL: 'http://127.0.0.1:9999',
    screenshot: 'only-on-failure',
    trace: 'retain-on-failure',
  },
  webServer: {
    command: 'docker exec -u dev open-perception-kit bash -lc "cd /work && exec ./tools/opk-menu yolo26n-320"',
    url: 'http://127.0.0.1:9999/opk-config.js',
    timeout: 60000,
    reuseExistingServer: false,
    stdout: 'pipe',
    stderr: 'pipe',
  },
});

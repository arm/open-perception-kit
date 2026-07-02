# Playwright testing

The goal is to run basic browser smoke coverage for PEK web UI behavior. The
report shows the step-by-step execution, and each run captures video.

The current front-end hooks are ad-hoc, so checks such as "the stream arrived"
or "the video is playing" are intentionally lightweight and fragile. The current
Playwright goal is to set up the basic infrastructure and demonstrate the
capability. These tests should be tightened once the UI exposes stable test
signals.

This directory contains the Playwright browser smoke test runner and the GitHub
Pages report publisher.

The browser targets are Chromium, Firefox, and WebKit/Safari because those are
the browser engines Playwright supports.

## Layout

- `browser-smoke/` runs the PEK browser UI smoke tests against local-data
  pipelines.
- `browser-smoke/run.sh` is the local and CI entrypoint for the browser smoke
  run.
- `pages/` publishes Playwright HTML reports to GitHub Pages.
- `pages/publish.sh` is the GitHub Actions entrypoint for report publish and
  cleanup.
- `pages/assets/` contains the CSS and JavaScript injected into published
  reports.

## Browser smoke

The browser smoke runner keeps Playwright and npm dependencies inside a separate
Docker image. The self-hosted runner only needs Docker, the quick-start
container, and the repository checkout.

Local prerequisites:

```bash
./scripts/quick-start/start-container.sh --recreate
./scripts/build.sh
```

Stop other local PEK/devcontainer instances first if they already publish the
standard PEK ports, especially `2222` and `9999`.

Run the default local smoke test:

```bash
./scripts/playwright/browser-smoke/run.sh
```

Run the same browser set used by CI:

```bash
BROWSER_SMOKE_BROWSERS=chromium,firefox,webkit \
BROWSER_SMOKE_REBUILD=1 \
NUM_FRAMES=45000 \
./scripts/playwright/browser-smoke/run.sh
```

Outputs:

- `playwright-report/`
- `test-results/playwright/`

The smoke runner starts PEK pipelines inside the quick-start container, runs
Playwright in the browser-smoke Docker image, records videos, and merges all
browser phases into one Playwright HTML report.

## Pages publisher

The Pages publisher consumes the `rpi-browser-smoke-<run-id>-<attempt>` artifact
from GitHub Actions and updates the persistent Pages site.

The workflow entrypoints are:

```bash
./scripts/playwright/pages/publish.sh publish
./scripts/playwright/pages/publish.sh cleanup
```

`publish` is intended for `workflow_run` events from the PEK CI workflow.
`cleanup` is intended for scheduled or manual cleanup of closed PR reports after
the retention window.

The publisher is implemented in Python so the HTML generation, source-link
mapping, retention decisions, and Playwright report injection points can be unit
tested.

Run publisher tests:

```bash
python3 -m unittest scripts/playwright/pages/test_publish_playwright_pages.py
```

The tests intentionally fail if the expected Playwright report injection points
(`<title>`, `</head>`, and `<body>`) disappear, so Playwright report markup drift
is visible before a broken report is published.

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
- `pages/run.sh` is the local and GitHub Actions entrypoint for report publish and
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

The workflow entrypoints run the publisher inside a small Docker image:

```bash
./scripts/playwright/pages/run.sh publish
./scripts/playwright/pages/run.sh cleanup
```

`publish` is intended for `workflow_run` events from the PEK CI workflow.
`cleanup` is intended for scheduled or manual cleanup of closed PR reports after
the retention window.

### Report persistence

GitHub Pages deployments are immutable artifacts, so the publisher stores the
current report index in the `playwright-pages` storage branch.

Publish flow:

- Download the `rpi-browser-smoke-<run-id>-<attempt>` artifact.
- Check out `playwright-pages` into `_playwright_pages_site`.
- Update only the affected report path:
  - `prs/<number>/` for PR reports.
  - `nightly/` for scheduled `develop` reports.
- Rebuild the top-level `index.html`.
- Store the pruned report in `playwright-pages`; Playwright videos remain in
  their GitHub Actions artifacts.
- Commit and push `playwright-pages`.
- Restore deploy-only Playwright videos for retained reports, the latest YOLO
  detection videos, and referenced YOLO dataset files.
- Deploy `_playwright_pages_site` as the GitHub Pages artifact.

Expired GitHub Actions artifacts leave the report and its Actions run link
available, but without the embedded video.

Concurrency:

- The workflow uses the shared `report-pages` concurrency group.
- `cancel-in-progress: false` lets the running index update finish.
- The shared group avoids racing two pushes to the same storage branch.

Why this is better than manual artifact handling:

- Failed test runs can still publish their reports.
- PR, nightly, index, and cleanup updates use the same code path.
- Old closed PR reports can be pruned by the scheduled cleanup job.
- The Pages site does not depend on manually downloading and re-uploading ZIP
  artifacts.

Run a local publish dry-run from an existing `playwright-report/` directory:

```bash
PLAYWRIGHT_PAGES_DRY_RUN=1 \
PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR=playwright-report \
PLAYWRIGHT_PAGES_SITE_DIR=tmp/playwright-pages-local \
GITHUB_REPOSITORY=Arm-Debug/amp-dev-forge \
UPSTREAM_CONCLUSION=success \
UPSTREAM_EVENT=pull_request \
UPSTREAM_HEAD_BRANCH="$(git branch --show-current)" \
UPSTREAM_HEAD_REPOSITORY=Arm-Debug/amp-dev-forge \
UPSTREAM_HEAD_SHA="$(git rev-parse HEAD)" \
UPSTREAM_PR_NUMBER=181 \
UPSTREAM_RUN_ATTEMPT=1 \
UPSTREAM_RUN_ID=1 \
./scripts/playwright/pages/run.sh publish
```

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

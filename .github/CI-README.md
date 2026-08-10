# Edge AI Experience Kits CI Chain

## Overview

This repository uses a robust CI chain to ensure code quality, reproducibility, and platform consistency for all contributors. The CI system leverages Docker Compose and GitHub Actions to automate builds, quality checks, and tests across multiple platforms.
Each CI job runs in a dedicated container, ensuring a clean, reproducible environment. This way we can mitigate "this works on my machine" discussions.

## What does `.github/docker-compose.yml` do?

- Sets up the docker environment and creates easily accessible services for pek-ci

## What does `.github/workflows/pek-ci.yml` do?

- Builds one exact-SHA PEK CI image, shares it within the workflow run, then runs
  Quality, Sonar, release Sonar, Valgrind, and the `pek-ci` Docker Scout scan
  from that image.
- Uploads the PR image for the trusted GHCR publisher in the same build job, so
  every consumer waits for one complete image handoff.
- Reuses Docker layers through the ref-scoped cache flow below.
- Starts the Linux, Raspberry Pi, and macOS quick-start checks independently
  because they build their own platform images.
- Routes `run-python-audit`, `run-docker-scout`, and `run-workflow-audit` PR
  labels through this workflow so label-triggered checks do not create duplicate
  PR workflows. Workflow dependency freshness keeps its scheduled and manual
  entry points in `workflow-audit.yml`.
- Supports manual `all`, `quality`, `sonar`, and `valgrind` selections.
- Uses each pull request's immediate base branch, including stacked pull requests.
- Owns the nightly Quality and Valgrind run and the Valgrind baseline artifact.
- Owns release-tag Sonar analysis; the required PR Sonar check keeps the exact
  `Run Sonar analysis in Docker` name.
- Runs pull request quality checks through `expkits-ci --ci-pr-checks`.
- Runs full/nightly quality checks through `expkits-ci --ci-full-checks`.
- Lets `pek-ci-image-cleanup.yml` delete successful/cancelled run handoffs and
  all remaining PR caches when the pull request closes. Failed-run handoffs stay
  available for failed-job reruns.

The Python dependency, Docker Scout, and workflow dependency workflows remain
reusable and keep their independent schedule/manual triggers. Their direct PR
triggers are disabled; `pek-ci.yml` owns PR orchestration. Scheduled report
sources and artifact names therefore stay unchanged.

`.github/workflows/valgrind.yml` is only the trusted `pull_request_target`
publisher that requests a missing baseline from `pek-ci.yml`; it never runs PR
code. Its trusted helper also covers feature-branch bases used by stacked pull
requests.

### Cache flow

GHCR stores published outputs, not build caches: the latest successful image
for each PR and exact-SHA Valgrind baselines. Docker layers and compiler outputs
use the GitHub Actions cache.

| Run | Docker layers read from | Docker layers written to |
| --- | --- | --- |
| `main` or `develop` | Current branch cache | Current branch baseline |
| First PR run | Available base/default branch baseline | `refs/pull/<number>/merge` |
| Later PR commit or rerun | The PR cache, with base/default as fallback | The same PR cache |

The Buildx scope is always `pek-ci`. GitHub applies the branch and PR isolation;
the workflow does not build its own cache-key hierarchy. A PR cannot overwrite
the `main` or `develop` baseline. The same rule applies to stacked PRs: each PR
writes only its own merge ref.

| Stored data | Purpose | Lifetime |
| --- | --- | --- |
| Buildx `pek-ci` cache | Reuse Docker layers between runs | Branch/PR ref; deleted when the PR closes or GitHub evicts it |
| Quality, Sonar, and Valgrind ccache | Reuse compiled objects for the same check | PR ref; deleted when the PR closes or GitHub evicts it |
| Sonar CFamily server cache | Reuse target-branch or main fallback analysis in pull requests | Updated by `main` and `develop` push analysis |
| Run image cache | Pass the image from `Build PEK CI image` to its dependent jobs | Exact run; retained after failure for rerun, deleted after success/cancel or PR close |
| PR image artifact | Pass the verified image to the trusted GHCR publisher | One day |
| `pek-ci-pr-<number>` image in GHCR | Pull the latest successful PEK CI image locally | Replaced after the next successful run; deleted when the PR closes |
| Valgrind baseline in GHCR | Compare against the exact base SHA | Managed by the trusted baseline publisher |

The pull-request workflow has no package-write permission. After successful CI,
the trusted `PEK CI Image` workflow publishes the verified image:

```console
docker pull ghcr.io/arm-debug/amp-dev-forge-ci:pek-ci-pr-<number>
```

References: GitHub [cache access restrictions](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache),
Docker [Buildx `gha` cache scope](https://docs.docker.com/build/cache/backends/gha/#scope),
and Sonar [incremental analysis](https://docs.sonarsource.com/sonarqube-server/2025.4/analyzing-source-code/incremental-analysis/introduction/).

### Measured PR cache timings

Queue time is excluded; job time includes setup and cleanup. The cold run
followed deletion of all PR caches; the warm run reran the same SHA.

| Job | Artifact baseline | Cold ref cache | Warm rerun |
| --- | ---: | ---: | ---: |
| CI image | [1:37](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31381880114/job/93433659544) | [5:33](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93442576791) | [1:27](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447199213) |
| Quality | [5:07](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31381880114/job/93434036295) | [9:12](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93443869967) | [5:01](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447548498) |
| Valgrind | [6:55](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31381880114/job/93434036310) | [8:30](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93443869925) | [5:55](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447548536) |
| Sonar | [20:21](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31381880114/job/93434036354) | [14:01](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93443869962) | [14:02](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447548602) |
| Quick-start | [8:42](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31381880114/job/93433659560) | [7:52](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93442576778) | [8:34](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447199214) |
| Pi 5 and Playwright | [16:49](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31381880114/job/93433659508) | [16:05](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93442576787) | [16:34](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447199187) |
| CI image to Sonar | 21:58 | 19:34 | 15:29 |

| Cache-sensitive step | Cold | Warm |
| --- | ---: | ---: |
| Build CI image | 4:48 | 0:42, all 17 layers cached |
| Quality build and unit tests | 7:01, 2/113 hits | 2:53, 112/113 hits |
| Valgrind checks | 5:52, 2/88 hits | 3:34, 87/88 hits |
| Sonar analysis | 11:48, 0/96 server hits | 11:45, 0/96 server hits |

The warm compiler and layer-cache critical path is 6:29 shorter than the
artifact baseline. After a `develop` branch analysis seeded Sonar's server
cache, the same PR Sonar job reran in [8:30](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31387622127/job/93458223381):
the analysis step fell from 11:46 to 6:07, with 54/96 CFamily cache hits and an
81% symbolic-execution hit rate. The same-head CI image-to-Sonar path is 9:50,
12:08 (55.2%) shorter than the artifact baseline.

## What does `.github/workflows/release-tests.yml` do?

- Runs directly only for pull requests targeting `main`.
- Builds temporary x86_64 and Arm candidate archives and runs the native
  package smoke test for each architecture. It does not build documentation or
  publish a release.

## What does `.github/workflows/release-packages.yml` do?

| Event | Validation workflow | Package workflow outcome |
| --- | --- | --- |
| Pull request targeting `main` | Builds and smoke-tests the two architecture candidates | Not run |
| Push to `main` | Not run | Builds all three archives, smoke-tests both architecture archives, and publishes one GitHub Release plus one Artifactory folder |
| Manual dispatch | Not run | Resolves `source_ref`, builds all three archives, smoke-tests both architecture archives, and publishes one Artifactory folder |

Both workflows execute `SmokePackage.py` against their exact x86_64 and Arm
archives. Each smoke uses PyGObject to load the packaged private runtime,
discover the plugins through `GST_PLUGIN_PATH`, run inference to EOS, and
verify the packaged `peksink` web content.
Push and manual publication jobs cannot start unless both package smokes pass.
For pushes to `main`, Artifactory publication also waits for the GitHub Release
job to succeed. An existing `v<version>` therefore prevents publication to both
release destinations. Manual snapshots do not create or depend on a GitHub
Release.

Automatic `main` publication writes to `releases/<version>/`; manual
publication writes to
`snapshots/<label>/<full-sha>-<run-id>-<attempt>/` below
`https://artifactory.arm.com/artifactory/ai-expkits-internal.opk-ci`.
The same URL is used for uploads and generated download links.
The final Artifactory workflow log and `$GITHUB_STEP_SUMMARY` expose the folder,
all three links, and SHA-256 values for both paths.
If GitHub Release publication succeeds but Artifactory later fails, repair or
remove the partial GitHub Release before rerunning the workflow.

Each workflow resolves the selected commit's pinned `hfDownload` descriptors
once with `scripts/download-models.py` and transfers that model tree to both
architecture builds as a short-lived Actions artifact. Both archives receive
exactly the same six ONNX model directories: `cam-contact`, `gaze-detection`,
`osnet_x0_25`, `ultraface`, `yolo26`, and `yolov11`. Published packages contain
the model bytes and need neither Hugging Face access nor a token at runtime.

Build inputs reuse the repository's ONNX Runtime installer. The downloaded
ONNX Runtime package is checksum-verified. Hailo models, operation modules,
SDKs, and runtimes are excluded from both release architectures.

Release dependency preparation gets its model and runtime inputs from these
sources:

| Variables | Set or referenced in |
| --- | --- |
| `ONNXRUNTIME_VERSION` | Defaulted in `Dockerfile`; read and passed explicitly by both release workflows |
| `HF_TOKEN` | Temporary read-only repository secret; exposed only to each workflow's model-resolution step while checked-in models require authentication |

`Dockerfile` remains the version authority; release workflows use the value
from the selected source.
Dependency preparation uses the selected source's checked-in installers and
does not receive GitHub secrets. Only model resolution receives `HF_TOKEN`;
package build jobs receive the resolved files and no credentials.

Configured GitHub Actions secrets supply `HF_TOKEN`, `PEK_ARTIFACTORY_USERNAME`,
and `PEK_ARTIFACTORY_API_KEY`. Once the workflow is registered on the default `develop`
branch, a manual run may select a feature branch for release testing. GitHub
cannot manually dispatch a new workflow before it exists on the default branch.

### Hugging Face credential boundary

`HF_TOKEN` must be a dedicated, read-only CI credential rather than a
developer's personal token. Its use in same-repository workflows is an accepted
temporary trust boundary: everyone who can push a branch and trigger those
workflows is assumed to be authorized for the same model-read access. Fork pull
requests do not receive it. Workflows must keep it in the model-resolution or
model-image build step, pass it to container builds only as a BuildKit secret,
and never expose it to package builds, published artifacts, or runtime
containers.

`Arm/*` values in `hfDownload.repo_id` identify Hugging Face Hub model
repositories, not Git submodules. Private Git submodules use separate read-only
deploy keys such as `DEPLOY_KEY_FLOWDATA_SDK`; that authentication path is
unrelated to `HF_TOKEN`.

This CI credential is a pre-release bridge only. Before the first public
release:

- make every checked-in `hfDownload` source anonymously readable;
- remove every `${{ secrets.HF_TOKEN }}` reference from repository workflows;
- delete the repository Actions secret after no workflow references it; and
- run model resolution plus the x86_64 and Arm package smokes with `HF_TOKEN`
  unset.

Optional local BuildKit-secret support remains available for developers who add
their own private or gated models; it is not a release credential dependency.

The Artifactory job checks out the shared `Arm-Debug/publisher` package at the
exact commit pinned in `release-packages.yml`, then installs it from the
publisher repository's own tracked `uv.lock`. Update that workflow ref when a
reviewed publisher change is adopted; PEK does not copy or fork the package.

## What does `.github/workflows/agent-review.yml` do?

- Runs Agent review on a self-hosted runner through the shared Python OpenAI Agents SDK runner
- Supports `workflow_dispatch` manual runs with a configurable `base_ref` input for the diff baseline
- Uses `OPENAI_PROXY_TOKEN`, the Arm OpenAI proxy endpoint, and tracing-disabled Agents SDK execution
- Uses the checked-in review assets under `.github/agent-runtime/review/`
- Keeps static trusted Agent instructions in `.github/agent-runtime/review/instructions.md`
- Reuses shared helper modules from `scripts/private/agent_runtime/`
- Sets up the runtime venv through `scripts/private/agent_runtime/setup_runtime.py`
- Passes repository scope and bounded basic PR fields (number, title, body, and
  URL) through a typed SDK run context; the model can access that data only
  through `get_review_context`
- Treats PR descriptions as arbitrary free-form text without requiring headings,
  templates, or list structure, and labels all PR-derived tool data as untrusted evidence
- Reads the potentially large PR body from the GitHub event file instead of a
  process environment variable, then applies explicit normalization and bounds
- Uploads `agent-review-out` artifacts, including the structured review context,
  raw JSON output, and summary markdown
- Treats `agent-review-out/review.json` as the canonical machine-readable review state
- Publishes a fresh PR summary comment for each run from the structured review output
- Publishes fresh inline review comments for the current findings without prior-state reconciliation
- Always submits the GitHub review as `COMMENT`; recommendations never approve or formally request changes
- Treats OpenAI SDK review failures as advisory so they do not fail the workflow
- Does not run on pull request label changes, so unrelated labels cannot overwrite
  the Agent Review gate check

## What does `.github/workflows/agent-stabilize-pr-on-label.yml` do?

- Handles the `agent-stabilize` pull request label only for current-PR Agent
  Review finding stabilization
- Calls the shared `.github/workflows/agent-stabilize-pr-worker.yml` workflow using the
  latest canonical `agent-review-out/review.json` artifact for the PR head
- Also runs when someone adds `agent-stabilize` to an existing same-repository
  PR, because the workflow listens for the `pull_request` `labeled` event
- Ignores unrelated labels and does not create an Agent Review gate check

## What does `.github/agent-runtime/` do?

- Stores Agent runtime assets only: instructions, prompts, profiles, dependency pins, and model/task config
- Does not define executable GitHub Actions workflows; those live only in `.github/workflows/`

## What does `.github/workflows/workflow-audit.yml` do?

- Runs a minimal dependency freshness report for external GitHub Actions used by repository workflows
- Compares the current `uses:` refs against the latest GitHub release/tag for each action repository
- Publishes one simple Markdown report and a lightweight JSON snapshot in the `workflow-dependency-freshness` artifact
- On pull requests, is called by `pek-ci.yml` and runs only the report job; it
  does not open repair PRs

## What does `.github/workflows/agent-repair-source-run.yml` do?

- Acts as the public manual entrypoint for source-run repair only
- Accepts a source run ID plus optional target branch, task reference, repair profile, and dispatch nonce
- Calls `.github/workflows/agent-repair-source-run-worker.yml` with inherited secrets

## What does `.github/workflows/agent-repair-source-run-worker.yml` do?

- Contains the core repair engine behind the caller workflow
- Resolves source-run metadata, downloads logs and artifacts, and creates `goal.md` plus the companion Markdown context files under `.agent-runtime/source-run-repair/`
- Feeds the collected failure state and any downloaded artifact context into the OpenAI SDK repair agent so the patch is generated from the report instead of from inline workflow logic
- Runs the same shared Python OpenAI Agents SDK path as `agent-review`, with the Arm proxy and tracing disabled, to generate the repair patch
- Opens a draft repair PR only when the source run belongs to a PR carrying the profile-defined repair authorization label, then applies the profile-defined rerun label so normal PR validation can run outside the repair creation flow
- The default repair authorization label is `agent-repair`; it does not trigger current-PR stabilization
- Stays orchestration-thin by delegating repo-specific helper commands to `scripts/private/agent_repair_orchestrator/` and shared helper pieces to `scripts/private/agent_workflow_common/`
- Does not dispatch stabilization or merge the draft repair PR it opens; `.github/workflows/agent-stabilize-pr-worker.yml` remains the callable worker for explicit current-PR stabilization
- Uses `OPENAI_PROXY_TOKEN` for the OpenAI SDK step so the repair flow matches `agent-review`
- Supports the optional `EXPKITS_AGENT_TOKEN` secret so checkout, push, and PR operations can run under a PAT or GitHub App token instead of the default `GITHUB_TOKEN`

## What does `.github/workflows/agent-stabilize-pr-worker.yml` do?

- Resolves the PR branch and latest canonical Agent Review state for that PR head
- Creates stabilization prompt context under `.agent-runtime/pr-stabilization/`
- Snapshots helper code and checked-in Agent assets under `.agent-runtime/agent-stabilization-helper/` before checking out the PR head
- Runs the shared Python OpenAI Agents SDK stabilizer only when the latest Agent Review recommendation is not `approve`
- Runs the configured validation command set and pushes one follow-up commit with `EXPKITS_AGENT_TOKEN`
- Does not create repair PRs or inspect source-run repair artifacts

## What shared workflow plumbing lives under `scripts/private/`?

- `scripts/private/agent_runtime/setup_runtime.py` owns OpenAI agent runtime venv creation and dependency installation for review, repair, and stabilization workflows
- `scripts/private/agent_repair_orchestrator/` owns source-run repair input resolution, context collection, repair prompt rendering, patch packaging, branch push, and draft PR creation
- `scripts/private/agent_stabilization_orchestrator/` owns current-PR stabilization context preparation, helper snapshots, validation, and follow-up commits
- `scripts/private/agent_workflow_common/` owns shared process, GitHub output, JSON, review workflow metadata, task-ref, validation, and branch-push helpers
- `scripts/private/github_pr_context.py` resolves manual PR refs for standard PR-context workflow_dispatch runs with one `gh pr view --json baseRefName,headRefName,headRefOid` call
- `scripts/private/sonar_quality_gate_workflow.py` owns the Sonar API probe/report wrapper so the workflow YAML only wires inputs, artifacts, and environment

## What does `scripts/private/agent_runtime/openai_agent_runner.py` do?

- Provides the shared Python OpenAI Agents SDK entrypoint for review, repair, and stabilization jobs
- Sets the Arm OpenAI proxy base URL, maps `OPENAI_PROXY_TOKEN` into `OPENAI_API_KEY`, disables Agents SDK tracing, and injects `truststore` before importing OpenAI libraries
- Resolves the model from `.github/agent-runtime/runtime/agent-models.json` by agent instance; workflow plumbing passes config paths, not concrete model names
- Resolves task ownership and limits from `.github/agent-runtime/runtime/agent-tasks.json`, then dispatches through checked-in task classes instead of embedding task-specific behavior in the generic entrypoint
- Runs from the workflow-local `.agent-runtime/openai-agent-venv` environment created by `setup_runtime.py` so Ubuntu's externally managed system Python is left untouched
- Writes structured Agent review JSON for `agent-review` and lets repair/stabilization agents inspect the repo, run validation commands, and apply minimal patches without owning branch or PR lifecycle operations

## What do the split `.github/agent-runtime/` agentic roots do?

- `.github/agent-runtime/source-run-repair/` stores source-run repair prompts and repair profiles
- The default repair profile lives at `.github/agent-runtime/source-run-repair/profiles/profile.json`
- The workflow dependency freshness repair profile lives at `.github/agent-runtime/source-run-repair/profiles/workflow-dependency-freshness.json`
- `.github/agent-runtime/pr-stabilization/` stores current-PR stabilization prompts and the stabilization profile
- `.github/agent-runtime/workflow-policy/` stores shared prompt policy used by both repair and stabilization
- Runtime task limits and default task-to-agent-instance mapping live in `.github/agent-runtime/runtime/agent-tasks.json`
- Keeps long review and constraint text out of the workflow YAML and Python helpers while letting profiles carry flow-specific policy
- Lets helpers generate final `.agent-runtime/source-run-repair/*.md` and `.agent-runtime/pr-stabilization/*.md` files on the fly at runtime, so callers reuse the same core without checking generated prompt files into git

## How do agentic workflow helper commands run?

- Repair workflows call `python3 -m agent_repair_orchestrator ...` directly with `PYTHONPATH` pointed at `scripts/private/`
- Label-triggered stabilization workflows call `python3 -m agent_stabilization_orchestrator ...` directly with `PYTHONPATH` pointed at `scripts/private/` or at the restored helper snapshot
- Output-producing helper commands write to `GITHUB_OUTPUT` through their explicit `--github-output` argument
- Stabilization uses `snapshot-helper-bundle` and `restore-helper-bundle` helper commands to preserve the helper Python packages and prompt/config assets before checking out the PR head, then imports that snapshot while keeping the working directory on the PR checkout

## Operational Notes

- Self-hosted runner workspace isolation and the `/work` ownership hazard are
  documented in [.github/ci/self-hosted-runner-workspace-isolation.md](ci/self-hosted-runner-workspace-isolation.md).

## Functionalities

- **Triggers:** Runs on pull requests, `main`/`develop` pushes, `release/*`
  tags, manual dispatch, and the nightly schedule.
- **Branch and PR logic:** Standard checks run on non-draft PRs;
  `run-pek-ci`, `run-macos-ci`, `run-python-audit`, `run-docker-scout`, and
  `run-workflow-audit` route their selected work through the same PR workflow.
- **Context:** The shared-image job resolves the exact source SHA and immediate
  PR base; platform quick-start jobs checkout the event source directly.
- **Shared image:** Publishes `pek-ci` once and attaches each compatible Docker
  Compose service to it.
- **Platform checks:** Linux, Raspberry Pi, and macOS quick-start checks build
  their native images independently from the shared x86_64 CI image.
- **Agent Review:** A separate advisory workflow runs Agent Review, uploads the generated artifacts for the PR, posts a fresh comment-only summary review for each successful run, and publishes inline review comments for the current findings.
- **Ruleset sync:** A separate workflow applies the checked-in repository ruleset drafts to GitHub after they are merged to `develop`.

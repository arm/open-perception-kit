# Edge AI Experience Kits CI Chain

## Overview

This repository uses a robust CI chain to ensure code quality, reproducibility, and platform consistency for all contributors. The CI system leverages Docker Compose and GitHub Actions to automate builds, quality checks, and tests across multiple platforms.
Each CI job runs in a dedicated container, ensuring a clean, reproducible environment. This way we can mitigate "this works on my machine" discussions.

## What does `.github/docker-compose.yml` do?

- Sets up the docker environment and creates easily accessible services for pek-ci

## What does `.github/workflows/pek-ci.yml` do?

- Builds one exact-SHA PEK CI image, shares it within the workflow run, then runs
  Quality, Sonar, Valgrind, full Black Duck, and the `pek-ci` Docker Scout scan
  from that image.
- Uploads the complete run image handoff as a one-day raw tar artifact, so every
  consumer and the trusted PR GHCR publisher waits for the same image and
  exact-SHA helpers without consuming the Actions cache quota.
- Reuses Docker layers through the ref-scoped cache flow below.
- Starts the Linux, Raspberry Pi, and macOS quick-start checks independently.
  The macOS lane pulls an exact-SHA quick-start image from GHCR and seeds its
  compiler cache into temporary Colima volumes. It can reuse the newest
  image-compatible ancestor, or the PR-base image when its inputs are unchanged;
  missing images fall back to the local QEMU build. The checkout, job containers,
  and complete Colima VM are removed.
- Routes `run-python-audit`, `run-docker-scout`, and `run-workflow-audit` PR
  labels through this workflow so label-triggered checks do not create duplicate
  PR workflows. Workflow dependency freshness keeps its scheduled and manual
  entry points in `workflow-audit.yml`.
- Supports manual `all`, `quality`, `sonar`, and `valgrind` selections.
- Uses each pull request's immediate base branch, including stacked pull requests.
- Owns the nightly Quality and Valgrind run, the native deployment image
  caches, and the Valgrind baseline artifact.
- The required PR Sonar check keeps the exact `Run Sonar analysis in Docker`
  name. `release-packages.yml` owns release Sonar analysis.
- Runs one Black Duck subgraph beside Quality, Sonar, and Valgrind. Every
  same-repository pull request gets its own `pr-<number>` version, a scan of the
  release build output produced in the PEK CI image, a Rapid dependency policy
  check, a base-to-head snippet scan, and one required quality-gate result.
  Nightly runs and `release/*` tags run the full built-output, dependency, and
  snippet equivalents against `nightly` or the release tag. Full snippet scans
  materialize Meson wrap sources first.
  Downloaded scanner executables are checksum-verified and every Detect policy
  violation fails its lane. Persistent job summaries link to their Black Duck
  BOM; transient Rapid details remain in the workflow artifact and log.
- Runs pull request quality checks through `expkits-ci --ci-pr-checks`.
- Runs full/nightly quality checks through `expkits-ci --ci-full-checks`.
- Applies CI exceptions from the root-level `ci-suppressions.txt` only in the
  pull request that adds each `SUPPRESSION_TYPE: Reason` line. Merged entries
  remain as inert suppression history.
- Supports Sonar gate suppressions for `UNIT_TEST_COVERAGE`, `CODE_DUPLICATION`,
  `MAINTAINABILITY`, `RELIABILITY`, `SECURITY`, and `SECURITY_HOTSPOTS`.
  Sonar findings and unsuppressed gate conditions remain blocking.
- Lets `pek-ci-image-cleanup.yml` delete all remaining PR caches and the PR GHCR
  image when the pull request closes. One-day run artifacts remain available
  for failed-job reruns.

The Python dependency, Docker Scout, and workflow dependency workflows remain
reusable and keep their independent schedule/manual triggers. Their direct PR
triggers are disabled; `pek-ci.yml` owns PR orchestration. Scheduled report
sources and artifact names therefore stay unchanged.

`.github/workflows/valgrind.yml` is only the trusted `pull_request_target`
publisher that requests a missing baseline from `pek-ci.yml`; it never runs PR
code. Its trusted helper also covers feature-branch bases used by stacked pull
requests.

### Cache flow

GHCR stores the latest successful image for each PR, exact-SHA Valgrind
baselines, recent exact-SHA macOS quick-start images, and the nightly
amd64/arm64 deployment images. The deployment, macOS, and release Sonar jobs
export their BuildKit graphs to separate registry cache tags. Other Docker
layers and compiler outputs use the GitHub Actions cache. The macOS compiler
cache is embedded in its published image and copied into a temporary Colima
volume.

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
| macOS quick-start ccache seed | Avoid cold compilation under QEMU | Embedded in each published macOS quick-start image; the entrypoint copies it into a temporary Colima volume |
| Exact-SHA macOS quick-start image | Avoid QEMU image builds in the macOS lane | Published by `main` and `develop` pushes; newest 20 retained in GHCR |
| macOS quick-start BuildKit cache | Reuse publisher image layers | Current GHCR `buildcache` tag; superseded untagged versions are deleted |
| Sonar CFamily server cache | Reuse target-branch or main fallback analysis in pull requests | Updated by `main` and `develop` push analysis |
| Run image artifact | Pass the image and exact-SHA helpers from `Build PEK CI image` to its dependent jobs and trusted PR publisher | One day |
| `pek-ci-pr-<number>` image in GHCR | Pull the latest successful PEK CI image locally | Replaced after the next successful run; deleted when the PR closes |
| `nightly-amd64` and `nightly-arm64` deployment images in GHCR | Seed native release runtime layers | Replaced by the next nightly run |
| `buildcache-amd64` and `buildcache-arm64` in GHCR | Seed the complete native deployment build graph | Replaced by the next nightly run |
| `buildcache-release-sonar-amd64` in GHCR | Reuse the release Sonar `pek-ci` image layers | Replaced by the next release Sonar build |
| Valgrind baseline in GHCR | Compare against the exact base SHA | Managed by the trusted baseline publisher |

The pull-request workflow has no package-write permission. After successful CI,
the trusted `PEK CI Image` workflow publishes the verified image:

```console
docker pull ghcr.io/arm-debug/amp-dev-forge-ci:pek-ci-pr-<number>
```

References: GitHub [cache access restrictions](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache),
Docker [Buildx `gha` cache scope](https://docs.docker.com/build/cache/backends/gha/#scope),
and Sonar [incremental analysis](https://docs.sonarsource.com/sonarqube-server/2025.4/analyzing-source-code/incremental-analysis/introduction/).

### Measured PR timings

Queue time is excluded; job time includes setup and cleanup. The legacy
baseline built the same CI image independently in each job. Cold and warm paths
include the shared producer once; warm reran the same SHA with populated image
and compiler caches. Quick-start jobs are excluded because they remain
independent of this x86_64 image.

| Path | Legacy baseline | Cold ref cache | Warm rerun | Warm reduction |
| --- | ---: | ---: | ---: | ---: |
| CI image | built in every job | [5:33](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93442576791) | [1:27](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447199213) | n/a |
| Quality E2E | [10:07](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31254094969/job/93094753719) | 5:33 + [9:12](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93443869967) = 14:45 | 1:27 + [5:01](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447548498) = 6:28 | 36.1% |
| Sonar E2E | [20:00](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31254094986/job/93094753758) | 5:33 + [14:01](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93443869962) = 19:34 | 1:27 + [14:02](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447548602) = 15:29 | 22.6% |
| Valgrind E2E | [11:52](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31254094974/job/93094753670) | 5:33 + [8:30](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93443869925) = 14:03 | 1:27 + [5:55](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31384465615/job/93447548536) = 7:22 | 37.9% |
| Critical path | 20:00 | 19:34 | 15:29 | 22.6% |
| Runner time | 41:59 | 37:16 | 26:25 | 37.1% |

The cold run followed deletion of every PR cache; the warm run reran the same
SHA. Their cache-sensitive steps show where the warm reduction comes from:

| Cache-sensitive step | Cold | Warm |
| --- | ---: | ---: |
| Build CI image | 4:48 | 0:42, all 17 layers cached |
| Quality build and unit tests | 7:01, 2/113 hits | 2:53, 112/113 hits |
| Valgrind checks | 5:52, 2/88 hits | 3:34, 87/88 hits |
| Sonar analysis | 11:48, 0/96 server hits | 11:45, 0/96 server hits |

After a `develop` branch analysis seeded Sonar's server cache, the same PR Sonar
job reran in [8:30](https://github.com/Arm-Debug/amp-dev-forge/actions/runs/31387622127/job/93458223381):
the analysis step fell from 11:46 to 6:07, with 54/96 CFamily cache hits and an
81% symbolic-execution hit rate. The same-head CI image-to-Sonar path is 9:50,
10:10 (50.8%) shorter than the legacy baseline.

## What does `.github/workflows/release-tests.yml` do?

- Runs for pull requests targeting `main`, or manually for a selected
  `source_ref`.
- Builds temporary x86_64 and Arm release snapshot images. Each image runs its
  native offline Perception integration smoke during the Docker build and
  exports its validated archive; the Arm job also exports the embedded
  Perception wheel. A successful run is followed by disposable publication
  probes: generic Artifactory receives both archives and the release wheel, a
  disposable prerelease wheel is published and consumed through Artifactory
  PyPI, and the draft GitHub Release remains archive-only. Every probe deletes
  its uploads.

## What does `.github/workflows/release-packages.yml` do?

| Event | Candidate validation | Publication validation | Package publication |
| --- | --- | --- | --- |
| Pull request targeting `main` | Builds and smoke-tests the two architecture snapshot images | Uploads, verifies, and deletes the generic Artifactory, Artifactory PyPI, and GitHub Release probes | Not run |
| Push to `main` | Not run | Not run | Builds all three archives, smoke-tests and publishes one multi-architecture GHCR image, publishes the archives to GitHub Release and generic Artifactory, and publishes the wheel to Artifactory PyPI |
| Manual release validation | Resolves any commit, tag, or branch `source_ref`, builds and smoke-tests the two temporary architecture images | Uploads, verifies, and deletes the generic Artifactory, Artifactory PyPI, and GitHub Release probes | Not run |
| Manual package publication | Not run | Not run | Resolves `source_ref`, builds all three archives, smoke-tests and publishes one multi-architecture GHCR snapshot, then publishes the archives and wheel to one generic Artifactory snapshot folder |

For release builds, `pek-deployment-base` runs its smoke inside the existing
Dockerfile with networking disabled. The non-root runtime extracts the generated
archive, discovers its installed plugins, executes YOLov11 with ONNX Runtime and
YOLOX with ExecuTorch, requires non-empty output from `pekcomm`, and starts the
packaged `peksink` web surface. No separate smoke image or Dockerfile is built.
Push and manual publication jobs cannot start unless both native image builds
pass.
The native jobs push the existing `pek-deployment-base` outputs by digest and a
small merge job publishes those exact amd64 and arm64 digests as
`ghcr.io/arm-debug/amp-dev-forge-deployment:<tag>` without rebuilding. Stable
tags are the product version; manual tags also include the run ID and attempt.
The summary records the immutable multi-architecture digest.
For pushes to `main`, Artifactory publication also waits for the GitHub Release
job to succeed. An existing `v<version>` therefore prevents publication to both
release destinations. Manual snapshots do not create or depend on a GitHub
Release.

Release Sonar and the staging documentation deployment are independent jobs on
pushes to `main`. Their failures make the workflow red without blocking the
GitHub Release or Artifactory publication jobs. Release Sonar keeps its
`pek-ci` BuildKit graph in the dedicated GHCR registry cache above.

Automatic `main` publication writes to `releases/<version>/`; manual
publication writes to
`snapshots/<label>/<full-sha>-<run-id>-<attempt>/` below
`https://artifactory.arm.com/artifactory/ai-expkits-internal.opk-ci`.
The same URL is used for uploads and generated download links.
The final Artifactory workflow log and `$GITHUB_STEP_SUMMARY` expose the folder,
the three stable archive links or four snapshot links, and their SHA-256 values.
If GHCR or GitHub Release publication succeeds but a later publication fails,
repair or remove the partial publication before rerunning the workflow.

Each native architecture build uses the existing `pek-models` Docker artifact
stage to resolve the selected commit's pinned `hfDownload` descriptors. Both
archives receive the six ONNX model directories `cam-contact`,
`gaze-detection`, `osnet_x0_25`, `ultraface`, `yolo26`, and `yolov11`, plus the
checked-in ExecuTorch `yolox` model. Published packages contain the model bytes
and need neither Hugging Face access nor a token at runtime.

Build inputs reuse `pek-deployment-build`, which owns the repository build
toolchain and the ONNX Runtime and ExecuTorch Debian installers. The runnable
`pek-deployment-base` snapshot contains the prebuilt app and the validated
release archive. It retains the deployment lane's resolved configuration,
models, pipelines, and demo media. Release archives remain the narrow
seven-model integration surface and contain the standard, ONNX, and
experimental ExecuTorch operation modules, but no SDK headers or static
libraries. The release image does not install Hailo operation modules, SDKs, or
runtimes.

The same Docker stage packages the checked-in Perception SDK snapshot. The
workflow passes only the selected source and flowdata-sdk gitlink SHAs; it does
not initialize the private submodule or transfer a separate SDK input artifact.

Release image builds get their model and runtime inputs from these sources:

| Variables | Set or referenced in |
| --- | --- |
| `ONNXRUNTIME_VERSION` | Defaulted and consumed by `pek-deployment-build` |
| `EXECUTORCH_VERSION`, `EXECUTORCH_DEB_REVISION` | Defaulted and consumed by `pek-deployment-build` |
| `HF_TOKEN` | Read-only repository secret; exposed to `pek-models` only as a BuildKit secret while checked-in models require authentication |
| `PEK_ARTIFACTORY_USERNAME`, `PEK_ARTIFACTORY_API_KEY` | Existing repository secrets used to read the ExecuTorch Debian package and publish release archives |

`Dockerfile` remains the version authority. Release jobs build its existing
`pek-deployment-base` target for the native architecture and copy the archive
from `/opt/pek-release-artifacts`. The same image digest is the corresponding
GHCR manifest input. The native jobs import the nightly deployment lane's
architecture-specific BuildKit registry cache and fall back to its shared
ccache. They do not upload another full BuildKit graph after every release.
No prepared dependency or model tree is transferred between jobs.

The documentation release job likewise builds the existing `pek-docs` target,
runs `scripts/gen-doc.sh` in that container, and archives the generated HTML
with system `tar`. There is no separate release documentation image or package
script.

Configured GitHub Actions secrets supply `HF_TOKEN`, `PEK_ARTIFACTORY_USERNAME`,
and `PEK_ARTIFACTORY_API_KEY`. Once the workflow is registered on the default `develop`
branch, a manual run may select a feature branch for release testing. GitHub
cannot manually dispatch a new workflow before it exists on the default branch.

### Hugging Face credential boundary

`HF_TOKEN` must be a dedicated, read-only CI credential rather than a
developer's personal token. Its use in same-repository workflows is an accepted
trust boundary while this repository and required model sources remain private:
everyone who can push a branch and trigger those workflows is assumed to be
authorized for the same model-read access. Fork pull requests do not receive
it. Workflows must keep it in the model-resolution or model-image build step,
pass it to container builds only as a BuildKit secret, and never expose it to
package builds, published artifacts, or runtime containers.

`Arm/*` values in `hfDownload.repo_id` identify Hugging Face Hub model
repositories, not Git submodules. Private Git submodules use separate read-only
deploy keys such as `DEPLOY_KEY_FLOWDATA_SDK`; that authentication path is
unrelated to `HF_TOKEN`.

Before making the repository or its release pipeline public:

- make every checked-in `hfDownload` source anonymously readable;
- remove every `${{ secrets.HF_TOKEN }}` reference from repository workflows;
- delete the repository Actions secret after no workflow references it; and
- run model resolution plus the x86_64 and Arm image smokes with `HF_TOKEN`
  unset.

Optional local BuildKit-secret support remains available for developers who add
their own private or gated models.

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

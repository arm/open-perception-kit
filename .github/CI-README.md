# Edge AI Experience Kits CI Chain

## Core CI

`.github/compose.ci.yaml` defines the CI services. `.github/workflows/pek-ci.yml`
owns their GitHub Actions orchestration:

- one exact-SHA `pek-ci` image is published to a run-tagged GHCR reference;
  Quality, Sonar, Valgrind, Black Duck, and the `pek-ci` Docker Scout lane pull
  that image and verify its revision label
- Linux, Raspberry Pi, and macOS quick-start jobs build independently; the
  Raspberry Pi PR lane runs by default, while Linux and macOS require
  `run-pek-ci` or `run-macos-ci`; `run-pek-ci` replays both Linux and Raspberry
  Pi at the validated PR head
- `expkits-ci --ci-pr-checks` and `expkits-ci --ci-full-checks` remain the
  repository-owned quality entrypoints
- the required Sonar check keeps the `Run Sonar analysis in Docker` name;
  release Sonar belongs to `release-packages.yml`

Python dependency, Docker Scout, Black Duck, and workflow dependency checks
remain reusable workflows. `.github/workflows/valgrind.yml` is the trusted
`pull_request_target` publisher for missing Valgrind baselines and never runs
pull-request code.

## Cache contracts

| Stored data | Owner and lifetime |
| --- | --- |
| Buildx `pek-ci` cache | `develop`-owned GitHub Actions seed |
| Quality, Sonar, Valgrind, Black Duck, and Raspberry Pi ccache | Separate GitHub Actions branch/PR caches for each check |
| YOLO Pages benchmark inputs | Checksum-tagged GHCR data image produced by `develop` and read by the three Pages publishers |
| Run-tagged `pek-ci` image | GHCR handoff between jobs; deleted after one day |
| Exact-SHA Arm64 development image | GHCR; newest 20 retained; used by macOS and YOLO |
| Arm64 development BuildKit cache | GHCR `buildcache`; used by macOS, Raspberry Pi, and YOLO after an exact-image miss or when platform build arguments differ |
| YOLO compiler cache | Existing benchmark Docker volume shared by video and image-set setup |
| Native deployment BuildKit caches | GHCR architecture-specific `buildcache-*` tags |
| Release Sonar BuildKit cache | GHCR `buildcache-release-sonar-amd64` |
| Sonar CFamily server cache | Updated by `main` and `develop` analysis |
| Valgrind baseline | Artifactory, managed by the trusted baseline publisher |

The `develop` branch is the only writer of the PEK CI BuildKit cache; `main`,
pull requests, tags, and manual runs only read it.

- Pull requests write only their lane-specific Quality, Sonar, Valgrind, Black
  Duck, and Raspberry Pi compiler caches under the PR merge ref; reruns of the
  same PR reuse them, and the close workflow deletes them.

The architecture-neutral `pek-yolo-pages-dataset` target contains the verified
COCO val2017 images and pinned benchmark video used in Pages deployments. Its
immutable GHCR tag covers all three source checksums. The expanded inputs no
longer consume GitHub Actions cache quota; the scheduled cleanup removes the
retired cache entry.

The Arm64 development image embeds a compiler-cache seed. macOS copies it into
temporary Colima volumes, while YOLO keeps subsequent compiler output in its
existing benchmark volume. An exact-SHA hit skips the image build; misses and
the Raspberry Pi camera-enabled target rebuild from the registry layer cache.
Container layer ownership is documented in [Container Architecture](../docs/arch/containers.md).

The same-repository PEK CI image producer has package-write access for this handoff.
Fork pull requests cannot publish or consume that image path. Every consumer
validates the image revision against its checked-out SHA.

References: GitHub [cache access restrictions](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache),
Docker [Buildx `gha` cache scope](https://docs.docker.com/build/cache/backends/gha/#scope),
and Sonar [incremental analysis](https://docs.sonarsource.com/sonarqube-server/2025.4/analyzing-source-code/incremental-analysis/introduction/).

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
| Push to `main` | Not run | Not run | Builds all three archives, smoke-tests and publishes one multi-architecture GHCR image, publishes the archives to GitHub Release and generic Artifactory, the wheel to Artifactory PyPI, and the verified crate to Artifactory Cargo with a byte-for-byte download check |
| Manual release validation | Resolves any commit, tag, or branch `source_ref`, builds and smoke-tests the two temporary architecture images | Uploads, verifies, and deletes the generic Artifactory, Artifactory PyPI, and GitHub Release probes | Not run |
| Manual package publication | Not run | Not run | Resolves `source_ref`, builds all three archives, smoke-tests and publishes one multi-architecture GHCR snapshot, then publishes the archives, wheel, and crate to one generic Artifactory snapshot folder |

For release builds, `pek-deployment-base` runs its smoke inside the existing
Dockerfile with networking disabled. The non-root runtime extracts the generated
archive, discovers its installed plugins, executes YOLov11 with ONNX Runtime and
YOLOX with ExecuTorch, requires non-empty output from `pekcomm`, and starts the
packaged `peksink` web surface. No separate smoke image or Dockerfile is built.
Push and manual publication jobs cannot start unless both native image builds
pass.
Each native archive must also pass its Black Duck policy scan before GitHub
Release, GHCR index, or Artifactory publication can start.
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
the three stable archive links or five snapshot links, and their SHA-256 values.
See the [release process](../docs/arch/release-process.md) for language-package
routing and partial-publication recovery.

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
models, pipelines, demo media, embedded Python operation module, and a
target-platform Python runtime assembled from pinned wheels. Release archives
remain the narrow seven-model integration surface and contain the standard,
ONNX, experimental ExecuTorch, and Python operation modules, but no SDK headers
or static libraries.

The same Docker stage packages the checked-in Perception SDK snapshot. The
workflow passes only the selected source and flowdata-sdk gitlink SHAs; it does
not initialize the private submodule or transfer a separate SDK input artifact.

Release image builds get their model and runtime inputs from these sources:

| Variables | Set or referenced in |
| --- | --- |
| `ONNXRUNTIME_VERSION` | Defaulted and consumed by `pek-deployment-build` |
| `EXECUTORCH_VERSION`, `EXECUTORCH_DEB_REVISION` | Defaulted and consumed by `pek-deployment-build` |
| `HF_TOKEN` | Read-only repository secret; exposed to `pek-models` only as a BuildKit secret while checked-in models require authentication |
| `PEK_ARTIFACTORY_USERNAME`, `PEK_ARTIFACTORY_API_KEY` | Existing repository secrets used to read the ExecuTorch Debian package and publish release archives, Python wheels, and Rust crates |

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

- Self-hosted runner workspace isolation and the project-root ownership hazard are
  documented in [.github/ci/self-hosted-runner-workspace-isolation.md](ci/self-hosted-runner-workspace-isolation.md).

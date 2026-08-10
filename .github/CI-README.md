# Edge AI Experience Kits CI Chain

## Overview

This repository uses a robust CI chain to ensure code quality, reproducibility, and platform consistency for all contributors. The CI system leverages Docker Compose and GitHub Actions to automate builds, quality checks, and tests across multiple platforms.
Each CI job runs in a dedicated container, ensuring a clean, reproducible environment. This way we can mitigate "this works on my machine" discussions.

## What does `.github/docker-compose.yml` do?

- Sets up the docker environment and creates easily accessible services for pek-ci

## What does `.github/workflows/pek-ci.yml` do?

- Runs the actual checks
- Runs pull request quality checks through `expkits-ci --ci-pr-checks`.
- Runs full/nightly quality checks through `expkits-ci --ci-full-checks`.

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
| `HF_TOKEN` | Read-only repository secret; exposed only to each workflow's model-resolution step while checked-in models require authentication |

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
- run model resolution plus the x86_64 and Arm package smokes with `HF_TOKEN`
  unset.

Optional local BuildKit-secret support remains available for developers who add
their own private or gated models.

The Artifactory job checks out the shared `Arm-Debug/publisher` package at the
exact commit pinned in `release-packages.yml`, then installs it from the
publisher repository's own tracked `uv.lock`. Update that workflow ref when a
reviewed publisher change is adopted; PEK does not copy or fork the package.

## What does `.github/workflows/cairn-integration-snapshot.yml` do?

- Accepts one exact 40-character amp-dev-forge commit SHA.
- Reuses `ghcr.io/arm-debug/amp-dev-forge-cairn-python:<commit>` when its
  platform and source revision match the requested commit.
- Fails when an existing tag does not match that contract.
- Builds, smoke-tests, and publishes one native arm64 runtime image only when
  the commit tag is missing.
- Includes the PEK and GStreamer runtimes, Python GI bindings and introspection
  metadata, the YOLOv11 model configuration, and the image used by Cairn's
  current pipeline.
- Runs as the existing `pek` user (`1000:1000`) by default. A child image may
  switch to root for setup, but must select its non-root service user before
  defining the final runtime command.
- Does not create a GitHub Release or publish separate archives.

The workflow serializes runs for the same commit. Cairn can use the commit SHA as
its only integration pin: probe the deterministic image tag, dispatch this
workflow if it is missing, then wait until the image manifest is available.

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
- On pull requests, reruns only the report job so workflow changes can validate the same dependency evidence without opening repair PRs

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

- **Triggers:** Runs on pull requests, manual dispatch, and nightly schedule.
- **Branch and PR logic:** Only runs on non-draft PRs, or when the `run-pek-ci` label is added to a draft PR.
- **init-workspace:** Prepares the workspace and environment.
- **build-changed-applications:** Builds only the applications changed in a PR.
- **build-all-applications:** Builds all applications (nightly or manual trigger).
- **Agent Review:** A separate advisory workflow runs Agent Review, uploads the generated artifacts for the PR, posts a fresh comment-only summary review for each successful run, and publishes inline review comments for the current findings.
- **Ruleset sync:** A separate workflow applies the checked-in repository ruleset drafts to GitHub after they are merged to `develop`.

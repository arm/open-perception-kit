# Workflow Action Update Agent Context

## Goal

- Keep one automatic PR-head stabilization path only: same-repository pull
  requests run the standard `Agent Review`; PRs that carry the
  `agent-stabilize` label may then stabilize that PR head when the review is
  not `approve`.
- Keep manual PR stabilization available only through
  `.github/workflows/workflow-action-update-agent.yml`. The callable
  `.github/workflows/agent-stabilize-pr.yml` worker must not expose its own
  public dispatch path.
- Keep source-run repair PR creation separate from stabilization and merge: it
  is only for bounded work outside the source PR branch's direct stabilization
  scope, requires the source PR to carry the profile-defined repair
  authorization label, and ends after the draft repair PR is opened and labeled
  for normal PR validation.

## Hard Rules

- Reuse the `Agent Review` invocation pattern for any OpenAI-in-CI step: checkout, render prompt, run the shared Python OpenAI Agents SDK runner, then publish or consume structured output.
- Keep workflow YAML orchestration-thin. Repo-specific logic belongs in the `scripts/private/workflow_action_update_agent/` package behind direct `python3 -m workflow_action_update_agent ...` workflow calls.
- Keep validation plumbing canonical. Profiles select command set names and
  labels; concrete review-state scripts and local validation commands live in
  `scripts/private/workflow_action_update_agent/runtime.py`.
- Keep runtime prompt files under `.agent-runtime/workflow-action-update-agent/`; do not check generated prompt artifacts into git.
- Keep the stabilization loop focused on review findings only. It must not rewrite unrelated workflow plumbing.
- Keep labels role-specific: `agent-repair` authorizes source-run repair PR
  creation, while `agent-stabilize` triggers current-PR Agent Review finding
  stabilization. Do not treat either label as an alias for the other.
- Keep repair PR tasks bounded by source-run evidence and an explicit Definition
  of Done. If the source run is not associated with an authorized source PR, do
  not open a repair PR.
- Follow-up stabilization commits must rely on the next normal PR `Agent Review`
  run for proof. If the new review finds a new issue, the next PR event runs
  stabilization again.
- Prefer API polling over log scraping or annotation fetches when waiting for workflow completion.
- Functional tests should assert behavior and contract, not implementation trivia.

## Canonical OpenAI SDK Call

The reference implementation is `.github/workflows/agent-review.yml`.

- Runner: `self-hosted-ubuntu-latest`
- The shared runner performs deterministic prompt/diff size checks before the
  main review, repair, or stabilization agent run. Oversized tasks must be
  split instead of pushing the main agent past its turn budget.
- Prompt preparation stays outside the SDK runner in checked-in scripts.
- Agent runtime dependencies are installed by
  `scripts/private/agent_runtime/setup_runtime.py` from
  `.github/agent-runtime/runtime/requirements-openai-agents.txt` into
  `.agent-runtime/openai-agent-venv`.
- OpenAI invocation stays in `scripts/private/agent_runtime/openai_agent_runner.py`:
  - `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` is mapped to `OPENAI_API_KEY`
  - `OPENAI_BASE_URL` is `https://openai-api-proxy.geo.arm.com/api/providers/openai-eu/v1`
  - `OPENAI_AGENTS_DISABLE_TRACING` is `1`
  - `truststore.inject_into_ssl()` runs before importing `agents`, `openai`, or `httpx`
  - `model` comes from `.github/agent-runtime/runtime/agent-models.json` by agent instance
  - task limits and default task-to-agent-instance mapping come from `.github/agent-runtime/runtime/agent-tasks.json`
- Do not add repair-specific SDK home overrides or runner-specific `sudo` preflights around that call. If the runner works for `Agent Review`, reuse that exact SDK runner shape.
- The stabilizer should copy this shape and change only the prompt/output files and the follow-up validation/commit steps.

## Discoveries

- The useful gate is the standard `Agent Review` workflow on the PR. Repair-specific review logic should not fork that policy.
- `scripts/private/agent_runtime/review/publish.py` can publish a `request_changes` recommendation while the workflow run itself still concludes `success`. The stabilizer must look at structured review state, not only at workflow success/failure.
- Waiting on Actions runs via `gh api repos/{repo}/actions/runs/{id}` is more reliable than `gh run watch` for unattended polling.
- Fetching check-run annotations with the PAT was blocked by `HTTP 403: Resource not accessible by personal access token`; polling workflow runs avoids that permission edge.
- Self-hosted runner behavior is not perfectly uniform. Python OpenAI clients can fail corporate CA validation when they use the `certifi` bundle, so the runner injects the system trust store with `truststore` before importing OpenAI libraries.
- Agents SDK tracing is disabled in CI unless tracing is explicitly configured for this environment; otherwise the SDK may try to send traces outside the Arm proxy path.
- Stabilization attempts run through the callable `agent-stabilize-pr.yml`
  worker. External manual dispatch goes through
  `.github/workflows/workflow-action-update-agent.yml` so there is only one
  public entrypoint. Source-run repair PR creation must not dispatch this
  worker or merge the repair PR it opens.
- The branch under test still needs to be able to exercise the stabilizer workflow before merge. Use the current workflow ref for branch validation, but keep the SDK runner shape aligned with the canonical review workflow.
- If a stabilizer job checks out the PR head into the workspace root, any later local action lookup will resolve against the PR branch contents. Snapshot the helper bundle before the checkout and restore it under an ignored workspace path so the latest helper logic still drives the job.
- Stabilizer follow-up commits must push with `EXPKITS_AGENT_TOKEN`, not the workflow `github.token`, otherwise the PR branch update may not retrigger the normal `pull_request` workflows.
- Source-run repair PRs are opt-in from the source PR via the profile-defined
  authorization label. This prevents a failed run from opening a separate repair
  PR unless maintainers explicitly allowed that path.
- Draft repair PRs receive the profile-defined validation label after creation
  so standard PR workflows can run independently of the repair creation flow.
- Manual `Agent Review` runs may publish fresh PR comments for human visibility,
  but the stabilizer must consume only the `agent-review-out/review.json`
  artifact as machine-readable review state.
- At least one self-hosted repair runner does not have the `gh` CLI on `PATH`. The self-hosted agent patch-generation path must use GitHub REST downloads for run metadata, logs, and artifacts instead of assuming `gh run view/download` exists.

## Expected Flow

1. A same-repository PR is opened, reopened, synchronized, or marked ready for review.
2. The standard `Agent Review` workflow reviews the current PR head and publishes canonical review state.
3. If the PR carries `agent-stabilize`, the same workflow invokes the
   dedicated stabilizer workflow for that PR head.
4. If Agent Review already approves, the stabilizer writes a skip artifact and exits.
5. If Agent Review reports findings, the stabilizer applies the minimal follow-up patch, reruns configured validation commands, and pushes one follow-up commit.
6. The push retriggers normal PR workflows, including `Agent Review`; any new or remaining finding starts the next stabilization attempt for the new head.
7. Direct source-PR stabilization stops when the latest PR head receives an
   `approve` Agent Review.
8. If a source-run repair creates a draft repair PR, that flow ends after the
   repair branch, draft PR, and validation label are created. If someone later
   adds the `agent-stabilize` label to that repair PR, it is treated as
   ordinary current-PR stabilization, not as a continuation of the source-run
   repair.

## Token Notes

- `EXPKITS_AGENT_TOKEN` needs enough scope to push PR follow-up commits and dispatch workflows.
- `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` is the credential used by the canonical self-hosted OpenAI SDK runner.

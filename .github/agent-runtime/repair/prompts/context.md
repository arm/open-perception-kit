# Workflow Action Update Agent Context

## Goal

- Keep one automatic repair path only: same-repository pull requests run the
  standard `Agent Review`, then automatically stabilize that PR head when the
  review is not `approve`.
- Keep manual PR stabilization dispatchable for maintainers. The automatic path
  must not open separate repair PRs or merge the PR.

## Hard Rules

- Reuse the `Agent Review` invocation pattern for any OpenAI-in-CI step: checkout, render prompt, run the shared Python OpenAI Agents SDK runner, then publish or consume structured output.
- Keep workflow YAML orchestration-thin. Repo-specific logic belongs in `scripts/private/workflow_action_update_agent.py` behind `.github/actions/workflow-action-update-agent-helper/`.
- Keep runtime prompt files under `.agent-runtime/workflow-action-update-agent/`; do not check generated prompt artifacts into git.
- Keep the stabilization loop focused on review findings only. It must not rewrite unrelated workflow plumbing.
- Follow-up stabilization commits must rely on the next normal PR `Agent Review`
  run for proof. If the new review finds a new issue, the next PR event runs
  stabilization again.
- Prefer API polling over log scraping or annotation fetches when waiting for workflow completion.
- Functional tests should assert behavior and contract, not implementation trivia.

## Canonical OpenAI SDK Call

The reference implementation is `.github/workflows/agent-review.yml`.

- Runner: `self-hosted-ubuntu-latest`
- The shared runner performs a generic task-estimation agent call before the
  main review, repair, or stabilization agent run. Oversized tasks must be
  split instead of pushing the main agent past its turn budget.
- Prompt preparation stays outside the SDK runner in checked-in scripts.
- Agent runtime dependencies are installed from `.github/agent-runtime/runtime/requirements-openai-agents.txt`
  into `.agent-runtime/openai-agent-venv`.
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
- `.github/agent-runtime/review/scripts/publish-review.py` can publish a `request_changes` recommendation while the workflow run itself still concludes `success`. The stabilizer must look at structured review state, not only at workflow success/failure.
- Waiting on Actions runs via `gh api repos/{repo}/actions/runs/{id}` is more reliable than `gh run watch` for unattended polling.
- Fetching check-run annotations with the PAT was blocked by `HTTP 403: Resource not accessible by personal access token`; polling workflow runs avoids that permission edge.
- Self-hosted runner behavior is not perfectly uniform. Python OpenAI clients can fail corporate CA validation when they use the `certifi` bundle, so the runner injects the system trust store with `truststore` before importing OpenAI libraries.
- Agents SDK tracing is disabled in CI unless tracing is explicitly configured for this environment; otherwise the SDK may try to send traces outside the Arm proxy path.
- The workflow that performs a stabilization attempt needs to be separately dispatchable so the parent loop can reuse it, and maintainers can manually run it against any PR.
- The branch under test still needs to be able to exercise the stabilizer workflow before merge. Use the current workflow ref for branch validation, but keep the SDK runner shape aligned with the canonical review workflow.
- If a stabilizer job checks out the PR head into the workspace root, any later local action lookup will resolve against the PR branch contents. Snapshot the helper bundle before the checkout and restore it under an ignored workspace path so the latest helper logic still drives the job.
- Stabilizer follow-up commits must push with `EXPKITS_AGENT_TOKEN`, not the workflow `github.token`, otherwise the PR branch update may not retrigger the normal `pull_request` workflows.
- Draft PRs only trigger the heavy `pek-ci` and `sonar` jobs on the initial labeled/opened path. Later `synchronize` events do not exercise the same jobs while the PR stays draft, so the repair loop needs a deterministic manual PR-context bootstrap for those standard workflows.
- Plain `workflow_dispatch` on `pek-ci.yml` is not equivalent to PR validation: without explicit PR context it runs the nightly/full quality gate and can report unrelated baseline noise. Manual repair validation must pass PR metadata so the standard PR path runs.
- Manual `Agent Review` runs still need their artifact state published back onto the PR if we want the PR review state to reflect the latest head without waiting for a native `pull_request` run.
- At least one self-hosted repair runner does not have the `gh` CLI on `PATH`. The self-hosted agent patch-generation path must use GitHub REST downloads for run metadata, logs, and artifacts instead of assuming `gh run view/download` exists.

## Expected Flow

1. A same-repository PR is opened, reopened, synchronized, or marked ready for review.
2. The standard `Agent Review` workflow reviews the current PR head and publishes canonical review state.
3. The same workflow invokes the dedicated stabilizer workflow for that PR head.
4. If Agent Review already approves, the stabilizer writes a skip artifact and exits.
5. If Agent Review reports findings, the stabilizer applies the minimal follow-up patch, reruns configured validation commands, and pushes one follow-up commit.
6. The push retriggers normal PR workflows, including `Agent Review`; any new or remaining finding starts the next stabilization attempt for the new head.
7. The loop stops when the latest PR head receives an `approve` Agent Review. Maintainers still own final merge.

## Token Notes

- `EXPKITS_AGENT_TOKEN` needs enough scope to push PR follow-up commits and dispatch workflows.
- `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` is the credential used by the canonical self-hosted OpenAI SDK runner.

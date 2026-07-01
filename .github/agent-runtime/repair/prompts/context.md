# Workflow Action Update Agent Context

## Goal

- Keep one repair pipeline only: detect workflow freshness or CI regressions, generate the minimal patch, open a draft PR, wait for the normal PR checks and the normal Agent review, stabilize if review requests changes, then merge.
- Keep the repair flow reusable so later callers can plug in a different report source, such as Docker image freshness, without re-implementing PR lifecycle logic.

## Hard Rules

- Reuse the `Agent Review` invocation pattern for any OpenAI-in-CI step: checkout, render prompt, run the shared Python OpenAI Agents SDK runner, then publish or consume structured output.
- Keep workflow YAML orchestration-thin. Repo-specific logic belongs in `scripts/private/workflow_action_update_agent.py` behind `.github/actions/workflow-action-update-agent-helper/`.
- Keep runtime prompt files under `.agent-runtime/workflow-action-update-agent/`; do not check generated prompt artifacts into git.
- Keep the stabilization loop focused on review findings only. It must not rewrite unrelated workflow plumbing.
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

- The useful gate is the standard `Agent Review` workflow on the repair PR. Repair-specific review logic should not fork that policy.
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

1. Source workflow or report produces the repair context.
2. Repair workflow generates a patch and opens a draft PR.
3. Standard PR workflows run, including the normal `Agent Review`.
4. If Agent review recommends anything other than `approve`, dispatch the dedicated stabilizer workflow for that PR head.
5. The stabilizer workflow applies the minimal follow-up patch, reruns configured validation commands, and pushes one follow-up commit.
6. The parent loop waits for fresh PR workflows on the new head and repeats until Agent review approves or the max attempts are exhausted.
7. Merge only after the latest PR head is green.

## Token Notes

- `EXPKITS_AGENT_TOKEN` needs enough scope to open/edit/merge PRs, push the repair branch, and dispatch workflows.
- `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` is the credential used by the canonical self-hosted OpenAI SDK runner.

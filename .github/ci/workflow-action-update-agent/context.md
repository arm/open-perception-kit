# Workflow Action Update Agent Context

## Goal

- Keep one repair pipeline only: detect workflow freshness or CI regressions, generate the minimal patch, open a draft PR, wait for the normal PR checks and the normal Codex review, stabilize if review requests changes, then merge.
- Keep the repair flow reusable so later callers can plug in a different report source, such as Docker image freshness, without re-implementing PR lifecycle logic.

## Hard Rules

- Reuse the `main` branch `Codex Review` invocation pattern for any Codex-in-CI step: checkout, render prompt, run `openai/codex-action@v1`, then publish or consume structured output.
- Keep workflow YAML orchestration-thin. Repo-specific logic belongs in `scripts/private/workflow_action_update_agent.py` behind `.github/actions/workflow-action-update-agent-helper/`.
- Keep runtime prompt files under `.codex/workflow-action-update-agent/`; do not check generated prompt artifacts into git.
- Keep the stabilization loop focused on review findings only. It must not rewrite unrelated workflow plumbing.
- Prefer API polling over log scraping or annotation fetches when waiting for workflow completion.
- Functional tests should assert behavior and contract, not implementation trivia.

## Canonical Codex Call

The reference implementation is `.github/workflows/codex-review.yml` on `main`.

- Runner: `[self-hosted, Linux, X64]`
- Prompt preparation stays outside the action in checked-in scripts.
- Codex invocation stays the official action call:
  - `uses: openai/codex-action@v1`
  - `openai-api-key: ${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}`
  - `responses-api-endpoint: https://openai-api-proxy.geo.arm.com/api/providers/openai/v1/responses`
  - `model: gpt-5.3-codex`
  - `sandbox: danger-full-access`
  - `safety-strategy: unsafe`
- Do not add repair-specific `codex-home` overrides or runner-specific `sudo` preflights around that call. If the runner works for `Codex Review` on `main`, reuse that exact action shape.
- The stabilizer should copy this shape and change only the prompt/output files and the follow-up validation/commit steps.

## Discoveries

- The useful gate is the standard `Codex Review` workflow on the repair PR. Repair-specific review logic should not fork that policy.
- `codex-review/scripts/publish-review.py` can publish a `request_changes` recommendation while the workflow run itself still concludes `success`. The stabilizer must look at structured review state, not only at workflow success/failure.
- Waiting on Actions runs via `gh api repos/{repo}/actions/runs/{id}` is more reliable than `gh run watch` for unattended polling.
- Fetching check-run annotations with the PAT was blocked by `HTTP 403: Resource not accessible by personal access token`; polling workflow runs avoids that permission edge.
- Self-hosted runner behavior is not perfectly uniform. At least one runner hit a passworded `sudo` path inside the Codex action proxy hardening step, so the repair flow should keep the official action call shape and keep extra runner-specific workarounds out of the canonical review policy.
- The workflow that performs a stabilization attempt needs to be separately dispatchable so the parent loop can reuse it, and maintainers can manually run it against any PR.
- The branch under test still needs to be able to exercise the stabilizer workflow before merge. Use the current workflow ref for branch validation, but keep the Codex action shape aligned with the `main` canonical workflow.
- If a stabilizer job checks out the PR head into the workspace root, any later local action lookup will resolve against the PR branch contents. Snapshot the helper bundle before the checkout and restore it under an ignored workspace path so the latest helper logic still drives the job.
- Stabilizer follow-up commits must push with `EXPKITS_AGENT_TOKEN`, not the workflow `github.token`, otherwise the PR branch update may not retrigger the normal `pull_request` workflows.
- Draft PRs only trigger the heavy `pek-ci` and `sonar` jobs on the initial labeled/opened path. Later `synchronize` events do not exercise the same jobs while the PR stays draft, so the repair loop needs a deterministic manual PR-context bootstrap for those standard workflows.
- Plain `workflow_dispatch` on `pek-ci.yml` is not equivalent to PR validation: without explicit PR context it runs the nightly/full quality gate and can report unrelated baseline noise. Manual repair validation must pass PR metadata so the standard PR path runs.
- Manual `Codex Review` runs still need their artifact state published back onto the PR if we want the PR review state to reflect the latest head without waiting for a native `pull_request` run.
- At least one self-hosted repair runner does not have the `gh` CLI on `PATH`. The self-hosted Codex patch-generation path must use GitHub REST downloads for run metadata, logs, and artifacts instead of assuming `gh run view/download` exists.

## Expected Flow

1. Source workflow or report produces the repair context.
2. Repair workflow generates a patch and opens a draft PR.
3. Standard PR workflows run, including the normal `Codex Review`.
4. If Codex review recommends anything other than `approve`, dispatch the dedicated stabilizer workflow for that PR head.
5. The stabilizer workflow applies the minimal follow-up patch, reruns configured validation commands, and pushes one follow-up commit.
6. The parent loop waits for fresh PR workflows on the new head and repeats until Codex review approves or the max attempts are exhausted.
7. Merge only after the latest PR head is green.

## Token Notes

- `EXPKITS_AGENT_TOKEN` needs enough scope to open/edit/merge PRs, push the repair branch, and dispatch workflows.
- `OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS` is the credential used by the canonical self-hosted Codex action call.

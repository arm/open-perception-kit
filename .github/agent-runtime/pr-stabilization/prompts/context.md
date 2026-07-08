# Agent PR Stabilization Context

## Goal

- PR stabilization fixes only the latest standard `Agent Review` findings on the current PR branch.
- The flow is current-PR scoped: it does not create repair PRs, inspect source-run repair artifacts, or rewrite unrelated workflow plumbing.
- A follow-up stabilization commit must rely on the next normal PR `Agent Review` run for proof.

## Hard Rules

- Keep workflow YAML orchestration-thin. Stabilization workflow logic belongs in `scripts/private/agent_stabilization_orchestrator/`, shared helpers belong in `scripts/private/agent_workflow_common/`, and OpenAI SDK execution belongs in `scripts/private/agent_runtime/`.
- Stabilization consumes the canonical `agent-review-out/review.json` artifact. It must not infer review findings from workflow success, comments, annotations, or logs.
- Keep generated stabilization prompt files under `.agent-runtime/pr-stabilization/`; do not check generated prompt artifacts into git.
- Stabilization commits push with `EXPKITS_AGENT_TOKEN` so normal `pull_request` workflows retrigger.
- If the latest review state is missing, stale for the expected head SHA, or lacks actionable findings, leave the tree unchanged and report the blocker.

## Runtime Shape

- Reuse the checked-in Agent Runtime OpenAI SDK runner from `scripts/private/agent_runtime/`.
- Snapshot the helper bundle before checking out the PR head, then restore it under `.agent-runtime/agent-stabilization-helper/` so the workflow uses branch-current helper logic after checkout.
- The stabilizer changes only the prompt/output files and the follow-up validation/commit steps compared with the standard `Agent Review` runner shape.

## Expected Flow

1. The standard `Agent Review` workflow reviews the current PR head and publishes canonical review state.
2. If the PR carries `agent-stabilize`, `.github/workflows/agent-review.yml` invokes `.github/workflows/agent-stabilize-pr-worker.yml`.
3. If someone adds `agent-stabilize` to an existing same-repository PR, `.github/workflows/agent-stabilize-pr-on-label.yml` invokes the same worker for that current head.
4. If Agent Review already approves, the worker writes a skip artifact and exits.
5. If Agent Review reports findings, the stabilizer applies the minimal follow-up patch, reruns configured validation commands, and pushes one follow-up commit.
6. The push retriggers normal PR workflows, including `Agent Review`.

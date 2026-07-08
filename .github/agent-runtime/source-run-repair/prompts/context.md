# Agent Source-Run Repair Context

## Goal

- Source-run repair creates a separate draft PR only for bounded CI or workflow work outside the source PR branch's direct stabilization scope.
- The source PR must carry the profile-defined `agent-repair` authorization label before a repair PR can be opened.
- The flow ends after the repair branch, draft PR, and validation label are created. It does not dispatch stabilization or merge the generated PR.

## Hard Rules

- Keep workflow YAML orchestration-thin. Source-run repair workflow logic belongs in `scripts/private/agent_repair_orchestrator/`, shared helpers belong in `scripts/private/agent_workflow_common/`, and OpenAI SDK execution belongs in `scripts/private/agent_runtime/`.
- Profiles may select canonical validation command sets by name; concrete validation commands live in `scripts/private/agent_workflow_common/validation.py`.
- Keep generated repair prompt files under `.agent-runtime/source-run-repair/`; do not check generated prompt artifacts into git.
- Keep labels role-specific: `agent-repair` authorizes source-run repair PR creation, while `agent-stabilize` triggers current-PR Agent Review finding stabilization.
- If the source run is not associated with an authorized source PR, do not open a repair PR.
- Prefer API polling and REST artifact downloads over log scraping or local `gh` CLI assumptions on self-hosted runners.

## Runtime Shape

- Reuse the checked-in Agent Runtime OpenAI SDK runner from `scripts/private/agent_runtime/`.
- The runner receives a generated repair goal file and writes a repository patch plus agent output artifact.
- Repair PR generation uses profile-defined branch, title, commit, body, DoD, and label templates.

## Expected Flow

1. A source workflow run produces failure evidence outside the direct source PR branch stabilization scope.
2. A maintainer authorizes separate repair work by applying the `agent-repair` label to the source PR.
3. `.github/workflows/agent-repair-source-run.yml` manually dispatches `.github/workflows/agent-repair-source-run-worker.yml`.
4. The worker collects source-run logs and artifacts under `.agent-runtime/source-run-repair/`.
5. The repair agent produces the minimal patch needed for the source-run task and profile Definition of Done.
6. The worker applies the patch to a profile-defined repair branch, commits it, opens a draft PR, and applies the profile-defined validation label.

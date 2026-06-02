# Workflow Release Review

Use this checklist for changes under `.github/workflows/`.

## Check every time

- Trigger trust boundary is clear.
- `pull_request_target` and `workflow_run` do not checkout or run untrusted PR code.
- PR workflows handle fork PR checkout correctly.
- `permissions` are minimal for the job.
- Secrets and write-capable tokens are not exposed to fork PR code.
- Third-party actions are pinned and intentionally chosen.
- Caches and artifacts do not cross trust boundaries.
- Self-hosted jobs use isolated checkout paths and cleanup.
- Failure mode is explicit: required, opt-in, nightly-only, or manual-only.
- Outputs are stable and auditable; do not depend on internal or unstable action inputs.

## Flag as high risk

- New `pull_request_target` usage.
- New `workflow_run` job that can write, comment, tag, or release.
- New secret usage in PR-triggered flows.
- New broad `permissions`, especially `contents: write`, `pull-requests: write`, or `actions: write`.
- New external action that is not pinned.
- New checkout logic that assumes local branches instead of PR head repo and SHA.

## Nightly Agent Output

- List changed workflow files.
- Mark each item as `ok`, `review`, or `blocker`.
- Quote the exact risky lines.
- State the trust boundary involved.
- State the manual follow-up, if any.

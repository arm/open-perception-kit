# {{DISPLAY_NAME}}

Goal: repair a CI or workflow regression from the source GitHub Actions run and leave a clean working-tree patch. Later workflow steps will create the repair branch, commit, draft PR, rerun PR workflows, and merge on success. Your job is only to produce the minimal repository changes and validate them locally.

Read these first:

- `.codex/workflow-action-update-agent/ponytail-review.md`
- `.codex/workflow-action-update-agent/constraints.md`
- `.codex/workflow-action-update-agent/failure-context.md`
- `.codex/workflow-action-update-agent/validation.md`
- `.codex/workflow-action-update-agent/file-inventory.md`
{{PROMPT_CONTEXT_FILES}}

Then inspect `.codex/workflow-action-update-agent/source-run.log` and the relevant text files under `.codex/workflow-action-update-agent/artifacts/`.

Produce the minimal repo changes needed to address the regression. If the evidence is insufficient for a safe fix, leave the tree unchanged and explain exactly why in your final message.

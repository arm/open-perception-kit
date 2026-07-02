# Workflow Action Update Agent Stabilization

Goal: address the latest standard Agent Review findings on PR #175 and leave the current PR branch with only the minimal repository changes needed to turn the review into `approve`.

Read these first:
- `.github/agent-runtime/repair/prompts/context.md`
- `.github/agent-runtime/repair/prompts/ponytail-review.md`
- `.github/agent-runtime/repair/prompts/constraints.md`
- `.github/PULL_REQUEST_TEMPLATE.md`
- `.github/workflows/agent-review.yml`

Relevant repo context files:
- `.github/agent-runtime/repair/prompts/context.md`
- `.github/ci/workflow-release-review.md`
- `.github/PULL_REQUEST_TEMPLATE.md`
- `.github/workflows/agent-review.yml`
- `.github/workflows/pek-ci.yml`
- `.github/workflows/sonar.yml`
- `.github/compose.ci.yaml`

Then inspect `.agent-runtime/workflow-action-update-agent/review-state.json`.

Context:
- Source run ID: 28586547643
- PR number: 175
- PR branch: feature/EXPKITS-1234/openai-sdk-agent-review-workflow
- Review workflow: Agent Review
- Review run ID: 28586547643
- Review recommendation: request_changes
- Review summary: Found one concrete regression in the new agent tool boundary: the shell tool blocks mutating git commands but the exposed patch tool still allows repository mutation during review runs.

Validation commands that will run after your edits:
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_detect_secrets_quality_flow.py'`
- `python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_workflow_action_update_agent_flow.py'`
- `git diff --stat`

Review state JSON:

```json
{
  "finding_count": 1,
  "finding_count_available": false,
  "findings": [
    {
      "body": "The new review workflow runs the OpenAI agent against PR code and instructs mutation to be blocked, but `apply_unified_diff` is exported as a function tool for all agent tasks and directly runs `git apply --whitespace=nowarn`. `reject_unsafe_shell_command` only protects `run_shell_command`, so a review task can still modify the working tree before later summary/publish steps. This creates a trust-boundary/regression risk for the read-only review job; restrict the patch tool to repair/stabilization tasks or make review runs register only read-only tools.",
      "confidence": 0.87,
      "diff_side": "RIGHT",
      "end_line": 393,
      "path": "scripts/private/agent_runtime/repo_tools.py",
      "score": 0.78,
      "severity": "major",
      "start_line": 386,
      "suggestion": null,
      "title": "Review agent can still mutate the checkout via apply_unified_diff"
    }
  ],
  "head_sha": "3c6951647b54dd87cdb549d5427f798dbc69dc8d",
  "overall_confidence": 0.87,
  "overall_recommendation": "request_changes",
  "overall_score": 0.78,
  "run_id": "28586547643",
  "summary": "Found one concrete regression in the new agent tool boundary: the shell tool blocks mutating git commands but the exposed patch tool still allows repository mutation during review runs."
}
```

Instructions:
- Fix only the issues needed to turn the latest standard Agent Review into `approve`.
- Keep the diff minimal and focused on the review findings.
- Do not create commits, branches, pull requests, or change unrelated workflow plumbing.
- If the review findings are insufficient for a safe fix, leave the tree unchanged and explain exactly why in your final message.

---
applyTo: "**"
---
   
# PR Description Guidance

- Use only direct git commands with no shell plumbing.
- Generate a concise pull request description by reviewing the git diff for the PR.
- The target branch is ${PR_TARGET_BRANCH}.
- The source branch is ${PR_SOURCE_BRANCH}.
- If the source branch is not available locally, fetch it from ${PR_HEAD_REPO_URL}.
- Work from git, not from a prebuilt context file.
- Assume the current working directory is already the repository root.
- If the source branch is not available locally, fetch it with git from `PR_HEAD_REPO_URL`.
- Compare the PR branch against the target branch using git history and diff commands.
- Start by inspecting the diff summary, then inspect the relevant full diffs before writing the description.
- Use only direct `git` commands for repository inspection.
- Do not use `cd`, pipes, `grep`, `sed`, `awk`, shell chaining, or subshells.
- Read and interpret raw git output yourself.
- Use this exact structure:
  - `## Summary`
  - one short paragraph
  - `## Key changes`
  - a flat bullet list
- Focus on the most important behavior, architecture, configuration, or workflow changes visible in the diff.
- Be reviewer-focused and concrete.
- Do not mention branch names, file counts, or that the description was generated automatically.
- Do not invent tests, motivations, requirements, or follow-up work that are not evident from the provided context.
- Keep the full description under 220 words.

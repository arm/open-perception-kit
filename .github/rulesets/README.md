# GitHub Rulesets

These files are checked-in ruleset drafts intended to be imported into GitHub
or applied through the repository rulesets API.

Files:

- `branch-naming-gitflow.json`
  - Restricts branch names to the agreed gitflow-style naming policy:
    `main`, `develop`, `feature/EXPKITS-*`, `bugfix/EXPKITS-*`, and
    `hotfix/EXPKITS-*`, plus `sandbox/*` for temporary sandbox branches.
- `protect-main-and-develop.json`
  - Protects `main` and `develop` so updates must go through pull requests.
  - Requires reviews and disallows squash merges on those branches.

Notes:

- These JSON files are source-of-truth drafts; GitHub does not apply them
  automatically just because they exist in the repository unless
  `.github/workflows/sync-rulesets.yml` is enabled and has the required token.
- The contributor-facing branching workflow is documented in
  `docs/public/branching-policy.md`.
- Branch-name enforcement should come from the GitHub ruleset once it is
  imported or applied; repository workflows do not need to duplicate it.
- Required status checks are intentionally not included yet because the exact
  check names should be taken from the live repository once the desired checks
  are finalized.
- The sync workflow expects `RULESET_ADMIN_GITHUB_TOKEN`, backed by a GitHub
  App installation token or fine-grained PAT with repository administration
  write access.

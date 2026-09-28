# Contributing

## Contribution rules

- Format code according to the repository configuration. Use `opk-ci --pre-commit-check` to run the shared pre-commit checks manually.
- Name branches with a ticketed `feature/`, `bugfix/`, `hotfix/`, or `release/`
  prefix, for example `release/EXPKITS-1234-create-release-1.2.3`.
- Branch from `main` and open the pull request into `main`. Update feature
  branches by rebasing onto their target branch. In a native PR stack, upper
  PRs target the branch immediately below them.
- Use this commit message structure:
  - first line: short description
  - second line: `Task: EXPKITS-1234`
  - remaining lines: optional details
- Sign every commit and include the DCO `Signed-off-by` trailer using the
  configured contributor identity (`git commit -S -s`). Keep signing enabled
  during rebases and cherry-picks, and verify both requirements after rewrites.
- Use a Conventional Commit pull request title, for example
  `ci: EXPKITS-1234 simplify CI workflows`. Titles are validated for pull requests targeting `main`.
- Fill out the pull request template with `Goal`, `Change`, and `Testing`.
- Bump the product version only when preparing a release. Merge the preparation
  PR into `main`, then start the manual Release workflow. Do not overwrite
  published versions.
- Follow the [branching policy](../docs/public/branching-policy.md) and
  [release process](../docs/arch/release-process.md) for the complete flow.

## Documentation

- Keep public docs focused on usage, observable behavior, and public contracts.
- Describe component responsibilities, boundaries, and lifecycle in architecture docs.
- Include implementation details only when they explain behavior or constraints;
  keep internal helper descriptions and code walkthroughs in source comments.
- Keep explanations concise. Document shared behavior once and link to it.

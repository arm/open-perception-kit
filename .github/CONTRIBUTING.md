# Contributing

## Contribution rules

- Format code according to the repository configuration. Use `opk-ci --pre-commit-check` to run the shared pre-commit checks manually.
- Name branches with a ticketed `feature/`, `bugfix/`, `hotfix/`, or `release/`
  prefix, for example `release/EXPKITS-1234-create-release-1.2.3`.
- Use this commit message structure:
  - first line: short description
  - second line: `Task: EXPKITS-1234`
  - remaining lines: optional details
- Use a Conventional Commit pull request title, for example
  `ci: EXPKITS-1234 simplify CI workflows`. Titles are validated for pull requests targeting `main`.
- Fill out the pull request template with `Goal`, `Change`, and `Testing`.
- A pull request targeting `main` must set a new stable version in
  `development/meson.build` and add the matching non-empty `CHANGELOG.md`
  section. Published `v<MAJOR.MINOR.PATCH>` assets are immutable.

## Documentation

- Keep public docs focused on usage, observable behavior, and public contracts.
- Describe component responsibilities, boundaries, and lifecycle in architecture docs.
- Include implementation details only when they explain behavior or constraints;
  keep internal helper descriptions and code walkthroughs in source comments.
- Keep explanations concise. Document shared behavior once and link to it.

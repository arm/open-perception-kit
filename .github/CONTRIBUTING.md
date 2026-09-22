# Contributing

## Contribution rules

- Format code according to the repository configuration. Use `opk-ci --pre-commit-check` to run the shared pre-commit checks manually.
- Name branches with a ticketed `feature/`, `bugfix/`, `hotfix/`, or `release/`
  prefix, for example `release/EXPKITS-1234-create-release-1.2.3`.
- Use this commit message structure:
  - first line: short description
  - second line: `Task: EXPKITS-1234`
  - remaining lines: optional details
- Use a pull request title in the form `EXPKITS-1234: short summary`.
- Fill out the pull request template with `Goal`, `Change`, and `Testing`.
- For pull requests, CI can be rerun by adding the `run-opk-ci` label.
- A pull request targeting `main` must set a new stable version in
  `development/meson.build` and add the matching non-empty `CHANGELOG.md`
  section. Published `v<MAJOR.MINOR.PATCH>` assets are immutable.

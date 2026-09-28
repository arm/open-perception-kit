<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->

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

## Copyright and licence notices

Use the existing `tools/templates/header/` templates for new OPK source files.
`opk-ci --license-header-check` checks native, Python, shell, CMake and browser
source headers; `--license-header` adds missing headers using the current year.
The browser assets under `development/web/content/vendor/` retain upstream notices.

When modifying a file, preserve its existing copyright holders and contribution
years. Add the year of the new contribution; use a range only for consecutive
years (for example, `2022-2024, 2026`). A rename alone does not add a copyright
year. Do not update every file just because the calendar year changed.

OPK uses `SPDX-FileCopyrightText` and `SPDX-License-Identifier` notices, with
`Arm Limited and/or its affiliates <perception-fdbck@arm.com>` as the Arm holder
and contact. Keep each SPDX notice on one line. For formats that cannot carry
comments, follow the existing `.license` sidecars and `REUSE.toml` annotations
described in [Licensing](../docs/public/licensing.md). Edit generator inputs
and regenerate generated files through their existing owner scripts.

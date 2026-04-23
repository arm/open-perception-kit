# Pull Request

<!-- Fix the link to the latest GitHub Actions run for this PR below. -->
[![AMP CI Pipeline](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/amp-ci.yml/badge.svg?branch=feature/EXPKITS-/TODO)](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/amp-ci.yml)

## PR rules

- To rerun CI, add the `run-amp-ci` label. To retrigger again, remove it and add it again.
- Format code according to the repository configuration. The `expkits-ci` tool should help with this.
- Name branches as `feature/EXPKITS-xxxx/any-descriptive-string`.
- Use this commit message structure:
	- first line: short description
	- second line: `Task: EXPKITS-xxxx`
	- remaining lines: optional details
- Example:
  - Branch: `feature/EXPKITS-1234/update-pr-template`
  - Commit message:
    - `Update PR template rules`
    - `Task: EXPKITS-1234`
    - `Add contribution examples and clarify CI rerun instructions.`

## Description

<!-- Please provide a summary of the changes and the related issue. -->

## Developer checklist

- [ ] I have tested these changes locally.
- [ ] I have implemented tests where applicable, and all of the tests are passing.
- [ ] I have updated the documentation.
- [ ] My implementation follows the agreed upon coding guidelines.

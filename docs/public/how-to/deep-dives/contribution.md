---
sidebar_position: 33
sidebar_label: Contribution
---

# Contribution

This page describes the normal contribution flow for AMP Development Forge.

## Before you start

- Work inside the repository container workflow whenever possible.
- Build and test locally before opening a pull request.
- Update documentation when your change affects user flow, architecture, or configuration.

For the main repository rules, see the files in the repository root:
- `.github/CONTRIBUTING.md`
- `.github/PULL_REQUEST_TEMPLATE.md`

## Recommended contribution flow

1. create a branch from `main`
2. implement and test the change
3. run formatting and quality checks
4. update documentation if needed
5. open a pull request

## Branch naming

Branches are expected to follow this format:

```text
feature/EXPKITS-xxxx/any-descriptive-string
```

Example:

```text
feature/EXPKITS-1234/update-tracking-docs
```

## Commit message format

The repository expects this structure:

```text
Short description
Task: EXPKITS-xxxx
Optional extra details
```

Example:

```text
Update tracking documentation
Task: EXPKITS-1234
Clarify tracker inputs and model integration notes.
```

## Local checks

At minimum, contributors should:
- build the project
- run relevant tests
- run formatting and quality checks

Useful commands are already documented in [How-To](index.md). The repository also provides `expkits-ci` and pre-commit hooks for routine checks.

## Pull request expectations

When opening a PR:
- describe what changed and why
- mention the related task
- check the developer checklist in the PR template
- make sure documentation is updated when applicable

If CI needs to be retriggered, add the `run-amp-ci` label. If it was already present, remove it and add it again.

## What to update for common changes

- model integration changes: update the matching files under `config/models/` and `config/opchains/`
- pipeline changes: update the matching files under `config/pipelines/`
- user-facing workflow changes: update the relevant docs under `docs/public/how-to/`
- C++ or GStreamer behavior changes: update the relevant docs under `docs/public/arch/`
- contribution workflow changes: update `.github/CONTRIBUTING.md` and `.github/PULL_REQUEST_TEMPLATE.md`

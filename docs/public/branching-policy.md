---
sidebar_position: 12
sidebar_label: Branching Policy
---

# Branching Policy

This repository uses a simplified gitflow-style branching model.

The goals are:

- keep active development on `develop`
- keep releasable history on `main`
- separate new work, normal fixes, and post-release hotfixes
- enforce the naming and protection rules through manually maintained GitHub
  rulesets

## Allowed branch names

The branch naming ruleset allows these branch families:

- `main`
- `develop`
- `feature/EXPKITS-<integer>...`
- `bugfix/EXPKITS-<integer>...`
- `hotfix/EXPKITS-<integer>...`
- `dependabot/<name>...`
- `sandbox/<name>...`
Examples:

- `feature/EXPKITS-1234/add-camera-contact-parser`
- `bugfix/EXPKITS-5678/fix-ui-timeout`
- `hotfix/EXPKITS-9012/fix-release-crash`
- `sandbox/agent-review-smoke`

## Branch purpose

Use each branch type for a specific kind of work.

### `main`

`main` is the protected release branch.

- Changes reach `main` through pull requests only.
- `main` should always represent the current release line.

### `develop`

`develop` is the protected integration branch.

- Normal day-to-day work merges into `develop`.
- `develop` is the source branch for new features and normal bug fixes.

### `feature/EXPKITS-*`

Use `feature/*` for planned development work tied to a task.

- Branch from `develop`
- Open the pull request back into `develop`

### `bugfix/EXPKITS-*`

Use `bugfix/*` for normal fixes discovered during ongoing development.

- Branch from `develop`
- Open the pull request back into `develop`

### `hotfix/EXPKITS-*`

Use `hotfix/*` only for urgent fixes to something already released on `main`.

- Branch from `main`
- Open a pull request into `main`
- After the fix reaches `main`, merge the same change back into `develop`

### `sandbox/*`

Use `sandbox/*` for temporary CI, workflow, or integration experiments that
still need repository automation to run.

- Branch from `develop` unless the experiment requires a different base
- Do not treat `sandbox/*` as a long-lived branch family
- Open the pull request into the branch that matches the experiment goal

## Normal flow

The normal development flow is:

1. branch from `develop` using `feature/*` or `bugfix/*`
2. implement the change
3. open a pull request into `develop`
4. when the integrated work is ready to ship, open a pull request from `develop` into `main`

## Hotfix flow

The post-release hotfix flow is:

1. branch from `main` using `hotfix/*`
2. implement the urgent fix
3. open a pull request into `main`
4. after it is merged, back-merge the fix into `develop`

This keeps `main` stable while preventing hotfix-only drift between the release
line and ongoing development.

## No release branches

This repository does not use `release/*` branches.

That is intentional. The expected release procedure is linear, with one active
release line at a time. If the project later needs multiple maintained release
lines in parallel, this policy can be revisited.

## Enforcement

GitHub rulesets are maintained manually in the repository settings.

The branch naming ruleset applies to all branches and allows names matching:

```text
^(main|develop|feature/EXPKITS-[0-9]+.*|bugfix/EXPKITS-[0-9]+.*|hotfix/EXPKITS-[0-9]+.*|dependabot/.+|sandbox/.+)$
```

The branch protection ruleset applies to `main` and `develop` and:

- prevents branch deletion and force pushes
- requires changes to arrive through pull requests
- requires one approval, approval after the latest push, and resolution of all
  review threads
- dismisses stale approvals when new commits are pushed
- permits merge commits only
- does not require code-owner review

No bypass actors or required status checks are configured. Repository
administrators must keep the GitHub settings aligned with this policy.

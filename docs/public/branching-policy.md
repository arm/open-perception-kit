---
sidebar_position: 12
sidebar_label: Branching Policy
---

# Branching Policy

`main` is the default and integration branch. Development, fixes, and release
preparation use pull requests into `main`.

## Branch names and purpose

Use a ticketed branch name for contributor work:

| Branch family | Purpose | Example |
| --- | --- | --- |
| `feature/EXPKITS-*` | Planned development | `feature/EXPKITS-1234/add-camera-contact-parser` |
| `bugfix/EXPKITS-*` | Normal fixes | `bugfix/EXPKITS-5678/fix-ui-timeout` |
| `hotfix/EXPKITS-*` | Urgent fixes to released behavior | `hotfix/EXPKITS-9012/fix-release-crash` |
| `release/EXPKITS-*` | Release preparation | `release/EXPKITS-1234-create-release-1.2.3` |

Dependabot manages its own `dependabot/*` branches.

## Development flow

1. Create a ticketed branch from the current `main`.
2. Implement and validate the change using the repository's checks.
3. Open a pull request into `main` with a Conventional Commit title and the
   `Goal`, `Change`, and `Testing` sections from the PR template.
4. Rebase onto the target branch when an update is needed. Do not merge `main`
   into the feature branch.
5. Resolve review threads, obtain the required approvals, and pass the required
   checks before squash-merging.

Sign every commit and include a DCO `Signed-off-by` trailer. Use
`git commit -S -s`, `git rebase --gpg-sign --signoff`, and `git cherry-pick -S -s`.
Verify both the signature and sign-off after rewriting commits.

Hotfixes follow the same flow. In a native PR stack, the bottom PR targets `main`
and each upper PR targets the branch below it. Rebase when the base changes
and merge from bottom to top.

## Release flow

Ordinary PRs do not require a version bump or publish a release.

To release, prepare the version, changelog, and generated version consumers on
a ticketed `release/*` branch. Merge the PR into `main`, then manually start the
**Release** workflow from `main`. It publishes the committed version.
Do not overwrite published versions.

See the repository's
[release process](https://github.com/arm/open-perception-kit/blob/main/docs/arch/release-process.md#release-packages)
for preparation commands, artifacts, checks, and publication order.

## Enforcement

The active `main` ruleset targets the default branch. It requires:

- pull requests with one approval, code-owner review for owned paths, and all
  review threads resolved;
- dismissal of stale approvals after new commits, approval of the latest push
  by someone other than its author, and extra approval for unattributed changes;
- required status checks to pass with the branch up to date;
- signed commits and a DCO sign-off in commit messages;
- linear history and squash merges, with branch creation, deletion, and force
  pushes restricted on the protected ref.

Repository administrators have a configured bypass. GitHub settings are
maintained manually; inspect the
[current ruleset](https://github.com/arm/open-perception-kit/rules/23838289)
for the required check names and current settings.

# Ponytail Review

Apply this decision ladder before you change anything:

1. Delete before you add: can the fix be achieved by removing or simplifying code?
2. Reuse what is already in the repo before introducing new files, helpers, or abstractions.
3. Prefer standard library, built-in GitHub Actions features, and existing repo utilities over new dependencies.
4. Keep the diff narrow: touch only the files directly implicated by the regression.
5. Keep the runtime surface small: avoid new secrets, permissions, services, or background complexity unless required.
6. After the fix works, shrink it again: remove any step, branch, or explanation that is not load-bearing.

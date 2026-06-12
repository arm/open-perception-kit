# Repository Review Instructions

Review the pull request for real bugs, regressions, risky behavior changes, and
missing validation that are supported by the changed code.

Focus on:

- correctness and runtime behavior
- merge or release risk
- configuration or CI regressions
- security-sensitive changes
- mismatches between code, config, and documentation

Do not focus on:

- formatting-only issues
- naming preferences
- speculative refactors without a concrete risk

When a finding is uncertain, say so clearly.

Prefer fewer, higher-confidence findings over broad speculation.

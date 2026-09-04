---
name: validate-valgrind-regressions
description: >
  Use this skill when diagnosing Valgrind CI failures, changing C++ runtime code,
  or editing scripts/testing/valgrind/suppressed-warnings in this repository.
  Reproduce the CI environment, compare normalized results with the current target
  branch, fix repository-owned stacks, and keep only exercised third-party-only
  suppressions.
---

# Validate Valgrind regressions

Use the repository's CI path to distinguish new repository errors from noisy raw
Valgrind output before changing code or suppressions.

**Failure pattern:** Generated suppressions or raw error totals make pre-existing
GStreamer scanner output look like a feature regression and hide stacks that pass
through repository-developed functions.

**Verified by:** The EXPKITS-1282 feature and current target summaries each
contained 36 repository errors (`new=0`, `fixed=0`), and summary generation passed
with all 218 remaining suppressions exercised.

## Procedure

1. Read `.github/compose.ci.yaml`, the Valgrind job in
   `.github/workflows/pek-ci.yml`, and the scripts under
   `scripts/testing/valgrind/`. Reuse their pipeline, summary, and comparison
   commands.

2. Use a cached CI image when the host lacks Valgrind or runtime dependencies.
   Select the image used by the relevant CI run, then use it consistently for the
   feature and target runs:

   ```bash
   docker image ls --format '{{.Repository}}:{{.Tag}}' | rg 'amp-dev-forge-ci|pek-ci'
   export PEK_CI_IMAGE='<selected-cached-image>'
   ```

3. In the feature checkout, extract the target branch's suppression file. Use it
   for the diagnostic feature run so suppression churn cannot masquerade as a
   code regression:

   ```bash
   target_ref=origin/develop
   mkdir -p .cache/valgrind-analysis
   git show "$target_ref:scripts/testing/valgrind/suppressed-warnings" \
     > .cache/valgrind-analysis/target-suppressions
   find scripts/testing/valgrind/logs -name '*.valgrind.*.xml' -delete 2>/dev/null || true
   docker compose -f .github/compose.ci.yaml run --rm --no-deps \
     pek-valgrind-check bash -c '
       .devcontainer/setup.sh &&
       .devcontainer/platform_init.sh pek-dev disabled &&
       ./scripts/testing/valgrind/test-elements-with-valgrind.sh clean \
         --pipeline only-onnx-yolo \
         --suppressions-file /work/.cache/valgrind-analysis/target-suppressions
     '
   python3 scripts/testing/valgrind/summarize-valgrind-output.py \
     --logs-dir scripts/testing/valgrind/logs \
     --output /tmp/feature-valgrind-summary.xml
   ```

   Omit `--suppressions-file` only for this diagnostic summary: the target list
   may contain obsolete suppressions, and that should not prevent comparison.

4. Create or reuse an isolated clean checkout at the current target branch head.
   Run the same image, focused runner, and diagnostic summary there, using the
   target checkout's suppression file, and write its result to
   `/tmp/target-valgrind-summary.xml`. Do not compare against an old local target
   checkout or logs produced with different dependencies.

5. Compare normalized repository-owned errors, not raw counts:

   ```bash
   python3 scripts/testing/valgrind/compare-valgrind-results.py \
     --baseline /tmp/target-valgrind-summary.xml \
     --current /tmp/feature-valgrind-summary.xml
   ```

6. For every reported `NEW` record, inspect every `<stack>` and every `<frame>` in
   the raw XML. Use `<dir>`, `<file>`, `<obj>`, and `<fn>`, plus `rg` in the source
   tree when ownership is unclear, to decide whether any frame is a function
   developed in this repository.

   - If any frame is repository-developed, fix that code and rerun both summary
     and comparison. Do not suppress the warning, even when the allocation starts
     in GLib, GStreamer, ONNX Runtime, or another dependency.
   - Add a suppression only when the complete stack contains no repository-
     developed function. Treat `--gen-suppressions` output as a candidate that
     still requires this review.

7. After each fix, keep `scripts/testing/valgrind/suppressed-warnings` minimal.
   Clear the XML logs and run the default services against the proposed feature
   suppression file:

   ```bash
   find scripts/testing/valgrind/logs -name '*.valgrind.*.xml' -delete 2>/dev/null || true
   docker compose -f .github/compose.ci.yaml run --rm --no-deps pek-valgrind-check
   docker compose -f .github/compose.ci.yaml run --rm --no-deps pek-generate-valgrind-summary
   ```

   Then:

   - remove a suppression when its warning is fixed or no longer exercised;
   - rerun `pek-generate-valgrind-summary` until it reports that every remaining
     suppression was used;
   - rerun the comparison until it reports no new repository errors.

8. Run the focused tooling tests before committing:

   ```bash
   cd scripts/testing/valgrind
   python3 -m unittest \
     test_valgrind_baseline_artifact.py \
     test_compare_valgrind_results.py \
     test_summarize_valgrind_output.py
   ```

## Worked example

One feature run produced 46,374 raw error records while the target produced
46,376. After normalization and repository filtering, the comparison was:

```text
Baseline errors : 36
Current errors  : 36
New errors      : 0
Fixed errors    : 0
PASSED: No new Valgrind errors compared to the baseline.
```

Twenty-one proposed PEKinfer scanner suppressions were rejected because their
stacks included repository functions such as `gst_pekinfer_class_init`,
`gst_pekinfer_get_type`, or `pekinfer_plugin_init`. Nine unused PEKOSD
suppressions were removed. The final summary check reported:

```text
PASSED: All 218 Valgrind suppressions were used.
```

## Gotchas

- `test-elements-with-valgrind.sh` can finish after reporting Valgrind error
  records; its raw total is not the regression verdict. The normalized target
  comparison is authoritative.
- A stack is not third-party-only merely because its top frames are in GLib or
  GStreamer. Review the entire stack, including plugin registration callbacks.
- Generate target and feature results with the same CI image, pipeline, frame
  count, and suppression baseline. Otherwise fingerprint differences are not
  attributable to the source change.
- Keep raw logs from the two checkouts separate; stale XML files change summary
  inputs and suppression-use accounting.

## What didn't work

- Running the host script directly failed when host Valgrind and ONNX dependencies
  were absent. Installing ad hoc host packages would diverge from CI; the cached
  `pek-valgrind-check` image is the reproducible route.
- Comparing raw totals suggested thousands of warnings and obscured that there
  were zero new normalized repository errors.
- Copying `--gen-suppressions` output blindly hid scanner stacks containing
  repository plugin functions and violated the suppression policy.

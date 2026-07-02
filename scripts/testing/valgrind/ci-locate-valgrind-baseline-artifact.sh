#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

baseline_branch="${VALGRIND_BASELINE_BRANCH:-main}"
workflow_name="${VALGRIND_BASELINE_WORKFLOW:-valgrind.yml}"
artifact_name="${VALGRIND_BASELINE_ARTIFACT:-valgrind-baseline}"

baseline_sha="$(
    gh api "repos/${GITHUB_REPOSITORY}/git/ref/heads/${baseline_branch}" \
        --jq '.object.sha'
)"

# <codex-review:suppress-begin>
# "Baseline lookup hard-fails when HEAD artifact is missing instead of falling back to latest valid baseline"
# That is the intended way of operation. If HEAD artifact is missing for the main, then the creation shall be triggered manually.
run_ids="$(
    gh run list \
        --repo "${GITHUB_REPOSITORY}" \
        --workflow "${workflow_name}" \
        --branch "${baseline_branch}" \
        --commit "${baseline_sha}" \
        --status success \
        --limit 20 \
        --json databaseId \
        --jq '.[].databaseId'
)"

for run_id in ${run_ids}; do
    artifact_id="$(
        gh api "repos/${GITHUB_REPOSITORY}/actions/runs/${run_id}/artifacts" \
            --jq "[.artifacts[] | select(.name == \"${artifact_name}\" and .expired == false) | .id][0] // \"\""
    )"

    if [ -n "${artifact_id}" ]; then
        echo "Using ${artifact_name} artifact from run ${run_id} at ${baseline_branch} ${baseline_sha}."
        echo "run-id=${run_id}" >> "${GITHUB_OUTPUT}"
        exit 0
    fi
done

echo "No available ${artifact_name} artifact found on ${baseline_branch} at ${baseline_sha}." >&2
echo "Run ${workflow_name} manually on the current ${baseline_branch} tip to publish a new baseline artifact." >&2
exit 1
# <codex-review:suppress-end>

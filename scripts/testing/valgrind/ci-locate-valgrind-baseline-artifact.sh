#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

baseline_branch="${VALGRIND_BASELINE_BRANCH:-main}"
workflow_name="${VALGRIND_BASELINE_WORKFLOW:-valgrind.yml}"
artifact_name="${VALGRIND_BASELINE_ARTIFACT:-valgrind-baseline}"
max_attempts="${VALGRIND_BASELINE_WAIT_ATTEMPTS:-90}"
sleep_seconds="${VALGRIND_BASELINE_WAIT_SECONDS:-60}"

find_valgrind_baseline_run() {
    local candidate_run_ids
    candidate_run_ids="$(
        gh run list \
            --workflow "${workflow_name}" \
            --branch "${baseline_branch}" \
            --status success \
            --limit 20 \
            --json databaseId \
            --jq ".[].databaseId"
    )"

    local candidate_run_id
    for candidate_run_id in ${candidate_run_ids}; do
        if has_available_baseline_artifact "${candidate_run_id}"; then
            echo "${candidate_run_id}"
            return 0
        fi
    done

    return 1
}

has_available_baseline_artifact() {
    local run_id="$1"
    local artifact_id
    artifact_id="$(
        gh api "repos/${GITHUB_REPOSITORY}/actions/runs/${run_id}/artifacts" \
            --jq "[.artifacts[] | select(.name == \"${artifact_name}\" and .expired == false) | .id][0] // \"\""
    )"

    [ -n "${artifact_id}" ]
}

latest_valgrind_run_status() {
    gh run list \
        --workflow "${workflow_name}" \
        --branch "${baseline_branch}" \
        --limit 1 \
        --json databaseId,status,conclusion \
        --jq '.[0] | "run \(.databaseId): \(.status) / \(.conclusion)"'
}

wait_for_dispatched_baseline() {
    local run_id
    local attempt

    for attempt in $(seq 1 "${max_attempts}"); do
        sleep "${sleep_seconds}"
        run_id="$(find_valgrind_baseline_run || true)"
        if [ -n "${run_id}" ] && [ "${run_id}" != "null" ]; then
            echo "${run_id}"
            return 0
        fi

        echo "Waiting for ${artifact_name} artifact (${attempt}/${max_attempts}); latest $(latest_valgrind_run_status)."
    done

    return 1
}

run_id="$(find_valgrind_baseline_run || true)"

if [ -z "${run_id}" ] || [ "${run_id}" = "null" ]; then
    echo "No available ${artifact_name} artifact found on ${baseline_branch}; dispatching ${workflow_name}."
    gh workflow run "${workflow_name}" --ref "${baseline_branch}"
    run_id="$(wait_for_dispatched_baseline || true)"
fi

if [ -z "${run_id}" ] || [ "${run_id}" = "null" ]; then
    echo "No ${artifact_name} artifact became available on ${baseline_branch}." >&2
    exit 1
fi

echo "Using ${artifact_name} artifact from run ${run_id}."
echo "run-id=${run_id}" >> "${GITHUB_OUTPUT}"

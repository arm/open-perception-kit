#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Runs PEK browser smoke tests against local-data pipelines.
################################################################

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../../pre-commit/common.sh"

usage() {
    cat << 'EOF'
Usage:
  ./scripts/playwright/browser-smoke/run.sh [--sink-only|-h|--help]

Prerequisites:
  ./scripts/quick-start/start-container.sh --recreate
  ./scripts/build.sh

Environment:
  PLAYWRIGHT_BASE_URL  PEK browser URL. Default: http://127.0.0.1:9999
  BROWSER_SMOKE_BROWSERS  Comma-separated browser list. Default: chromium
  BROWSER_SMOKE_IMAGE_NAME  Runtime image tag override.
  BROWSER_SMOKE_REBUILD=1  Force rebuild of the Playwright runtime image.
  NUM_FRAMES           Frames served by the local-data pipelines. Default: 12000
  STOCK_VIDEO_LOOP_TIMEOUT_MS  Maximum wait for the stock video to loop. Default: 900000
EOF
}

TEST_SET="full"
case "${1:-}" in
    -h | --help)
        usage
        exit 0
        ;;
    --sink-only)
        TEST_SET="sink-only"
        ;;
    "") ;;
    *)
        echo "Error: unknown argument '${1}'" >&2
        usage >&2
        exit 2
        ;;
esac

REPO_ROOT="$(repo_checks_resolve_repo_root "${SCRIPT_DIR}")"
RUNTIME_DOCKERFILE="${SCRIPT_DIR}/Dockerfile"

PLAYWRIGHT_VERSION="1.61.0"
PLAYWRIGHT_BASE_URL="${PLAYWRIGHT_BASE_URL:-http://127.0.0.1:9999}"
BROWSER_SMOKE_BROWSERS="${BROWSER_SMOKE_BROWSERS:-chromium}"
NUM_FRAMES="${NUM_FRAMES:-12000}"
STOCK_VIDEO_LOOP_TIMEOUT_MS="${STOCK_VIDEO_LOOP_TIMEOUT_MS:-900000}"
ACTIVE_PID_FILE=""
ACTIVE_PIPELINE_PID=""
PIPELINE_STOP_TIMEOUT_SECONDS=30
PIPELINE_KILL_TIMEOUT_SECONDS=10

cd "${REPO_ROOT}"

cleanup_active_pipeline() {
    if [ -n "${ACTIVE_PIPELINE_PID}" ]; then
        stop_pipeline "${ACTIVE_PID_FILE}" "${ACTIVE_PIPELINE_PID}"
        ACTIVE_PID_FILE=""
        ACTIVE_PIPELINE_PID=""
    fi
}

on_signal() {
    cleanup_active_pipeline
    exit 130
}

trap 'cleanup_active_pipeline; repo_checks_on_error "${LINENO}"' ERR
trap cleanup_active_pipeline EXIT
trap on_signal INT TERM

browser_smoke_image_name() {
    local repo_name=""

    if [ -n "${BROWSER_SMOKE_IMAGE_NAME:-}" ]; then
        printf '%s\n' "${BROWSER_SMOKE_IMAGE_NAME}"
        return
    fi

    if [ "${CI:-}" = "true" ]; then
        printf 'amp-dev-forge-browser-smoke:%s\n' "${PLAYWRIGHT_VERSION}"
        return
    fi

    repo_name="$(basename "${REPO_ROOT}")"
    repo_name="$(
        printf '%s' "${repo_name}" |
            tr '[:upper:]' '[:lower:]' |
            sed -E 's/[^a-z0-9]+/-/g; s/^-+//; s/-+$//'
    )"
    [ -n "${repo_name}" ] || repo_name="repo"

    printf '%s-browser-smoke:%s\n' "${repo_name}" "${PLAYWRIGHT_VERSION}"
}

build_browser_smoke_image_if_needed() {
    local image_name="$1"

    [ -f "${RUNTIME_DOCKERFILE}" ] ||
        repo_checks_die "Dockerfile not found: ${RUNTIME_DOCKERFILE}"

    if [ "${BROWSER_SMOKE_REBUILD:-}" != "1" ] &&
        docker image inspect "${image_name}" > /dev/null 2>&1; then
        return
    fi

    docker build \
        -f "${RUNTIME_DOCKERFILE}" \
        -t "${image_name}" \
        "${SCRIPT_DIR}"
}

repo_checks_check_docker_setup

eval "$("${REPO_ROOT}/scripts/quick-start/detect-environment.sh" --shell)"

if ! docker inspect -f '{{.State.Running}}' "${PEK_CONTAINER_NAME}" 2> /dev/null | grep -q '^true$'; then
    echo "Error: quick-start container is not running: ${PEK_CONTAINER_NAME}" >&2
    echo "Run ./scripts/quick-start/start-container.sh --recreate first." >&2
    exit 1
fi

if ! docker exec -u dev "${PEK_CONTAINER_NAME}" bash -lc 'test -x /work/tools/pek-menu' > /dev/null 2>&1; then
    echo "Error: /work/tools/pek-menu is missing in ${PEK_CONTAINER_NAME}." >&2
    echo "Run ./scripts/build.sh first." >&2
    exit 1
fi

rm -rf playwright-report test-results/playwright
mkdir -p test-results/playwright/blob-report

image_name="$(browser_smoke_image_name)"
build_browser_smoke_image_if_needed "${image_name}"

common_dir="$(repo_checks_git_common_dir "${REPO_ROOT}")"
mount_args=(-v "${REPO_ROOT}:${REPO_ROOT}")
case "${common_dir}" in
    "${REPO_ROOT}" | "${REPO_ROOT}"/*) ;;
    *)
        mount_args+=(-v "${common_dir}:${common_dir}")
        ;;
esac

run_phase() {
    local phase="$1"
    local pipeline="$2"
    local spec="$3"
    local browsers="${4:-${BROWSER_SMOKE_BROWSERS}}"
    local status=0
    local pid_file="/tmp/pek-browser-smoke-${phase}.pid"
    local pipeline_pid=""
    local docker_exec_args=(-u dev)

    echo "Running browser smoke phase: ${phase}"

    if [ -f "${REPO_ROOT}/devices.env" ]; then
        docker_exec_args+=(--env-file "${REPO_ROOT}/devices.env")
    fi
    docker exec "${docker_exec_args[@]}" \
        -e NUM_FRAMES="${NUM_FRAMES}" \
        -e BROWSER_SMOKE_PIPELINE="${pipeline}" \
        -e BROWSER_SMOKE_PID_FILE="${pid_file}" \
        "${PEK_CONTAINER_NAME}" \
        bash -lc 'printf "%s\n" "$$" > "$BROWSER_SMOKE_PID_FILE"; cd /work && exec /work/tools/pek-menu "$BROWSER_SMOKE_PIPELINE"' &
    pipeline_pid=$!
    ACTIVE_PID_FILE="${pid_file}"
    ACTIVE_PIPELINE_PID="${pipeline_pid}"
    if ! wait_for_pipeline_start "${phase}" "${pid_file}" "${pipeline_pid}"; then
        stop_pipeline "${pid_file}" "${pipeline_pid}"
        ACTIVE_PID_FILE=""
        ACTIVE_PIPELINE_PID=""
        return 1
    fi

    docker run --rm --ipc=host \
        --network "container:${PEK_CONTAINER_NAME}" \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -w "${REPO_ROOT}" \
        "${mount_args[@]}" \
        -e CI=true \
        -e PLAYWRIGHT_BASE_URL="${PLAYWRIGHT_BASE_URL}" \
        -e BROWSER_SMOKE_BROWSERS="${browsers}" \
        -e STOCK_VIDEO_LOOP_TIMEOUT_MS="${STOCK_VIDEO_LOOP_TIMEOUT_MS}" \
        -e PLAYWRIGHT_BLOB_OUTPUT_DIR="test-results/playwright/blob-report" \
        -e PLAYWRIGHT_BLOB_OUTPUT_NAME="${phase}.zip" \
        -e PWTEST_BLOB_DO_NOT_REMOVE=1 \
        "${image_name}" \
        playwright test -c tests/playwright/pek-browser-smoke.config.js \
        --reporter=line,blob \
        --output="test-results/playwright/${phase}" \
        "${spec}" || status=$?

    stop_pipeline "${pid_file}" "${pipeline_pid}"
    ACTIVE_PID_FILE=""
    ACTIVE_PIPELINE_PID=""

    return "${status}"
}

merge_reports() {
    local status=0

    docker run --rm \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -w "${REPO_ROOT}" \
        "${mount_args[@]}" \
        -e PLAYWRIGHT_HTML_OPEN=never \
        -e PLAYWRIGHT_HTML_OUTPUT_DIR=playwright-report \
        "${image_name}" \
        playwright merge-reports --reporter=html test-results/playwright/blob-report || status=$?

    if [ "${status}" -eq 0 ]; then
        rm -rf test-results/playwright/blob-report
    fi

    return "${status}"
}

stop_pipeline() {
    local pid_file="$1"
    local pipeline_pid="$2"
    local attempt=""

    docker exec -e BROWSER_SMOKE_PID_FILE="${pid_file}" "${PEK_CONTAINER_NAME}" bash -lc '
        pid="$(cat "$BROWSER_SMOKE_PID_FILE" 2> /dev/null || true)"
        if [ -n "$pid" ] && kill -0 "$pid" 2> /dev/null; then
            kill -INT "$pid"
        fi
    ' 2> /dev/null || true
    for attempt in $(seq 1 "${PIPELINE_STOP_TIMEOUT_SECONDS}"); do
        if ! kill -0 "${pipeline_pid}" 2> /dev/null; then
            wait "${pipeline_pid}" 2> /dev/null || true
            docker exec -e BROWSER_SMOKE_PID_FILE="${pid_file}" "${PEK_CONTAINER_NAME}" bash -lc '
                rm -f "$BROWSER_SMOKE_PID_FILE"
            ' 2> /dev/null || true
            return
        fi
        sleep 1
    done

    echo "Warning: pipeline did not stop after SIGINT; forcing shutdown." >&2
    docker exec -e BROWSER_SMOKE_PID_FILE="${pid_file}" "${PEK_CONTAINER_NAME}" bash -lc '
        pid="$(cat "$BROWSER_SMOKE_PID_FILE" 2> /dev/null || true)"
        if [ -n "$pid" ] && kill -0 "$pid" 2> /dev/null; then
            kill -TERM "$pid"
        fi
    ' 2> /dev/null || true
    for attempt in $(seq 1 "${PIPELINE_KILL_TIMEOUT_SECONDS}"); do
        if ! kill -0 "${pipeline_pid}" 2> /dev/null; then
            wait "${pipeline_pid}" 2> /dev/null || true
            docker exec -e BROWSER_SMOKE_PID_FILE="${pid_file}" "${PEK_CONTAINER_NAME}" bash -lc '
                rm -f "$BROWSER_SMOKE_PID_FILE"
            ' 2> /dev/null || true
            return
        fi
        sleep 1
    done

    docker exec -e BROWSER_SMOKE_PID_FILE="${pid_file}" "${PEK_CONTAINER_NAME}" bash -lc '
        pid="$(cat "$BROWSER_SMOKE_PID_FILE" 2> /dev/null || true)"
        if [ -n "$pid" ] && kill -0 "$pid" 2> /dev/null; then
            kill -KILL "$pid"
        fi
    ' 2> /dev/null || true
    kill -KILL "${pipeline_pid}" 2> /dev/null || true
    wait "${pipeline_pid}" 2> /dev/null || true
    docker exec -e BROWSER_SMOKE_PID_FILE="${pid_file}" "${PEK_CONTAINER_NAME}" bash -lc '
        rm -f "$BROWSER_SMOKE_PID_FILE"
    ' 2> /dev/null || true
}

wait_for_pipeline_start() {
    local phase="$1"
    local pid_file="$2"
    local host_pid="$3"
    local attempt=""

    for attempt in $(seq 1 30); do
        if ! kill -0 "${host_pid}" 2> /dev/null; then
            wait "${host_pid}" 2> /dev/null || true
            echo "Error: pipeline failed to start for phase: ${phase}" >&2
            return 1
        fi

        if docker exec -e BROWSER_SMOKE_PID_FILE="${pid_file}" "${PEK_CONTAINER_NAME}" bash -lc '
            pid="$(cat "$BROWSER_SMOKE_PID_FILE" 2> /dev/null || true)"
            [ -n "$pid" ] && kill -0 "$pid" 2> /dev/null
        ' > /dev/null 2>&1; then
            return
        fi

        sleep 1
    done

    echo "Error: timed out waiting for pipeline to start for phase: ${phase}" >&2
    return 1
}

browser_smoke_status=0
browser_smoke_browsers="$(
    printf '%s' "${BROWSER_SMOKE_BROWSERS}" |
        tr '[:upper:]' '[:lower:]' |
        tr ',' '\n' |
        sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' -e '/^$/d'
)"

[ -n "${browser_smoke_browsers}" ] || repo_checks_die "BROWSER_SMOKE_BROWSERS did not contain any browser."
while IFS= read -r browser; do
    case "${browser}" in
        chromium | firefox | webkit) ;;
        *) repo_checks_die "Unsupported BROWSER_SMOKE_BROWSERS entry: ${browser}" ;;
    esac
done <<< "${browser_smoke_browsers}"

if [ "${TEST_SET}" = "full" ]; then
    run_phase "stock-video-loop-chromium" \
        "config/pipelines/01-full-onnx.json" \
        "tests/playwright/pek-browser-loop.spec.js" \
        "chromium" || browser_smoke_status=$?
fi

while IFS= read -r browser; do
    run_phase "sink-only-${browser}" \
        "config/pipelines/testing/only-peksink.json" \
        "tests/playwright/pek-browser-sink.spec.js" \
        "${browser}" || browser_smoke_status=$?
done <<< "${browser_smoke_browsers}"

if [ "${TEST_SET}" = "full" ]; then
    while IFS= read -r browser; do
        run_phase "onnx-full-${browser}" \
            "config/pipelines/testing/onnx-full.json" \
            "tests/playwright/pek-browser-models.spec.js" \
            "${browser}" || browser_smoke_status=$?
    done <<< "${browser_smoke_browsers}"
fi

merge_reports || browser_smoke_status=$?

exit "${browser_smoke_status}"

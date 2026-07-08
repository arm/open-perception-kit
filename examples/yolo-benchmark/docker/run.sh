#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
COMPOSE_FILE="${SCRIPT_DIR}/compose.yaml"
IMAGE_NAME="${YOLO_BENCHMARK_IMAGE_NAME:-amp-dev-forge-yolo-benchmark:local}"

source "${REPO_ROOT}/scripts/pre-commit/common.sh"

usage() {
    cat << 'EOF'
Usage:
  examples/yolo-benchmark/docker/run.sh [bare|pek|compare|both]

Environment:
  YOLO_BENCHMARK_LIMIT           Optional image-list limit used only when the image list is missing.
  YOLO_BENCHMARK_IMAGE_NAME      Runtime image tag override.
  COMPOSE_PROJECT_NAME           Compose project override. Default: amp-dev-forge-yolo-benchmark
EOF
}

case "${1:-both}" in
    bare | pek | compare | both)
        command="${1:-both}"
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

repo_checks_check_docker_setup
if ! docker image inspect "${IMAGE_NAME}" > /dev/null 2>&1; then
    repo_checks_die \
        "YOLO benchmark image '${IMAGE_NAME}' is not built yet. Run ./examples/yolo-benchmark/docker/setup.sh first."
fi

cd "${REPO_ROOT}"
export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
export COMPOSE_PROJECT_NAME="${COMPOSE_PROJECT_NAME:-amp-dev-forge-yolo-benchmark}"

docker compose -f "${COMPOSE_FILE}" run --rm yolo-benchmark bash -s -- "${command}" << 'EOF'
set -Eeuo pipefail

command="$1"
artifact_root="/work/artifacts/yolo-benchmark"
image_list="${artifact_root}/images.tsv"
venv="${artifact_root}/.venv"
requirements="examples/yolo-benchmark/bare/requirements.txt"

cd /work
mkdir -p "${artifact_root}" tools
export ARTIFACT_ROOT="${artifact_root}"
export IMAGE_LIST="${image_list}"
export YOLO_CONFIG_DIR="${artifact_root}/.ultralytics"

prepare_limited_dataset_if_requested() {
    if [[ -n "${YOLO_BENCHMARK_LIMIT:-}" && ! -f "${IMAGE_LIST}" ]]; then
        python3 examples/yolo-benchmark/prepare_dataset.py \
            --coco-dir datasets/coco \
            --output "${IMAGE_LIST}" \
            --limit "${YOLO_BENCHMARK_LIMIT}"
    fi
}

ensure_bare_venv() {
    if [[ -x "${venv}/bin/python3" ]]; then
        # shellcheck source=/dev/null
        source "${venv}/bin/activate"
        return
    fi

    python3 -m venv "${venv}"
    # shellcheck source=/dev/null
    source "${venv}/bin/activate"
    python3 -m pip install --upgrade pip
    if [[ "$(uname -m)" == "x86_64" ]]; then
        python3 -m pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
    else
        python3 -m pip install torch torchvision
    fi
    python3 -m pip install -r "${requirements}"
}

run_bare() {
    ensure_bare_venv
    prepare_limited_dataset_if_requested
    LD_LIBRARY_PATH="" ./examples/yolo-benchmark/run.sh bare
}

run_pek() {
    export YOLO_BENCHMARK_BUILD_DIR="${artifact_root}/pek-build"
    ./examples/yolo-benchmark/pek/build.sh debug true
    prepare_limited_dataset_if_requested
    ./examples/yolo-benchmark/run.sh pek
}

case "${command}" in
    bare)
        run_bare
        ;;
    pek)
        run_pek
        ;;
    compare)
        ./examples/yolo-benchmark/run.sh compare
        ;;
    both)
        run_bare
        run_pek
        ./examples/yolo-benchmark/run.sh compare
        ;;
esac
EOF

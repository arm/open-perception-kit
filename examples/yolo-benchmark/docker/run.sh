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
  examples/yolo-benchmark/docker/run.sh [benchmark|bare|pek|compare|both]

Environment:
  YOLO_BENCHMARK_LIMIT           Optional image-list limit used by setup.sh.
  YOLO_BENCHMARK_IMAGE_NAME      Runtime image tag override.
  COMPOSE_PROJECT_NAME           Compose project override. Default: amp-dev-forge-yolo-benchmark
EOF
}

case "${1:-benchmark}" in
    benchmark | bare | pek | compare | both)
        command="${1:-benchmark}"
        ;;
    prepare)
        if [[ "${YOLO_BENCHMARK_SETUP:-}" != "1" ]]; then
            usage >&2
            exit 2
        fi
        command="prepare"
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
cache_root="/cache/yolo-benchmark"
image_list="${artifact_root}/images.tsv"
venv="${cache_root}/.venv"
dataset_dir="${cache_root}/coco"
pek_build_dir="${cache_root}/pek-build"
ultralytics_config_dir="${cache_root}/ultralytics"
requirements="examples/yolo-benchmark/bare/requirements.txt"

cd /work
if [[ ! -w "${cache_root}" ]]; then
    sudo chown -R "$(id -u):$(id -g)" "${cache_root}"
fi
mkdir -p "${artifact_root}" "${cache_root}" "${ultralytics_config_dir}/Ultralytics" tools
export ARTIFACT_ROOT="${artifact_root}"
export IMAGE_LIST="${image_list}"
export YOLO_CONFIG_DIR="${ultralytics_config_dir}"

prepare_dataset() {
    limit_args=()
    if [[ -n "${YOLO_BENCHMARK_LIMIT:-}" && "${YOLO_BENCHMARK_LIMIT}" != "0" ]]; then
        limit_args=(--limit "${YOLO_BENCHMARK_LIMIT}")
    fi
    python3 examples/yolo-benchmark/prepare_dataset.py \
        --coco-dir "${dataset_dir}" \
        --output "${IMAGE_LIST}" \
        "${limit_args[@]}"
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

activate_bare_venv() {
    if [[ ! -x "${venv}/bin/python3" ]]; then
        echo "Bare runner venv is missing. Run ./examples/yolo-benchmark/docker/setup.sh first." >&2
        exit 1
    fi
    # shellcheck source=/dev/null
    source "${venv}/bin/activate"
}

prepare_ultralytics_config() {
    activate_bare_venv
    # Keep first-run settings creation in setup, not benchmark logs.
    python3 - << 'PY'
import ultralytics
PY
}

require_image_list() {
    if [[ ! -f "${IMAGE_LIST}" ]]; then
        echo "Image list is missing. Run ./examples/yolo-benchmark/docker/setup.sh first." >&2
        exit 1
    fi
}

build_pek() {
    export YOLO_BENCHMARK_BUILD_DIR="${pek_build_dir}"
    ./examples/yolo-benchmark/pek/build.sh debug true
}

prepare() {
    ensure_bare_venv
    prepare_ultralytics_config
    prepare_dataset
    build_pek
}

benchmark_bare() {
    activate_bare_venv
    LD_LIBRARY_PATH="" ./examples/yolo-benchmark/run.sh bare
}

benchmark_pek() {
    if [[ ! -x examples/bin/yolo-benchmark ]]; then
        echo "PEK benchmark runner is missing. Run ./examples/yolo-benchmark/docker/setup.sh first." >&2
        exit 1
    fi
    ./examples/yolo-benchmark/run.sh pek
}

benchmark() {
    require_image_list
    benchmark_bare
    benchmark_pek
    ./examples/yolo-benchmark/run.sh compare
}

run_bare() {
    require_image_list
    benchmark_bare
}

run_pek() {
    require_image_list
    benchmark_pek
}

case "${command}" in
    prepare)
        prepare
        ;;
    benchmark)
        benchmark
        ;;
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
        benchmark
        ;;
esac
EOF

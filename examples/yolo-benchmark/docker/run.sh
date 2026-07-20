#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
COMPOSE_FILE="${SCRIPT_DIR}/compose.yaml"
IMAGE_NAME="${YOLO_BENCHMARK_IMAGE_NAME:-amp-dev-forge-yolo-benchmark:local}"

usage() {
    cat << 'EOF'
Usage:
  examples/yolo-benchmark/docker/run.sh setup|benchmark|summary|cleanup-ci

Runs the Dockerized YOLO benchmark workflow.

Environment:
  YOLO_BENCHMARK_LIMIT           Optional image-list limit. Default: full COCO val2017.
  YOLO_BENCHMARK_RUNS            Number of benchmark repetitions. Default: 1.
  YOLO_BENCHMARK_IMAGE_NAME      Runtime image tag override.
  YOLO_BENCHMARK_CACHE_VOLUME    Docker volume override for dataset, venv, and PEK build cache.
  COMPOSE_PROJECT_NAME           Compose project override. Default: amp-dev-forge-yolo-benchmark
EOF
}

case "${1:-}" in
    setup | benchmark | summary | cleanup-ci)
        command="$1"
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

is_uint() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

validate_inputs() {
    if [[ -n "${YOLO_BENCHMARK_LIMIT:-}" ]] && ! is_uint "${YOLO_BENCHMARK_LIMIT}"; then
        echo "YOLO_BENCHMARK_LIMIT must be a non-negative integer, got '${YOLO_BENCHMARK_LIMIT}'." >&2
        exit 2
    fi
    if [[ -n "${YOLO_BENCHMARK_RUNS:-}" ]]; then
        if ! is_uint "${YOLO_BENCHMARK_RUNS}" || (("${YOLO_BENCHMARK_RUNS}" < 1)); then
            echo "YOLO_BENCHMARK_RUNS must be a positive integer, got '${YOLO_BENCHMARK_RUNS}'." >&2
            exit 2
        fi
    fi
}

run_in_container() {
    local artifact_root="/work/artifacts/yolo-benchmark"
    local cache_root="/cache/yolo-benchmark"
    local image_list="${artifact_root}/images.tsv"
    local benchmark_runs="${YOLO_BENCHMARK_RUNS:-1}"
    local venv="${cache_root}/.venv"
    local dataset_dir="${cache_root}/coco"
    local pek_build_dir="${cache_root}/pek-build"
    local ultralytics_config_dir="${cache_root}/ultralytics"
    local requirements="examples/yolo-benchmark/bare/requirements.txt"
    local model="config/models/yolov11/yolo11n-fp32-320.onnx"
    local opchain="config/models/yolov11/opchain.json"

    cd /work
    python3 /work/scripts/private/initialize_models.py
    if [[ ! -w "${cache_root}" ]]; then
        sudo chown -R "$(id -u):$(id -g)" "${cache_root}"
    fi
    mkdir -p "${artifact_root}" "${cache_root}" "${ultralytics_config_dir}/Ultralytics"
    export YOLO_CONFIG_DIR="${ultralytics_config_dir}"

    ensure_bare_venv() {
        if [[ ! -x "${venv}/bin/python3" ]]; then
            python3 -m venv "${venv}"
        fi

        # shellcheck source=/dev/null
        source "${venv}/bin/activate"
        python3 -m pip install --upgrade pip
        python3 -m pip install \
            --index-url https://download.pytorch.org/whl/cpu \
            "torch==2.12.1+cpu" \
            "torchvision==0.27.1+cpu"
        python3 -m pip install -r "${requirements}"
    }

    activate_bare_venv() {
        if [[ ! -x "${venv}/bin/python3" ]]; then
            echo "Bare runner venv is missing. Run examples/yolo-benchmark/docker/run.sh setup first." >&2
            exit 1
        fi
        # shellcheck source=/dev/null
        source "${venv}/bin/activate"
    }

    prepare_dataset() {
        local limit_args=()
        if [[ -n "${YOLO_BENCHMARK_LIMIT:-}" && "${YOLO_BENCHMARK_LIMIT}" != "0" ]]; then
            limit_args=(--limit "${YOLO_BENCHMARK_LIMIT}")
        fi
        python3 examples/yolo-benchmark/prepare_dataset.py \
            --coco-dir "${dataset_dir}" \
            --output "${image_list}" \
            "${limit_args[@]}"
    }

    build_pek_runner() {
        ./scripts/build-elements.sh debug true
        if [[ -d "${pek_build_dir}/meson-private" ]]; then
            meson setup --reconfigure "${pek_build_dir}" examples/yolo-benchmark/pek
        else
            meson setup "${pek_build_dir}" examples/yolo-benchmark/pek
        fi
        meson compile -C "${pek_build_dir}"
        mkdir -p examples/bin
        cp "${pek_build_dir}/yolo-benchmark" examples/bin/yolo-benchmark
        chmod +x examples/bin/yolo-benchmark
    }

    setup() {
        ensure_bare_venv
        prepare_dataset
        build_pek_runner
    }

    validate_benchmark_runs() {
        if [[ -z "${benchmark_runs}" || "${benchmark_runs}" == *[!0-9]* ]]; then
            echo "YOLO_BENCHMARK_RUNS must be a positive integer, got '${benchmark_runs}'." >&2
            exit 2
        fi
        if ((benchmark_runs < 1)); then
            echo "YOLO_BENCHMARK_RUNS must be a positive integer, got '${benchmark_runs}'." >&2
            exit 2
        fi
    }

    require_setup() {
        if [[ ! -f "${image_list}" ]]; then
            echo "Image list is missing. Run examples/yolo-benchmark/docker/run.sh setup first." >&2
            exit 1
        fi
        if [[ ! -x examples/bin/yolo-benchmark ]]; then
            echo "PEK benchmark runner is missing. Run examples/yolo-benchmark/docker/run.sh setup first." >&2
            exit 1
        fi
        activate_bare_venv
    }

    benchmark_once() {
        local run_root="$1"
        rm -rf "${run_root}"
        mkdir -p "${run_root}"

        LD_LIBRARY_PATH="" python3 examples/yolo-benchmark/bare/benchmark.py \
            --model "${model}" \
            --images "${image_list}" \
            --output "${run_root}/bare/predictions.jsonl" \
            --summary "${run_root}/bare/benchmark_summary.json"

        ./examples/bin/yolo-benchmark \
            --opchain "${opchain}" \
            --images "${image_list}" \
            --output "${run_root}/pek/predictions.jsonl" \
            --summary "${run_root}/pek/benchmark_summary.json"

        python3 examples/yolo-benchmark/compare_benchmark_summaries.py \
            --bare-summary "${run_root}/bare/benchmark_summary.json" \
            --pek-summary "${run_root}/pek/benchmark_summary.json" \
            --output-json "${run_root}/comparison.json" \
            --output-md "${run_root}/comparison.md"
    }

    benchmark() {
        validate_benchmark_runs
        require_setup
        rm -rf "${artifact_root}/runs"
        for run_index in $(seq 1 "${benchmark_runs}"); do
            local run_name
            run_name="$(printf 'run-%02d' "${run_index}")"
            echo "YOLO benchmark ${run_index}/${benchmark_runs}: artifacts/yolo-benchmark/runs/${run_name}"
            benchmark_once "${artifact_root}/runs/${run_name}"
        done
    }

    if [[ "${command}" == "setup" ]]; then
        setup
    else
        benchmark
    fi
}

if [[ "${YOLO_BENCHMARK_IN_CONTAINER:-}" == "1" ]]; then
    if [[ "${command}" != "setup" && "${command}" != "benchmark" ]]; then
        echo "'${command}' is a host-only command." >&2
        exit 2
    fi
    validate_inputs
    run_in_container
    exit 0
fi

write_summary() {
    local summary_file="${GITHUB_STEP_SUMMARY:-/dev/stdout}"
    {
        echo "## YOLO Benchmark"
        echo
        echo "- image_limit: ${YOLO_BENCHMARK_LIMIT:-full}"
        echo "- benchmark_runs: ${YOLO_BENCHMARK_RUNS:-1}"
        echo "- image: ${YOLO_BENCHMARK_IMAGE_NAME:-${IMAGE_NAME}}"
        echo
        if compgen -G "artifacts/yolo-benchmark/runs/run-*/comparison.md" > /dev/null; then
            for comparison in artifacts/yolo-benchmark/runs/run-*/comparison.md; do
                echo "## $(basename "$(dirname "${comparison}")")"
                echo
                cat "${comparison}"
                echo
            done
        else
            echo "No comparison.md was generated."
        fi
    } >> "${summary_file}"
}

cleanup_ci() {
    set +e
    local checkout_path="${GITHUB_WORKSPACE:-}/${CI_CHECKOUT_PATH:-}"
    case "${checkout_path}" in
        "${GITHUB_WORKSPACE:-}/repo-"*) ;;
        *)
            echo "Skipping cleanup for unexpected checkout_path='${checkout_path}'" >&2
            return 0
            ;;
    esac
    if [[ -f "${checkout_path}/examples/yolo-benchmark/docker/compose.yaml" ]]; then
        docker compose -f "${checkout_path}/examples/yolo-benchmark/docker/compose.yaml" down --remove-orphans || true
    fi
    docker image rm -f "${YOLO_BENCHMARK_IMAGE_NAME:-${IMAGE_NAME}}" || true
    local checkout_root
    checkout_root="$(git -C "${checkout_path}" rev-parse --show-toplevel 2> /dev/null || true)"
    if [[ "${checkout_root}" != "${checkout_path}" ]]; then
        echo "Skipping checkout cleanup because '${checkout_path}' is not a git worktree root." >&2
        return 0
    fi
    rm -rf -- "${checkout_path}"
}

if [[ "${command}" == "summary" ]]; then
    cd "${REPO_ROOT}"
    write_summary
    exit 0
elif [[ "${command}" == "cleanup-ci" ]]; then
    cleanup_ci
    exit 0
fi

source "${REPO_ROOT}/scripts/pre-commit/common.sh"
repo_checks_check_docker_setup
validate_inputs

cd "${REPO_ROOT}"
export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
export COMPOSE_PROJECT_NAME="${COMPOSE_PROJECT_NAME:-amp-dev-forge-yolo-benchmark}"

if [[ "${command}" == "setup" ]]; then
    bash scripts/private/prepare-modelfetch-candidate.sh > /dev/null
    docker compose -f "${COMPOSE_FILE}" build yolo-benchmark
elif ! docker image inspect "${IMAGE_NAME}" > /dev/null 2>&1; then
    repo_checks_die \
        "YOLO benchmark image '${IMAGE_NAME}' is not built yet. Run examples/yolo-benchmark/docker/run.sh setup first."
fi

docker compose -f "${COMPOSE_FILE}" run --rm \
    -e YOLO_BENCHMARK_IN_CONTAINER=1 \
    yolo-benchmark \
    ./examples/yolo-benchmark/docker/run.sh "${command}"

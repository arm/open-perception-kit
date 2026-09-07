#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
: "${PEK_PROJECT_ROOT:=${REPO_ROOT}}"
PEK_PROJECT_ROOT="$(cd "${PEK_PROJECT_ROOT}" && pwd -P)"
export PEK_PROJECT_ROOT
REPO_ROOT="${PEK_PROJECT_ROOT}"
SCRIPT_DIR="${REPO_ROOT}/examples/yolo-benchmark/docker"
COMPOSE_FILE="${SCRIPT_DIR}/compose.yaml"
CACHE_COMPOSE_FILE="${SCRIPT_DIR}/compose.registry-cache.yaml"
IMAGE_NAME="${YOLO_BENCHMARK_IMAGE_NAME:-amp-dev-forge-yolo-benchmark:local}"
DEFAULT_BENCHMARK_RUNS=10

usage() {
    cat << 'EOF'
Usage:
  examples/yolo-benchmark/docker/run.sh setup|benchmark|summary|cleanup-ci

Runs the Dockerized YOLO benchmark workflow.

Environment:
  YOLO_BENCHMARK_KIND            Benchmark kind: images or video. Default: images.
  YOLO_BENCHMARK_LIMIT           Optional image-list limit. Default: full COCO val2017.
  YOLO_BENCHMARK_RUNS            Optional benchmark repetition override. Default: 10.
  YOLO_BENCHMARK_IMAGE_NAME      Runtime image tag override.
  YOLO_BENCHMARK_IMAGE_SHA       Expected revision of a prebuilt runtime image.
  YOLO_BENCHMARK_CACHE_FROM      Optional BuildKit cache source used after an image miss.
  YOLO_BENCHMARK_CACHE_VOLUME    Docker volume override for dataset, venv, and PEK build cache.
  PEK_PROJECT_ROOT               Absolute PEK checkout path. Default: checkout containing this script.
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
    if [[ "${YOLO_BENCHMARK_KIND:-images}" != "images" && "${YOLO_BENCHMARK_KIND:-images}" != "video" ]]; then
        echo "YOLO_BENCHMARK_KIND must be 'images' or 'video', got '${YOLO_BENCHMARK_KIND}'." >&2
        exit 2
    fi
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
    local artifact_root="${PEK_PROJECT_ROOT}/artifacts/yolo-benchmark"
    local cache_root="/cache/yolo-benchmark"
    local benchmark_kind="${YOLO_BENCHMARK_KIND:-images}"
    local image_list="${artifact_root}/images.tsv"
    local video="${cache_root}/media/mediapipe-object-detection.mp4"
    local video_manifest="${artifact_root}/video-source.json"
    local benchmark_runs="${YOLO_BENCHMARK_RUNS:-${DEFAULT_BENCHMARK_RUNS}}"
    local venv="${cache_root}/.venv"
    local dataset_dir="${cache_root}/coco"
    local pek_build_dir="${cache_root}/pek-build"
    local ccache_dir="${cache_root}/ccache"
    local ultralytics_config_dir="${cache_root}/ultralytics"
    local requirements="examples/yolo-benchmark/bare/requirements.txt"
    local model="config/models/yolov11/yolo11n-fp32-320.onnx"
    local opchain="config/models/yolov11/opchain.json"

    cd "${PEK_PROJECT_ROOT}"
    if [[ ! -w "${cache_root}" ]]; then
        sudo chown -R "$(id -u):$(id -g)" "${cache_root}"
    fi
    mkdir -p "${artifact_root}" "${cache_root}" "${ccache_dir}" "${ultralytics_config_dir}/Ultralytics"
    export CCACHE_DIR="${ccache_dir}"
    export GST_PLUGIN_PATH="${PEK_PROJECT_ROOT}/development/build-active/meson-out${GST_PLUGIN_PATH:+:${GST_PLUGIN_PATH}}"
    export LD_LIBRARY_PATH="${PEK_PROJECT_ROOT}/development/build-active/meson-out${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    export YOLO_CONFIG_DIR="${ultralytics_config_dir}"

    ensure_bare_venv() {
        if [[ ! -x "${venv}/bin/python3" ]]; then
            uv venv --seed "${venv}"
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

    prepare_video() {
        python3 examples/yolo-benchmark/prepare_video.py \
            --video "${video}" \
            --manifest "${video_manifest}"
    }

    build_pek_runner() {
        ./scripts/build.sh debug true
        if [[ -d "${pek_build_dir}/meson-private" ]]; then
            meson setup --reconfigure "${pek_build_dir}" examples/yolo-benchmark/pek
        else
            meson setup "${pek_build_dir}" examples/yolo-benchmark/pek
        fi
        meson compile -C "${pek_build_dir}"
        mkdir -p examples/bin
        cp "${pek_build_dir}/yolo-benchmark" examples/bin/yolo-benchmark
        cp "${pek_build_dir}/yolo-video-benchmark" examples/bin/yolo-video-benchmark
        chmod +x examples/bin/yolo-benchmark examples/bin/yolo-video-benchmark
    }

    setup() {
        ensure_bare_venv
        if [[ "${benchmark_kind}" == "video" ]]; then
            prepare_video
        else
            prepare_dataset
        fi
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
        if [[ "${benchmark_kind}" == "video" ]]; then
            if [[ ! -f "${video}" || ! -f "${video_manifest}" || ! -x examples/bin/yolo-video-benchmark ]]; then
                echo "Video benchmark setup is missing. Run YOLO_BENCHMARK_KIND=video examples/yolo-benchmark/docker/run.sh setup first." >&2
                exit 1
            fi
        else
            if [[ ! -f "${image_list}" || ! -x examples/bin/yolo-benchmark ]]; then
                echo "Image benchmark setup is missing. Run examples/yolo-benchmark/docker/run.sh setup first." >&2
                exit 1
            fi
        fi
        activate_bare_venv
    }

    benchmark_images_once() {
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

    run_bare_video() {
        local run_root="$1"
        LD_LIBRARY_PATH="" python3 examples/yolo-benchmark/bare/video_benchmark.py \
            --model "${model}" \
            --video "${video}" \
            --source-manifest "${video_manifest}" \
            --summary "${run_root}/bare/benchmark_summary.json"
    }

    run_pek_video() {
        local run_root="$1"
        ./examples/bin/yolo-video-benchmark \
            --opchain "${opchain}" \
            --video "${video}" \
            --source-manifest "${video_manifest}" \
            --summary "${run_root}/pek/benchmark_summary.json"
    }

    render_bare_detection_video() {
        local output="${artifact_root}/bare-detections.mp4"
        local temporary_dir="/tmp/bare-detection-render"
        local temporary="/tmp/bare-detections.mp4"
        local rendered="${temporary_dir}/prediction/$(basename "${video%.*}").avi"
        rm -rf "${temporary_dir}"
        rm -f "${output}" "${temporary}"
        LD_LIBRARY_PATH="" yolo detect predict \
            model="${model}" source="${video}" imgsz=320 device=cpu batch=1 vid_stride=1 \
            conf=0.25 save=true project="${temporary_dir}" name=prediction \
            exist_ok=true verbose=false
        test -s "${rendered}"
        gst-launch-1.0 -e -q \
            filesrc location="${rendered}" ! \
            decodebin ! videoconvert ! video/x-raw,format=I420 ! \
            x264enc speed-preset=ultrafast tune=zerolatency bitrate=6000 key-int-max=30 ! \
            h264parse ! mp4mux faststart=true ! filesink location="${temporary}"
        test -s "${temporary}"
        mv "${temporary}" "${output}"
        rm -rf "${temporary_dir}"
    }

    render_pek_detection_video() {
        local output="${artifact_root}/pek-detections.mp4"
        local temporary="/tmp/pek-detections.mp4"
        rm -f "${output}" "${temporary}"
        gst-launch-1.0 -e -q \
            filesrc location="${video}" ! \
            decodebin ! videoconvert ! video/x-raw,format=BGRA ! \
            pekinfer opchain-path="${opchain}" active=true ! \
            pekosd enabled=true ! videoconvert ! video/x-raw,format=I420 ! \
            x264enc speed-preset=ultrafast tune=zerolatency bitrate=6000 key-int-max=30 ! \
            h264parse ! mp4mux faststart=true ! filesink location="${temporary}"
        test -s "${temporary}"
        mv "${temporary}" "${output}"
    }

    benchmark_video_once() {
        local run_root="$1"
        local run_index="$2"
        rm -rf "${run_root}"
        mkdir -p "${run_root}"

        if ((run_index % 2 == 1)); then
            run_bare_video "${run_root}"
            run_pek_video "${run_root}"
        else
            run_pek_video "${run_root}"
            run_bare_video "${run_root}"
        fi

        python3 examples/yolo-benchmark/compare_video_benchmark_summaries.py \
            --bare-summary "${run_root}/bare/benchmark_summary.json" \
            --pek-summary "${run_root}/pek/benchmark_summary.json" \
            --output-json "${run_root}/comparison.json" \
            --output-md "${run_root}/comparison.md"
    }

    benchmark() {
        validate_benchmark_runs
        require_setup
        rm -rf "${artifact_root}/runs" "${artifact_root}/summary.json" "${artifact_root}/summary.md"
        for run_index in $(seq 1 "${benchmark_runs}"); do
            local run_name
            run_name="$(printf 'run-%02d' "${run_index}")"
            echo "YOLO ${benchmark_kind} benchmark ${run_index}/${benchmark_runs}: artifacts/yolo-benchmark/runs/${run_name}"
            if [[ "${benchmark_kind}" == "video" ]]; then
                benchmark_video_once "${artifact_root}/runs/${run_name}" "${run_index}"
            else
                benchmark_images_once "${artifact_root}/runs/${run_name}"
            fi
        done
        if [[ "${benchmark_kind}" == "video" ]]; then
            python3 examples/yolo-benchmark/compare_video_benchmark_summaries.py \
                --runs-root "${artifact_root}/runs" \
                --output-json "${artifact_root}/summary.json" \
                --output-md "${artifact_root}/summary.md"
            render_bare_detection_video
            render_pek_detection_video
        fi
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
        echo "- benchmark_kind: ${YOLO_BENCHMARK_KIND:-images}"
        echo "- image_limit: ${YOLO_BENCHMARK_LIMIT:-full}"
        echo "- benchmark_runs: ${YOLO_BENCHMARK_RUNS:-${DEFAULT_BENCHMARK_RUNS}}"
        echo "- container_image: ${YOLO_BENCHMARK_IMAGE_NAME:-${IMAGE_NAME}}"
        echo
        if [[ -f artifacts/yolo-benchmark/summary.md ]]; then
            cat artifacts/yolo-benchmark/summary.md
        elif compgen -G "artifacts/yolo-benchmark/runs/run-*/comparison.md" > /dev/null; then
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
    if [[ -n "${YOLO_BENCHMARK_IMAGE_SHA:-}" ]] && docker pull "${IMAGE_NAME}"; then
        test "$(docker image inspect --format '{{ index .Config.Labels "org.opencontainers.image.revision" }}' "${IMAGE_NAME}")" = "${YOLO_BENCHMARK_IMAGE_SHA}"
    else
        compose_files=(-f "${COMPOSE_FILE}")
        if [[ -n "${YOLO_BENCHMARK_CACHE_FROM:-}" ]]; then
            compose_files+=(-f "${CACHE_COMPOSE_FILE}")
        fi
        docker compose "${compose_files[@]}" build yolo-benchmark
    fi
elif ! docker image inspect "${IMAGE_NAME}" > /dev/null 2>&1; then
    repo_checks_die \
        "YOLO benchmark image '${IMAGE_NAME}' is not built yet. Run examples/yolo-benchmark/docker/run.sh setup first."
fi

docker compose -f "${COMPOSE_FILE}" run --rm \
    -e YOLO_BENCHMARK_IN_CONTAINER=1 \
    yolo-benchmark \
    ./examples/yolo-benchmark/docker/run.sh "${command}"

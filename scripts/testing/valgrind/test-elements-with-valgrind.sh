#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
WORK_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
BUILD_SCRIPT="$WORK_ROOT/scripts/build.sh"
SHTOOLS_SCRIPT="$WORK_ROOT/scripts/private/shtools.sh"
OPK_MENU="$WORK_ROOT/tools/opk-menu"
OPKINFER_RETRY_TEST="$WORK_ROOT/development/build/meson-out/opkinfer-retry-valgrind-test"
OPKSINK_FACTORY_FAILURE_TEST="$WORK_ROOT/development/build/meson-out/opksink_factory_failure_test"
OPKSINK_PLUGIN="$WORK_ROOT/development/build/meson-out/libopksink.so"
TEST_PIPELINES_DIR="$WORK_ROOT/config/pipelines"
LOG_DIR="$SCRIPT_DIR/logs"
DEFAULT_SUPPRESSIONS_FILE="$SCRIPT_DIR/suppressed-warnings"

. "$SHTOOLS_SCRIPT"

# Optional tuning knobs
VALGRIND_ERROR_EXITCODE="${VALGRIND_ERROR_EXITCODE:-99}"
NUM_FRAMES="${NUM_FRAMES:-30}"

usage() {
    cat << EOF
Usage:
    test-elements-with-valgrind.sh [clean] [--pipeline <json-path>] [--gen-suppressions] [--show-3rd-party-warnings] [--verbose]
    test-elements-with-valgrind.sh [clean] [--pipeline <json-path>] [--gen-suppressions] [--suppressions-file <path>] [--verbose]

Commands:
  clean  Clear build artifacts first, then build debug and run valgrind tests.

Options:
    --pipeline, -p <json-path>
                 Run valgrind only for the provided pipeline JSON file.
                 If omitted, the yolo26n-320 pipeline is used.
                 Pipelines in the config/pipelines directory can be referenced by their filename as well (with or without .json extension),
                 for everything else, an absolute or relative path to the JSON file shall be provided.
    --show-3rd-party-warnings
                 Disable third-party suppressions and show all Valgrind warnings.
                 By default, repo-local suppressions are enabled.
    --suppressions-file <path>
                 Override the default suppression file path (Default is: $DEFAULT_SUPPRESSIONS_FILE).
                 Has no effect if --show-3rd-party-warnings is used.
    --gen-suppressions
                 Add --gen-suppressions=all to Valgrind output to help capture new third-party suppressions.
    --verbose, -v
                 Enable verbose Valgrind output (adds --verbose to Valgrind arguments).

Environment:
    NUM_FRAMES                 Number of frames to process from the default image (default value: 30)
    VALGRIND_ERROR_EXITCODE    Valgrind error exit code (default: 99)

EOF
}

run_build() {
    local do_clean="$1"

    if [[ "$do_clean" == "true" ]]; then
        msg "Cleaning build directory via scripts/build.sh clean"
        "$BUILD_SCRIPT" clean
    fi

    msg "Building debug artifacts via scripts/build.sh debug"
    "$BUILD_SCRIPT" debug
    meson compile -C "$WORK_ROOT/development/build" \
        opkinfer-retry-valgrind-test \
        opksink_factory_failure_test
}

run_valgrind_all() {
    local requested_pipeline="${1:-}"
    local use_3rd_party_suppressions="${2:-true}"
    local suppressions_file="${3:-$DEFAULT_SUPPRESSIONS_FILE}"
    local generate_suppressions="${4:-false}"
    local verbose_output="${5:-false}"
    local fail_count=0
    local valgrind_error_count=0
    local total_count=0

    mkdir -p "$LOG_DIR"

    if [[ ! -d "$TEST_PIPELINES_DIR" ]]; then
        echo "Pipelines directory not found: $TEST_PIPELINES_DIR" >&2
        exit 1
    fi

    if [[ ! -x "$OPK_MENU" ]]; then
        echo "opk-menu binary not found or not executable: $OPK_MENU" >&2
        exit 1
    fi

    if [[ "$use_3rd_party_suppressions" == "true" && ! -f "$suppressions_file" ]]; then
        echo "Suppression file not found: $suppressions_file" >&2
        exit 1
    fi

    # Ensure freshly built plugins are discoverable and scanned under Valgrind.
    export GST_PLUGIN_PATH="$WORK_ROOT/development/build/meson-out${GST_PLUGIN_PATH:+:$GST_PLUGIN_PATH}"
    export GST_REGISTRY="$LOG_DIR/.gstreamer-registry.bin"
    rm -f "$GST_REGISTRY"

    # Let pipeline templates resolve the configured frame count.
    export NUM_FRAMES="${NUM_FRAMES:-30}"

    local valgrind_args=(
        --leak-check=full
        --num-callers=64
        --show-leak-kinds=all
        --track-origins=yes
        --trace-children=yes
        --error-exitcode="$VALGRIND_ERROR_EXITCODE"
        --xml=yes
    )

    if [[ "$use_3rd_party_suppressions" == "true" ]]; then
        valgrind_args+=(--suppressions="$suppressions_file")
    fi

    if [[ "$generate_suppressions" == "true" ]]; then
        valgrind_args+=(--gen-suppressions=all)
    fi

    if [[ "$verbose_output" == "true" ]]; then
        valgrind_args+=(--verbose)
    fi

    local pipelines=()

    if [[ -n "$requested_pipeline" ]]; then
        local resolved_pipeline=""

        if [[ -f "$requested_pipeline" ]]; then
            resolved_pipeline="$requested_pipeline"
        elif [[ -f "$TEST_PIPELINES_DIR/$requested_pipeline" ]]; then
            resolved_pipeline="$TEST_PIPELINES_DIR/$requested_pipeline"
        elif [[ -f "$TEST_PIPELINES_DIR/${requested_pipeline}.json" ]]; then
            resolved_pipeline="$TEST_PIPELINES_DIR/${requested_pipeline}.json"
        fi

        if [[ -z "$resolved_pipeline" ]]; then
            echo "Requested pipeline JSON was not found: $requested_pipeline" >&2
            exit 1
        fi

        pipelines=("$resolved_pipeline")
    else
        shopt -s nullglob
        pipelines=("$TEST_PIPELINES_DIR/yolo26n-320.json")
        shopt -u nullglob

        # Skip intentionally disabled pipelines by filename convention.
        local filtered_pipelines=()
        local candidate
        for candidate in "${pipelines[@]}"; do
            if [[ "$(basename "$candidate")" == DISABLED* ]]; then
                msg "Skipping disabled pipeline: $candidate"
                continue
            fi
            filtered_pipelines+=("$candidate")
        done
        pipelines=("${filtered_pipelines[@]}")
    fi

    if ((${#pipelines[@]} == 0)); then
        echo "No JSON pipelines found in: $TEST_PIPELINES_DIR" >&2
        exit 1
    fi

    IFS=$'\n' pipelines=($(printf '%s\n' "${pipelines[@]}" | sort))
    unset IFS

    for pipeline in "${pipelines[@]}"; do
        local base
        local log_file
        local log_glob
        local rc=0

        base="$(basename "$pipeline" .json)"
        log_file="$LOG_DIR/${base}.valgrind.%p.xml"
        log_glob="${log_file//%p/*}"

        total_count=$((total_count + 1))
        msg "[$total_count/${#pipelines[@]}] Running valgrind for $pipeline"

        if valgrind \
            "${valgrind_args[@]}" \
            --xml-file="$log_file" \
            "$OPK_MENU" "$pipeline"; then
            msg "PASSED: $pipeline"
            msg "Logs: $log_glob (including child processes)"
        else
            rc=$?
            if [[ "$rc" == "$VALGRIND_ERROR_EXITCODE" ]]; then
                valgrind_error_count=$((valgrind_error_count + 1))
                msg "VALGRIND ERRORS ($rc): $pipeline"
            else
                fail_count=$((fail_count + 1))
                msg "FAILED ($rc): $pipeline"
            fi
            msg "Logs: $log_glob (including child processes)"
        fi
    done

    local retry_log_file="$LOG_DIR/opkinfer-retry.valgrind.%p.xml"
    local retry_log_glob="${retry_log_file//%p/*}"
    local retry_rc=0

    msg "Running OPKinfer failed-start/retry regression under valgrind"
    if valgrind \
        "${valgrind_args[@]}" \
        --show-leak-kinds=definite \
        --errors-for-leak-kinds=definite \
        --xml-file="$retry_log_file" \
        "$OPKINFER_RETRY_TEST" \
        "$WORK_ROOT/config/models/yolo26n-320/opchain.json" \
        "$WORK_ROOT/config/models/yolo26n-320/model.json"; then
        msg "PASSED: OPKinfer failed-start/retry regression"
    else
        retry_rc=$?
        msg "FAILED ($retry_rc): OPKinfer failed-start/retry regression"
        msg "Logs: $retry_log_glob"
    fi

    local opksink_failure_rc=0
    local opksink_failure_case
    local failure_kind
    local failure_name
    local opksink_failure_cases=(
        factory:vconv
        factory:rtp_tee
        factory:drain_fakesink
        factory:audio_silence_src
        factory:audio_tee
        factory:audio_drain_fakesink
        operation:add_vp8enc
        operation:add_aconv
        operation:pad_link_video_drain
        operation:create_video_ghost_pad
        operation:pad_link_audio_real
        operation:pad_link_audio_drain
    )

    for opksink_failure_case in "${opksink_failure_cases[@]}"; do
        failure_kind="${opksink_failure_case%%:*}"
        failure_name="${opksink_failure_case#*:}"
        local failure_log_file="$LOG_DIR/opksink-${failure_kind}-failure-${failure_name}.valgrind.%p.xml"
        local failure_log_glob="${failure_log_file//%p/*}"
        local failure_rc=0

        msg "Running OpkSink ${failure_name} ${failure_kind} failure under valgrind"
        if valgrind \
            "${valgrind_args[@]}" \
            --show-leak-kinds=definite \
            --errors-for-leak-kinds=definite \
            --xml-file="$failure_log_file" \
            "$OPKSINK_FACTORY_FAILURE_TEST" \
            "$OPKSINK_PLUGIN" \
            "$failure_kind" \
            "$failure_name"; then
            msg "PASSED: OpkSink ${failure_name} ${failure_kind} failure"
        else
            failure_rc=$?
            opksink_failure_rc=1
            msg "FAILED ($failure_rc): OpkSink ${failure_name} ${failure_kind} failure"
            msg "Logs: $failure_log_glob"
        fi
    done

    msg "Completed $total_count pipeline(s), failures: $fail_count, valgrind error reports: $valgrind_error_count"

    if ((fail_count > 0 || retry_rc > 0 || opksink_failure_rc > 0)); then
        return 1
    fi

    return 0
}

main() {
    need valgrind

    local do_clean="false"
    local selected_pipeline=""
    local use_3rd_party_suppressions="true"
    local suppressions_file="$DEFAULT_SUPPRESSIONS_FILE"
    local generate_suppressions="false"
    local verbose_output="false"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            clean)
                do_clean="true"
                shift
                ;;
            -p | --pipeline)
                if [[ $# -lt 2 ]]; then
                    echo "Option $1 requires a JSON path argument." >&2
                    usage >&2
                    exit 2
                fi
                selected_pipeline="$2"
                shift 2
                ;;
            --show-3rd-party-warnings)
                use_3rd_party_suppressions="false"
                shift
                ;;
            --suppressions-file)
                if [[ $# -lt 2 ]]; then
                    echo "Option $1 requires a file path argument." >&2
                    usage >&2
                    exit 2
                fi
                suppressions_file="$2"
                shift 2
                ;;
            --gen-suppressions)
                generate_suppressions="true"
                shift
                ;;
            -v | --verbose)
                verbose_output="true"
                shift
                ;;
            -h | --help | help)
                usage
                exit 0
                ;;
            *)
                echo "Unknown argument: $1" >&2
                usage >&2
                exit 2
                ;;
        esac
    done

    run_build "$do_clean"
    run_valgrind_all \
        "$selected_pipeline" \
        "$use_3rd_party_suppressions" \
        "$suppressions_file" \
        "$generate_suppressions" \
        "$verbose_output"
}

main "$@"

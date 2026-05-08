#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
WORK_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
BUILD_SCRIPT="$WORK_ROOT/scripts/build-elements.sh"
SHTOOLS_SCRIPT="$WORK_ROOT/scripts/private/shtools.sh"
PEK_MENU="$WORK_ROOT/tools/pek-menu"
TEST_PIPELINES_DIR="$WORK_ROOT/config/pipelines/testing"
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
                 If omitted, all JSON files from scripts/pipelines/testing are used.
                 Pipelines in the pipelines/testing directory can be referenced by their filename as well (with or without .json extension),
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
        msg "Cleaning build directory via build-elements.sh clean"
        "$BUILD_SCRIPT" clean
    fi

    msg "Building debug artifacts via build-elements.sh debug"
    "$BUILD_SCRIPT" debug
}

run_valgrind_all() {
    local requested_pipeline="${1:-}"
    local use_3rd_party_suppressions="${2:-true}"
    local suppressions_file="${3:-$DEFAULT_SUPPRESSIONS_FILE}"
    local generate_suppressions="${4:-false}"
    local verbose_output="${5:-false}"
    local fail_count=0
    local total_count=0

    mkdir -p "$LOG_DIR"

    if [[ ! -d "$TEST_PIPELINES_DIR" ]]; then
        echo "Pipelines directory not found: $TEST_PIPELINES_DIR" >&2
        exit 1
    fi

    if [[ ! -x "$PEK_MENU" ]]; then
        echo "pek-menu binary not found or not executable: $PEK_MENU" >&2
        exit 1
    fi

    if [[ "$use_3rd_party_suppressions" == "true" && ! -f "$suppressions_file" ]]; then
        echo "Suppression file not found: $suppressions_file" >&2
        exit 1
    fi

    # Ensure freshly built plugins are discoverable and pipeline templates can
    # resolve the configured frame count from the process environment.
    export GST_PLUGIN_PATH="$WORK_ROOT/development/build/meson-out${GST_PLUGIN_PATH:+:$GST_PLUGIN_PATH}"
    export NUM_FRAMES="${NUM_FRAMES:-30}"

    local valgrind_args=(
        --leak-check=full
        --show-leak-kinds=all
        --track-origins=yes
        --trace-children=yes
        --error-exitcode="$VALGRIND_ERROR_EXITCODE"
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
        pipelines=("$TEST_PIPELINES_DIR"/*.json)
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
        local rc=0

        base="$(basename "$pipeline" .json)"
        log_file="$LOG_DIR/${base}.valgrind.log"

        total_count=$((total_count + 1))
        msg "[$total_count/${#pipelines[@]}] Running valgrind for $pipeline"

        if valgrind \
            "${valgrind_args[@]}" \
            --log-file="$log_file.%p" \
            "$PEK_MENU" "$pipeline"; then
            msg "PASSED: $pipeline"
            msg "Logs: $log_file.* (including child processes)"
        else
            rc=$?
            fail_count=$((fail_count + 1))
            msg "FAILED ($rc): $pipeline"
            msg "Logs: $log_file.* (including child processes)"
        fi
    done

    msg "Completed $total_count pipeline(s), failures: $fail_count"

    if ((fail_count > 0)); then
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
